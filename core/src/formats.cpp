#include "savetinker/formats.hpp"

#include <bit>
#include <cstring>
#include <stdexcept>

#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>

namespace st {

namespace {

struct FormatError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Bounds-checked reads in either byte order.
class Reader {
public:
    Reader(ByteView data, bool bigEndian) : data_(data), big_(bigEndian) {}

    std::size_t size() const { return data_.size(); }
    ByteView view() const { return data_; }

    void need(std::size_t offset, std::size_t length) const {
        if (offset > data_.size() || length > data_.size() - offset) throw FormatError("data ends early");
    }

    std::uint64_t uint(std::size_t offset, std::size_t width) const {
        need(offset, width);
        std::uint64_t value = 0;
        for (std::size_t i = 0; i < width; ++i) {
            const std::uint64_t byte = data_[offset + i];
            value |= big_ ? byte << (8 * (width - 1 - i)) : byte << (8 * i);
        }
        return value;
    }
    std::uint8_t u8(std::size_t offset) const { return static_cast<std::uint8_t>(uint(offset, 1)); }
    std::uint16_t u16(std::size_t offset) const { return static_cast<std::uint16_t>(uint(offset, 2)); }
    std::uint32_t u24(std::size_t offset) const { return static_cast<std::uint32_t>(uint(offset, 3)); }
    std::uint32_t u32(std::size_t offset) const { return static_cast<std::uint32_t>(uint(offset, 4)); }
    std::uint64_t u64(std::size_t offset) const { return uint(offset, 8); }

    std::string cString(std::size_t offset) const {
        need(offset, 1);
        const auto* begin = reinterpret_cast<const char*>(data_.data() + offset);
        const auto* end = static_cast<const char*>(std::memchr(begin, 0, data_.size() - offset));
        if (!end) throw FormatError("unterminated string");
        return std::string(begin, end);
    }

    Bytes bytes(std::size_t offset, std::size_t length) const {
        need(offset, length);
        return Bytes(data_.begin() + static_cast<std::ptrdiff_t>(offset), data_.begin() + static_cast<std::ptrdiff_t>(offset + length));
    }

private:
    ByteView data_;
    bool big_;
};

template <typename Function>
auto guard(Function&& function) -> decltype(function()) {
    try {
        return function();
    } catch (const FormatError& error) {
        return std::unexpected(std::string(error.what()));
    } catch (const std::length_error&) {
        return std::unexpected(std::string("data is too large"));
    }
}

}

std::expected<Bytes, std::string> zstdDecompress(ByteView data, const std::vector<ByteView>& dictionaries, std::uint64_t maxSize) {
    const unsigned dictionaryId = ZSTD_getDictID_fromFrame(data.data(), data.size());
    ByteView dictionary;
    if (dictionaryId != 0) {
        for (const auto& candidate : dictionaries)
            if (ZSTD_getDictID_fromDict(candidate.data(), candidate.size()) == dictionaryId) dictionary = candidate;
        if (dictionary.empty()) return std::unexpected("needs zstd dictionary " + std::to_string(dictionaryId));
    }

    std::unique_ptr<ZSTD_DCtx, decltype(&ZSTD_freeDCtx)> context(ZSTD_createDCtx(), &ZSTD_freeDCtx);
    if (!context) return std::unexpected("out of memory");
    if (!dictionary.empty() && ZSTD_isError(ZSTD_DCtx_loadDictionary(context.get(), dictionary.data(), dictionary.size())))
        return std::unexpected("invalid zstd dictionary");

    const auto declared = ZSTD_getFrameContentSize(data.data(), data.size());
    if (declared == ZSTD_CONTENTSIZE_ERROR) return std::unexpected("not zstd data");
    Bytes output;
    if (declared != ZSTD_CONTENTSIZE_UNKNOWN) {
        if (declared > maxSize) return std::unexpected("decompressed data is too large");
        output.resize(static_cast<std::size_t>(declared));
        const auto written = ZSTD_decompressDCtx(context.get(), output.data(), output.size(), data.data(), data.size());
        if (ZSTD_isError(written)) return std::unexpected(std::string("zstd: ") + ZSTD_getErrorName(written));
        output.resize(written);
        return output;
    }

    ZSTD_inBuffer input{data.data(), data.size(), 0};
    Bytes chunk(ZSTD_DStreamOutSize());
    while (input.pos < input.size) {
        ZSTD_outBuffer out{chunk.data(), chunk.size(), 0};
        const auto result = ZSTD_decompressStream(context.get(), &out, &input);
        if (ZSTD_isError(result)) return std::unexpected(std::string("zstd: ") + ZSTD_getErrorName(result));
        if (output.size() + out.pos > maxSize) return std::unexpected("decompressed data is too large");
        output.insert(output.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(out.pos));
        if (result == 0 && out.pos < out.size) break;
    }
    return output;
}

std::expected<std::vector<SarcEntry>, std::string> readSarc(ByteView data) {
    return guard([&]() -> std::expected<std::vector<SarcEntry>, std::string> {
        if (data.size() < 0x14 || std::memcmp(data.data(), "SARC", 4) != 0) return std::unexpected("not a SARC archive");
        const bool big = data[6] == 0xFE && data[7] == 0xFF;
        const Reader reader(data, big);
        const std::size_t headerSize = reader.u16(4);
        const std::size_t dataStart = reader.u32(0xC);
        const std::size_t sfat = headerSize;
        if (reader.bytes(sfat, 4) != Bytes{'S', 'F', 'A', 'T'}) return std::unexpected("SARC has no SFAT section");
        const std::size_t count = reader.u16(sfat + 6);
        const std::size_t nodes = sfat + 0xC;
        const std::size_t names = nodes + count * 16 + 8;

        std::vector<SarcEntry> entries;
        entries.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            const std::size_t node = nodes + i * 16;
            const std::uint32_t attributes = reader.u32(node + 4);
            const std::size_t begin = reader.u32(node + 8);
            const std::size_t end = reader.u32(node + 12);
            if (end < begin) throw FormatError("bad SARC entry");
            std::string name = (attributes >> 24) ? reader.cString(names + (attributes & 0xFFFF) * 4) : std::to_string(i);
            entries.push_back({std::move(name), reader.bytes(dataStart + begin, end - begin)});
        }
        return entries;
    });
}

namespace {

class BymlParser {
public:
    explicit BymlParser(ByteView data) : reader_(data, data[0] == 'B') {}

    BymlValue parse() {
        const std::uint16_t version = reader_.u16(2);
        if (version < 2 || version > 7) throw FormatError("unsupported BYML version " + std::to_string(version));
        const std::uint32_t keyTable = reader_.u32(4);
        const std::uint32_t stringTable = reader_.u32(8);
        const std::uint32_t root = reader_.u32(12);
        if (keyTable) keys_ = stringTableAt(keyTable);
        if (stringTable) strings_ = stringTableAt(stringTable);
        if (!root) return {};
        return node(reader_.u8(root), root, 0);
    }

private:
    std::vector<std::string> stringTableAt(std::size_t offset) {
        if (reader_.u8(offset) != 0xC2) throw FormatError("bad BYML string table");
        const std::uint32_t count = reader_.u24(offset + 1);
        std::vector<std::string> table;
        table.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) table.push_back(reader_.cString(offset + reader_.u32(offset + 4 + i * 4)));
        return table;
    }

    const std::string& lookup(const std::vector<std::string>& table, std::uint32_t index) const {
        if (index >= table.size()) throw FormatError("BYML string index out of range");
        return table[index];
    }

    BymlValue node(std::uint8_t type, std::size_t offset, int depth) {
        if (depth > 64) throw FormatError("BYML nested too deeply");
        BymlValue result;
        const std::uint32_t count = reader_.u24(offset + 1);
        switch (type) {
        case 0xC0: {
            auto array = std::make_shared<BymlArray>();
            const std::size_t values = (offset + 4 + count + 3) & ~std::size_t{3};
            for (std::uint32_t i = 0; i < count; ++i) array->push_back(value(reader_.u8(offset + 4 + i), values + i * 4, depth));
            result.value = array;
            break;
        }
        case 0xC1: {
            auto map = std::make_shared<BymlMap>();
            for (std::uint32_t i = 0; i < count; ++i) {
                const std::size_t entry = offset + 4 + i * 8;
                (*map)[lookup(keys_, reader_.u24(entry))] = value(reader_.u8(entry + 3), entry + 4, depth);
            }
            result.value = map;
            break;
        }
        case 0x20:
        case 0x21: {
            auto map = std::make_shared<BymlHashMap>();
            const std::size_t keyWidth = type == 0x20 ? 4 : 8;
            const std::size_t entrySize = keyWidth + 4;
            for (std::uint32_t i = 0; i < count; ++i) {
                const std::size_t entry = offset + 4 + i * entrySize;
                const std::uint64_t key = reader_.uint(entry, keyWidth);
                (*map)[key] = value(reader_.u8(offset + 4 + count * entrySize + i), entry + keyWidth, depth);
            }
            result.value = map;
            break;
        }
        default: throw FormatError("unsupported BYML node type " + std::to_string(type));
        }
        return result;
    }

    BymlValue value(std::uint8_t type, std::size_t offset, int depth) {
        BymlValue result;
        const std::uint32_t raw = reader_.u32(offset);
        switch (type) {
        case 0xC0:
        case 0xC1:
        case 0x20:
        case 0x21: return node(type, raw, depth + 1);
        case 0xA0: result.value = lookup(strings_, raw); break;
        case 0xA1: result.value = reader_.bytes(raw + 4, reader_.u32(raw)); break;
        case 0xA2: result.value = reader_.bytes(raw + 8, reader_.u32(raw)); break;
        case 0xD0: result.value = raw != 0; break;
        case 0xD1: result.value = static_cast<std::int64_t>(static_cast<std::int32_t>(raw)); break;
        case 0xD2: result.value = static_cast<double>(std::bit_cast<float>(raw)); break;
        case 0xD3: result.value = static_cast<std::uint64_t>(raw); break;
        case 0xD4: result.value = static_cast<std::int64_t>(reader_.u64(raw)); break;
        case 0xD5: result.value = reader_.u64(raw); break;
        case 0xD6: result.value = std::bit_cast<double>(reader_.u64(raw)); break;
        case 0xFF: break;
        default: throw FormatError("unsupported BYML value type " + std::to_string(type));
        }
        return result;
    }

    Reader reader_;
    std::vector<std::string> keys_;
    std::vector<std::string> strings_;
};

void appendUtf8(std::string& out, std::uint32_t codePoint) {
    if (codePoint < 0x80) {
        out.push_back(static_cast<char>(codePoint));
    } else if (codePoint < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    } else if (codePoint < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }
}

void appendLe16(std::string& out, std::uint16_t value) {
    out.push_back(static_cast<char>(value & 0xFF));
    out.push_back(static_cast<char>(value >> 8));
}

// Decodes one message into UTF-8 with tags kept as 0x0E group type size params / 0x0F group type records.
void decodeMessage(const Reader& reader, std::size_t begin, std::size_t end, bool wide, MsbtMessage& message) {
    const std::size_t unit = wide ? 2 : 1;
    std::size_t i = begin;
    while (i + unit <= end) {
        const std::uint32_t code = wide ? reader.u16(i) : reader.u8(i);
        if (code == 0) break;
        if (code == 0x0E) {
            const std::uint16_t group = reader.u16(i + unit);
            const std::uint16_t type = reader.u16(i + unit + 2);
            const std::uint16_t size = reader.u16(i + unit + 4);
            const Bytes params = reader.bytes(i + unit + 6, size);
            message.raw.push_back('\x0E');
            appendLe16(message.raw, group);
            appendLe16(message.raw, type);
            appendLe16(message.raw, size);
            message.raw.append(params.begin(), params.end());
            i += unit + 6 + size;
            continue;
        }
        if (code == 0x0F) {
            message.raw.push_back('\x0F');
            appendLe16(message.raw, reader.u16(i + unit));
            appendLe16(message.raw, reader.u16(i + unit + 2));
            i += unit + 4;
            continue;
        }
        if (!wide) {
            message.raw.push_back(static_cast<char>(code));
            message.text.push_back(static_cast<char>(code));
            i += 1;
            continue;
        }
        std::uint32_t codePoint = code;
        i += 2;
        if (code >= 0xD800 && code < 0xDC00 && i + 2 <= end) {
            const std::uint16_t low = reader.u16(i);
            if (low >= 0xDC00 && low < 0xE000) {
                codePoint = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                i += 2;
            }
        }
        std::string encoded;
        appendUtf8(encoded, codePoint);
        message.raw += encoded;
        message.text += encoded;
    }
}

}

std::expected<BymlValue, std::string> readByml(ByteView data) {
    return guard([&]() -> std::expected<BymlValue, std::string> {
        if (data.size() < 16 || !((data[0] == 'B' && data[1] == 'Y') || (data[0] == 'Y' && data[1] == 'B')))
            return std::unexpected("not a BYML file");
        return BymlParser(data).parse();
    });
}

std::expected<std::vector<MsbtMessage>, std::string> readMsbt(ByteView data) {
    return guard([&]() -> std::expected<std::vector<MsbtMessage>, std::string> {
        if (data.size() < 0x20 || std::memcmp(data.data(), "MsgStdBn", 8) != 0) return std::unexpected("not an MSBT file");
        const bool big = data[8] == 0xFE && data[9] == 0xFF;
        const Reader reader(data, big);
        const std::uint8_t encoding = reader.u8(0xC);
        if (encoding > 1) return std::unexpected("UTF-32 messages are not supported");
        const bool wide = encoding == 1;
        const std::uint16_t sectionCount = reader.u16(0xE);

        std::size_t labels = 0, texts = 0, textsSize = 0;
        std::size_t offset = 0x20;
        for (std::uint16_t i = 0; i < sectionCount && offset + 16 <= data.size(); ++i) {
            const Bytes magic = reader.bytes(offset, 4);
            const std::size_t size = reader.u32(offset + 4);
            if (magic == Bytes{'L', 'B', 'L', '1'}) labels = offset + 16;
            if (magic == Bytes{'T', 'X', 'T', '2'}) {
                texts = offset + 16;
                textsSize = size;
            }
            offset = (offset + 16 + size + 15) & ~std::size_t{15};
        }
        if (!texts) return std::unexpected("MSBT has no text section");

        const std::uint32_t count = reader.u32(texts);
        std::vector<MsbtMessage> messages(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::size_t begin = texts + reader.u32(texts + 4 + i * 4);
            const std::size_t end = i + 1 < count ? texts + reader.u32(texts + 8 + i * 4) : texts + textsSize;
            messages[i].label = std::to_string(i);
            decodeMessage(reader, begin, end, wide, messages[i]);
        }
        if (labels) {
            const std::uint32_t slots = reader.u32(labels);
            for (std::uint32_t slot = 0; slot < slots; ++slot) {
                const std::uint32_t inSlot = reader.u32(labels + 4 + slot * 8);
                std::size_t position = labels + reader.u32(labels + 8 + slot * 8);
                for (std::uint32_t j = 0; j < inSlot; ++j) {
                    const std::uint8_t length = reader.u8(position);
                    const Bytes name = reader.bytes(position + 1, length);
                    const std::uint32_t index = reader.u32(position + 1 + length);
                    if (index < messages.size()) messages[index].label.assign(name.begin(), name.end());
                    position += 5 + length;
                }
            }
        }
        return messages;
    });
}

}

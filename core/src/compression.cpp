#include "savetinker/compression.hpp"

#include <memory>
#include <miniz.h>
#include <zstd.h>

#include "savetinker/checksum.hpp"

namespace st {

namespace {

struct MallocFree {
    void operator()(void* pointer) const { mz_free(pointer); }
};

void putU32(Bytes& out, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
}

std::uint32_t readU32(ByteView data, std::size_t offset) {
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(data[offset + i]) << (8 * i);
    return value;
}

std::expected<ByteView, std::string> gzipBody(ByteView data) {
    constexpr std::uint8_t kExtra = 4, kName = 8, kComment = 16, kHeaderCrc = 2;
    if (data.size() < 18 || data[0] != 0x1F || data[1] != 0x8B || data[2] != 8) return std::unexpected("not gzip data");
    const std::uint8_t flags = data[3];
    std::size_t offset = 10;
    auto skipString = [&] {
        while (offset < data.size() && data[offset] != 0) ++offset;
        ++offset;
    };
    if (flags & kExtra) offset += 2 + (data[offset] | (data[offset + 1] << 8));
    if (flags & kName) skipString();
    if (flags & kComment) skipString();
    if (flags & kHeaderCrc) offset += 2;
    if (offset + 8 > data.size()) return std::unexpected("truncated gzip data");
    return data.subspan(offset, data.size() - offset - 8);
}

}

std::expected<DeflateFormat, std::string> parseDeflateFormat(std::string_view name) {
    if (name == "raw" || name == "deflate") return DeflateFormat::Raw;
    if (name == "zlib") return DeflateFormat::Zlib;
    if (name == "gzip") return DeflateFormat::Gzip;
    return std::unexpected("unknown format \"" + std::string(name) + "\" (raw, zlib or gzip)");
}

std::expected<Bytes, std::string> deflateCompress(ByteView data, DeflateFormat format, int level) {
    if (level < 0 || level > 10) return std::unexpected("level must be 0-10");
    const int windowBits = format == DeflateFormat::Zlib ? 15 : -15;
    const int flags = static_cast<int>(tdefl_create_comp_flags_from_zip_params(level, windowBits, MZ_DEFAULT_STRATEGY));
    std::size_t length = 0;
    std::unique_ptr<void, MallocFree> packed(tdefl_compress_mem_to_heap(data.data(), data.size(), &length, flags));
    if (!packed) return std::unexpected("compression failed");
    const auto* bytes = static_cast<const std::uint8_t*>(packed.get());
    Bytes out;
    if (format == DeflateFormat::Gzip) out = {0x1F, 0x8B, 8, 0, 0, 0, 0, 0, 0, 0xFF};
    out.insert(out.end(), bytes, bytes + length);
    if (format == DeflateFormat::Gzip) {
        putU32(out, crc32(data));
        putU32(out, static_cast<std::uint32_t>(data.size()));
    }
    return out;
}

std::expected<Bytes, std::string> deflateDecompress(ByteView data, DeflateFormat format, std::uint64_t maxSize) {
    ByteView body = data;
    if (format == DeflateFormat::Gzip) {
        auto found = gzipBody(data);
        if (!found) return std::unexpected(found.error());
        body = *found;
    }
    struct Sink {
        Bytes out;
        std::uint64_t maxSize;
        bool tooLarge = false;
    } sink{{}, maxSize};
    auto put = [](const void* buffer, int length, void* user) -> int {
        auto& target = *static_cast<Sink*>(user);
        if (target.out.size() + static_cast<std::size_t>(length) > target.maxSize) {
            target.tooLarge = true;
            return 0;
        }
        const auto* bytes = static_cast<const std::uint8_t*>(buffer);
        target.out.insert(target.out.end(), bytes, bytes + length);
        return 1;
    };
    std::size_t consumed = body.size();
    const int flags = format == DeflateFormat::Zlib ? TINFL_FLAG_PARSE_ZLIB_HEADER : 0;
    if (!tinfl_decompress_mem_to_callback(body.data(), &consumed, put, &sink, flags))
        return std::unexpected(sink.tooLarge ? "decompressed data is too large" : "invalid compressed data");
    Bytes out = std::move(sink.out);
    if (format == DeflateFormat::Gzip && (readU32(data, data.size() - 8) != crc32(out) ||
                                          readU32(data, data.size() - 4) != static_cast<std::uint32_t>(out.size())))
        return std::unexpected("gzip checksum does not match");
    return out;
}

std::expected<Bytes, std::string> zstdCompress(ByteView data, int level) {
    if (level < 1 || level > ZSTD_maxCLevel()) return std::unexpected("level must be 1-" + std::to_string(ZSTD_maxCLevel()));
    Bytes out(ZSTD_compressBound(data.size()));
    const auto written = ZSTD_compress(out.data(), out.size(), data.data(), data.size(), level);
    if (ZSTD_isError(written)) return std::unexpected(std::string("zstd: ") + ZSTD_getErrorName(written));
    out.resize(written);
    return out;
}

}

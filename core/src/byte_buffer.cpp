#include "savetinker/byte_buffer.hpp"

#include <algorithm>

namespace st {

bool ByteBuffer::readBit(std::size_t offset, unsigned bit) const {
    if (bit > 7) throw std::out_of_range("bit index must be 0-7");
    return (read<std::uint8_t>(offset) >> bit) & 1u;
}

void ByteBuffer::writeBit(std::size_t offset, unsigned bit, bool value) {
    if (bit > 7) throw std::out_of_range("bit index must be 0-7");
    auto byte = read<std::uint8_t>(offset);
    const auto mask = static_cast<std::uint8_t>(1u << bit);
    byte = value ? (byte | mask) : (byte & ~mask);
    write<std::uint8_t>(offset, byte);
}

Bytes ByteBuffer::readBytes(std::size_t offset, std::size_t length) const {
    requireRange(offset, length);
    return Bytes(bytes_.begin() + offset, bytes_.begin() + offset + length);
}

void ByteBuffer::writeBytes(std::size_t offset, ByteView data) {
    requireRange(offset, data.size());
    std::ranges::copy(data, bytes_.begin() + offset);
}

std::string ByteBuffer::readUtf8(std::size_t offset, std::size_t maxLength) const {
    requireRange(offset, 0);
    const std::size_t available = std::min(maxLength, bytes_.size() - offset);
    const auto begin = bytes_.begin() + offset;
    const auto end = std::find(begin, begin + available, std::uint8_t{0});
    return std::string(begin, end);
}

std::string ByteBuffer::readUtf16(std::size_t offset, std::size_t maxUnits, Endian endian) const {
    std::u16string units;
    for (std::size_t i = 0; i < maxUnits; ++i) {
        const std::size_t position = offset + i * 2;
        if (position + 2 > bytes_.size()) break;
        const auto unit = read<std::uint16_t>(position, endian);
        if (unit == 0) break;
        units.push_back(static_cast<char16_t>(unit));
    }
    return utf16ToUtf8(units);
}

void ByteBuffer::writeUtf8(std::size_t offset, std::string_view text, std::size_t fieldLength) {
    if (text.size() > fieldLength) throw std::length_error("text longer than field");
    requireRange(offset, fieldLength);
    std::fill_n(bytes_.begin() + offset, fieldLength, std::uint8_t{0});
    std::ranges::copy(text, bytes_.begin() + offset);
}

void ByteBuffer::writeUtf16(std::size_t offset, std::string_view utf8Text, std::size_t fieldUnits,
                            Endian endian) {
    const auto units = utf8ToUtf16(utf8Text);
    if (units.size() > fieldUnits) throw std::length_error("text longer than field");
    requireRange(offset, fieldUnits * 2);
    for (std::size_t i = 0; i < fieldUnits; ++i) {
        const std::uint16_t unit = i < units.size() ? static_cast<std::uint16_t>(units[i]) : 0;
        write<std::uint16_t>(offset + i * 2, unit, endian);
    }
}

std::optional<std::size_t> ByteBuffer::find(ByteView pattern, std::size_t from) const {
    if (pattern.empty() || from >= bytes_.size()) return std::nullopt;
    const auto match = std::search(bytes_.begin() + from, bytes_.end(), pattern.begin(), pattern.end());
    if (match == bytes_.end()) return std::nullopt;
    return static_cast<std::size_t>(match - bytes_.begin());
}

std::u16string utf8ToUtf16(std::string_view utf8) {
    std::u16string out;
    std::size_t i = 0;
    while (i < utf8.size()) {
        const auto lead = static_cast<unsigned char>(utf8[i]);
        char32_t codePoint;
        std::size_t extra;
        if (lead < 0x80) { codePoint = lead; extra = 0; }
        else if ((lead & 0xE0) == 0xC0) { codePoint = lead & 0x1F; extra = 1; }
        else if ((lead & 0xF0) == 0xE0) { codePoint = lead & 0x0F; extra = 2; }
        else if ((lead & 0xF8) == 0xF0) { codePoint = lead & 0x07; extra = 3; }
        else throw std::invalid_argument("invalid UTF-8");
        if (i + extra >= utf8.size()) throw std::invalid_argument("truncated UTF-8");
        for (std::size_t k = 1; k <= extra; ++k) {
            const auto next = static_cast<unsigned char>(utf8[i + k]);
            if ((next & 0xC0) != 0x80) throw std::invalid_argument("invalid UTF-8");
            codePoint = (codePoint << 6) | (next & 0x3F);
        }
        i += extra + 1;
        if (codePoint >= 0x10000) {
            codePoint -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (codePoint >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (codePoint & 0x3FF)));
        } else {
            out.push_back(static_cast<char16_t>(codePoint));
        }
    }
    return out;
}

std::string utf16ToUtf8(std::u16string_view utf16) {
    std::string out;
    for (std::size_t i = 0; i < utf16.size(); ++i) {
        char32_t codePoint = utf16[i];
        if (codePoint >= 0xD800 && codePoint <= 0xDBFF && i + 1 < utf16.size()) {
            const char32_t low = utf16[i + 1];
            if (low >= 0xDC00 && low <= 0xDFFF) {
                codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (low - 0xDC00);
                ++i;
            }
        }
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
    return out;
}

}

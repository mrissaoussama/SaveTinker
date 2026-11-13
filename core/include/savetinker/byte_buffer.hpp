#pragma once

#include <bit>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace st {

enum class Endian { Little, Big };

using Bytes = std::vector<std::uint8_t>;
using ByteView = std::span<const std::uint8_t>;

class ByteBuffer {
public:
    ByteBuffer() = default;
    explicit ByteBuffer(Bytes bytes) : bytes_(std::move(bytes)) {}

    std::size_t size() const { return bytes_.size(); }
    ByteView view() const { return bytes_; }
    const Bytes& bytes() const { return bytes_; }
    Bytes& bytes() { return bytes_; }

    template <std::integral T>
    T read(std::size_t offset, Endian endian = Endian::Little) const {
        requireRange(offset, sizeof(T));
        T value;
        std::memcpy(&value, bytes_.data() + offset, sizeof(T));
        return fromEndian(value, endian);
    }

    template <std::integral T>
    void write(std::size_t offset, T value, Endian endian = Endian::Little) {
        requireRange(offset, sizeof(T));
        T stored = fromEndian(value, endian);
        std::memcpy(bytes_.data() + offset, &stored, sizeof(T));
    }

    float readFloat(std::size_t offset, Endian endian = Endian::Little) const {
        return std::bit_cast<float>(read<std::uint32_t>(offset, endian));
    }
    double readDouble(std::size_t offset, Endian endian = Endian::Little) const {
        return std::bit_cast<double>(read<std::uint64_t>(offset, endian));
    }
    void writeFloat(std::size_t offset, float value, Endian endian = Endian::Little) {
        write(offset, std::bit_cast<std::uint32_t>(value), endian);
    }
    void writeDouble(std::size_t offset, double value, Endian endian = Endian::Little) {
        write(offset, std::bit_cast<std::uint64_t>(value), endian);
    }

    bool readBit(std::size_t offset, unsigned bit) const;
    void writeBit(std::size_t offset, unsigned bit, bool value);

    Bytes readBytes(std::size_t offset, std::size_t length) const;
    void writeBytes(std::size_t offset, ByteView data);

    // Reads up to maxLength bytes, stopping at the first NUL.
    std::string readUtf8(std::size_t offset, std::size_t maxLength) const;
    // Reads up to maxUnits UTF-16 code units, stopping at NUL, returns UTF-8.
    std::string readUtf16(std::size_t offset, std::size_t maxUnits, Endian endian = Endian::Little) const;
    // Writes text into a fixed-size field, padding with zeros. Throws if it does not fit.
    void writeUtf8(std::size_t offset, std::string_view text, std::size_t fieldLength);
    void writeUtf16(std::size_t offset, std::string_view utf8Text, std::size_t fieldUnits,
                    Endian endian = Endian::Little);

    std::optional<std::size_t> find(ByteView pattern, std::size_t from = 0) const;

private:
    void requireRange(std::size_t offset, std::size_t length) const {
        if (offset > bytes_.size() || length > bytes_.size() - offset)
            throw std::out_of_range("access out of range at offset " + std::to_string(offset) +
                                    " length " + std::to_string(length));
    }

    template <std::integral T>
    static T fromEndian(T value, Endian endian) {
        const bool nativeLittle = std::endian::native == std::endian::little;
        const bool wantLittle = endian == Endian::Little;
        return nativeLittle == wantLittle ? value : std::byteswap(value);
    }

    Bytes bytes_;
};

std::u16string utf8ToUtf16(std::string_view utf8);
std::string utf16ToUtf8(std::u16string_view utf16);

}

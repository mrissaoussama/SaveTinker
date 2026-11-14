#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "savetinker/byte_buffer.hpp"

namespace st {

std::uint32_t crc32(ByteView data, std::uint32_t seed = 0);
std::uint16_t crc16Ccitt(ByteView data, std::uint16_t initial = 0xFFFF);
std::uint32_t adler32(ByteView data);
std::uint32_t murmur3(ByteView data, std::uint32_t seed = 0);

std::array<std::uint8_t, 16> md5(ByteView data);
std::array<std::uint8_t, 20> sha1(ByteView data);
std::array<std::uint8_t, 32> sha256(ByteView data);

std::string toHex(ByteView data);
Bytes fromHex(std::string_view hex);

template <std::size_t N>
std::string toHex(const std::array<std::uint8_t, N>& digest) {
    return toHex(ByteView(digest));
}

}

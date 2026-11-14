#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

#include "savetinker/byte_buffer.hpp"

namespace st {

enum class AesMode { Ecb, Cbc, Ctr };

std::expected<AesMode, std::string> parseAesMode(std::string_view name);

// No padding: ECB and CBC need whole 16-byte blocks. Keys are 16, 24 or 32 bytes.
std::expected<Bytes, std::string> aesEncrypt(AesMode mode, ByteView key, ByteView data, ByteView iv = {});
std::expected<Bytes, std::string> aesDecrypt(AesMode mode, ByteView key, ByteView data, ByteView iv = {});
std::expected<std::array<std::uint8_t, 16>, std::string> aesCmac(ByteView key, ByteView data);

std::array<std::uint8_t, 32> hmacSha256(ByteView key, ByteView data);
Bytes xorWith(ByteView data, ByteView key);

}

#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

#include "savetinker/byte_buffer.hpp"

namespace st {

enum class DeflateFormat { Raw, Zlib, Gzip };

std::expected<DeflateFormat, std::string> parseDeflateFormat(std::string_view name);
std::expected<Bytes, std::string> deflateCompress(ByteView data, DeflateFormat format, int level = 6);
std::expected<Bytes, std::string> deflateDecompress(ByteView data, DeflateFormat format, std::uint64_t maxSize);
std::expected<Bytes, std::string> zstdCompress(ByteView data, int level = 3);

}

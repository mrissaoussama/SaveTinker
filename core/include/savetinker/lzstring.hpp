#pragma once

#include <expected>
#include <string>
#include <string_view>

namespace st {

// lz-string's compressToBase64 / decompressFromBase64 (RPG Maker MV saves). Text is UTF-8 here and
// UTF-16 inside the format, as in JavaScript.
std::string lzCompressToBase64(std::string_view utf8);
std::expected<std::string, std::string> lzDecompressFromBase64(std::string_view base64);

}

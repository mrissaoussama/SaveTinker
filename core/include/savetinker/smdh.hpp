#pragma once

#include <optional>
#include <string>

#include "savetinker/byte_buffer.hpp"

namespace st {

// 3DS title metadata (icon.bin / "icon" in ExeFS).
struct Smdh {
    std::string shortTitle;
    std::string publisher;
    // 48x48 icon as an uncompressed TGA image that stb_image can load.
    Bytes iconTga;
};

inline constexpr std::size_t kSmdhSize = 0x36C0;

// `language` is the 3DS system language index (0 Japanese, 1 English, ...). Falls back to English, then to
// the first non-empty title.
std::optional<Smdh> parseSmdh(ByteView data, int language);

}

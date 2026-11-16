#pragma once

#include <optional>
#include <string>

#include "savetinker/byte_buffer.hpp"

namespace st {

struct Smdh {
    std::string shortTitle;
    std::string publisher;
    Bytes iconTga;
};

inline constexpr std::size_t kSmdhSize = 0x36C0;

// `language` is the 3DS system language index (0 Japanese, 1 English, ...). Falls back to English, then to
// the first non-empty title.
std::optional<Smdh> parseSmdh(ByteView data, int language);

// The banner of a DS or DSi title (DSiWare and DS cartridges have no SMDH): the first line of the title is
// the name, the last line the publisher, and the 32x32 icon is returned as a TGA.
inline constexpr std::size_t kDsBannerMinSize = 0x840;
std::optional<Smdh> parseDsBanner(ByteView data, int language);

}

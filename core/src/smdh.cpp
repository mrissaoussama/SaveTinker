#include "savetinker/smdh.hpp"

#include <array>

namespace st {

namespace {

constexpr std::size_t kTitlesOffset = 0x8;
constexpr std::size_t kTitleSize = 0x200;
constexpr std::size_t kTitleCount = 16;
constexpr std::size_t kLargeIconOffset = 0x24C0;
constexpr int kIconSize = 48;

// Pixel order inside each 8x8 tile of a 3DS texture.
constexpr std::array<std::uint8_t, 64> kTileOrder = {
    0,  1,  8,  9,  2,  3,  10, 11, 16, 17, 24, 25, 18, 19, 26, 27, 4,  5,  12, 13, 6,  7,
    14, 15, 20, 21, 28, 29, 22, 23, 30, 31, 32, 33, 40, 41, 34, 35, 42, 43, 48, 49, 56, 57,
    50, 51, 58, 59, 36, 37, 44, 45, 38, 39, 46, 47, 52, 53, 60, 61, 54, 55, 62, 63};

std::string readTitleText(const ByteBuffer& buffer, std::size_t offset, std::size_t characters) {
    std::u16string text;
    for (std::size_t i = 0; i < characters; ++i) {
        const auto unit = buffer.read<std::uint16_t>(offset + i * 2);
        if (unit == 0) break;
        text.push_back(unit == u'\n' ? u' ' : static_cast<char16_t>(unit));
    }
    return utf16ToUtf8(text);
}

Bytes iconToTga(const ByteBuffer& buffer) {
    Bytes tga(18 + kIconSize * kIconSize * 4, 0);
    tga[2] = 2;
    tga[12] = kIconSize;
    tga[14] = kIconSize;
    tga[16] = 32;
    tga[17] = 0x28;
    std::size_t source = kLargeIconOffset;
    for (int tileY = 0; tileY < kIconSize; tileY += 8) {
        for (int tileX = 0; tileX < kIconSize; tileX += 8) {
            for (const auto order : kTileOrder) {
                const int x = tileX + (order & 7);
                const int y = tileY + (order >> 3);
                const auto pixel = buffer.read<std::uint16_t>(source);
                source += 2;
                const auto red = static_cast<std::uint8_t>(((pixel >> 11) & 0x1F) * 255 / 31);
                const auto green = static_cast<std::uint8_t>(((pixel >> 5) & 0x3F) * 255 / 63);
                const auto blue = static_cast<std::uint8_t>((pixel & 0x1F) * 255 / 31);
                auto* out = &tga[18 + static_cast<std::size_t>(y * kIconSize + x) * 4];
                out[0] = blue;
                out[1] = green;
                out[2] = red;
                out[3] = 255;
            }
        }
    }
    return tga;
}

}

std::optional<Smdh> parseSmdh(ByteView data, int language) {
    if (data.size() < kSmdhSize || data[0] != 'S' || data[1] != 'M' || data[2] != 'D' || data[3] != 'H') return std::nullopt;
    const ByteBuffer buffer(Bytes(data.begin(), data.end()));

    auto titleAt = [&](std::size_t index) {
        const std::size_t base = kTitlesOffset + index * kTitleSize;
        return std::pair{readTitleText(buffer, base, 0x40), readTitleText(buffer, base + 0x180, 0x40)};
    };
    std::pair<std::string, std::string> chosen;
    for (const std::size_t candidate : {static_cast<std::size_t>(language), std::size_t{1}}) {
        if (candidate >= kTitleCount) continue;
        chosen = titleAt(candidate);
        if (!chosen.first.empty()) break;
    }
    for (std::size_t i = 0; i < kTitleCount && chosen.first.empty(); ++i) chosen = titleAt(i);

    return Smdh{std::move(chosen.first), std::move(chosen.second), iconToTga(buffer)};
}

}

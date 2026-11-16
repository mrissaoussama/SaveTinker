#include "savetinker/smdh.hpp"

#include <algorithm>
#include <array>
#include <vector>

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

std::optional<Smdh> parseDsBanner(ByteView data, int language) {
    if (data.size() < kDsBannerMinSize) return std::nullopt;
    const ByteBuffer buffer(Bytes(data.begin(), data.end()));
    constexpr std::size_t kIconOffset = 0x20, kPaletteOffset = 0x220, kTitles = 0x240, kTitleBytes = 0x100;
    constexpr int kDsIcon = 32;

    auto lines = [&](std::size_t index) {
        std::vector<std::string> result;
        std::u16string line;
        for (std::size_t i = 0; i < kTitleBytes / 2; ++i) {
            const auto unit = buffer.read<std::uint16_t>(kTitles + index * kTitleBytes + i * 2);
            if (unit == 0) break;
            if (unit == u'\n') {
                result.push_back(utf16ToUtf8(line));
                line.clear();
            } else {
                line.push_back(static_cast<char16_t>(unit));
            }
        }
        if (!line.empty()) result.push_back(utf16ToUtf8(line));
        return result;
    };
    // Six languages exist in every banner (Japanese, English, French, German, Italian, Spanish), in the 3DS order.
    std::vector<std::string> chosen;
    for (const int candidate : {language, 1, 0})
        if (candidate >= 0 && candidate < 6 && (chosen = lines(static_cast<std::size_t>(candidate)), !chosen.empty())) break;
    if (chosen.empty()) return std::nullopt;

    std::array<std::array<std::uint8_t, 4>, 16> palette{};
    for (std::size_t i = 0; i < palette.size(); ++i) {
        const auto colour = buffer.read<std::uint16_t>(kPaletteOffset + i * 2);
        palette[i] = {static_cast<std::uint8_t>(((colour >> 10) & 0x1F) * 255 / 31), static_cast<std::uint8_t>(((colour >> 5) & 0x1F) * 255 / 31),
                      static_cast<std::uint8_t>((colour & 0x1F) * 255 / 31), static_cast<std::uint8_t>(i == 0 ? 0 : 255)};
    }
    Bytes tga(18 + kDsIcon * kDsIcon * 4, 0);
    tga[2] = 2;
    tga[12] = kDsIcon;
    tga[14] = kDsIcon;
    tga[16] = 32;
    tga[17] = 0x28;
    // 4x4 tiles of 8x8 pixels, two pixels per byte with the left one in the low nibble.
    for (int tile = 0; tile < 16; ++tile)
        for (int pixel = 0; pixel < 64; ++pixel) {
            const auto byte = data[kIconOffset + static_cast<std::size_t>(tile * 32 + pixel / 2)];
            const auto index = pixel % 2 ? byte >> 4 : byte & 0xF;
            const int x = (tile % 4) * 8 + pixel % 8, y = (tile / 4) * 8 + pixel / 8;
            auto* out = &tga[18 + static_cast<std::size_t>(y * kDsIcon + x) * 4];
            std::copy(palette[index].begin(), palette[index].end(), out);
        }

    Smdh banner;
    banner.shortTitle = chosen.front();
    if (chosen.size() > 1) banner.publisher = chosen.back();
    banner.iconTga = std::move(tga);
    return banner;
}

}

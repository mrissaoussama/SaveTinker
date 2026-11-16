#include <doctest.h>

#include "savetinker/smdh.hpp"

namespace {

void writeTitle(st::ByteBuffer& buffer, std::size_t index, std::string_view title, std::string_view publisher) {
    const std::size_t base = 0x8 + index * 0x200;
    buffer.writeUtf16(base, std::string(title), 0x40);
    buffer.writeUtf16(base + 0x180, std::string(publisher), 0x40);
}

st::ByteBuffer blankSmdh() {
    st::ByteBuffer buffer(st::Bytes(st::kSmdhSize));
    buffer.writeBytes(0, st::ByteView(reinterpret_cast<const std::uint8_t*>("SMDH"), 4));
    return buffer;
}

}

TEST_CASE("SMDH titles follow the system language with English fallback") {
    auto buffer = blankSmdh();
    writeTitle(buffer, 1, "Puzzle Garden", "Example Studio");
    writeTitle(buffer, 2, "Jardin Puzzle", "Studio Exemple");

    auto french = st::parseSmdh(buffer.view(), 2);
    REQUIRE(french.has_value());
    CHECK(french->shortTitle == "Jardin Puzzle");
    CHECK(french->publisher == "Studio Exemple");

    auto german = st::parseSmdh(buffer.view(), 3);
    REQUIRE(german.has_value());
    CHECK(german->shortTitle == "Puzzle Garden");
}

TEST_CASE("SMDH icon is untiled into a TGA image") {
    auto buffer = blankSmdh();
    writeTitle(buffer, 0, "Only Japanese", "");
    // First pixel of the first tile is (0, 0); the second is (1, 0); the third is (0, 1).
    buffer.write<std::uint16_t>(0x24C0, 0xF800);
    buffer.write<std::uint16_t>(0x24C2, 0x07E0);
    buffer.write<std::uint16_t>(0x24C4, 0x001F);

    auto smdh = st::parseSmdh(buffer.view(), 1);
    REQUIRE(smdh.has_value());
    CHECK(smdh->shortTitle == "Only Japanese");
    const auto& tga = smdh->iconTga;
    REQUIRE(tga.size() == 18 + 48 * 48 * 4);
    auto pixel = [&](int x, int y) { return &tga[18 + static_cast<std::size_t>(y * 48 + x) * 4]; };
    CHECK(pixel(0, 0)[2] == 255);
    CHECK(pixel(1, 0)[1] == 255);
    CHECK(pixel(0, 1)[0] == 255);
    CHECK(pixel(0, 1)[3] == 255);
}

TEST_CASE("data without the SMDH magic is rejected") {
    CHECK_FALSE(st::parseSmdh(st::Bytes(st::kSmdhSize), 1).has_value());
    CHECK_FALSE(st::parseSmdh(st::Bytes(16), 1).has_value());
}

TEST_CASE("DS banners give the title, publisher and a 32x32 icon") {
    st::ByteBuffer buffer(st::Bytes(st::kDsBannerMinSize));
    buffer.writeUtf16(0x240 + 0x100, "Flipnote\nNintendo", 0x80);
    buffer.write<std::uint16_t>(0x220 + 2, 0x001F);  // palette entry 1: red
    buffer.write<std::uint8_t>(0x20, 0x10);          // first pixel transparent, second red
    auto banner = st::parseDsBanner(buffer.view(), 2);
    REQUIRE(banner.has_value());
    CHECK(banner->shortTitle == "Flipnote");
    CHECK(banner->publisher == "Nintendo");
    REQUIRE(banner->iconTga.size() == 18 + 32 * 32 * 4);
    CHECK(banner->iconTga[18 + 3] == 0);
    CHECK(banner->iconTga[18 + 4 + 2] == 255);
    CHECK(banner->iconTga[18 + 4 + 3] == 255);
    CHECK_FALSE(st::parseDsBanner(st::Bytes(0x10), 1).has_value());
}

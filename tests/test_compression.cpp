#include <doctest.h>

#include "savetinker/compression.hpp"
#include "savetinker/lzstring.hpp"
#include "savetinker/formats.hpp"
#include "test_support.hpp"

using st::DeflateFormat;

TEST_CASE("deflate formats round trip and reject damage") {
    std::string text;
    for (int i = 0; i < 200; ++i) text += "save data " + std::to_string(i % 7);
    const auto data = test::bytesOf(text);
    for (auto format : {DeflateFormat::Raw, DeflateFormat::Zlib, DeflateFormat::Gzip}) {
        auto packed = st::deflateCompress(data, format);
        REQUIRE(packed.has_value());
        CHECK(packed->size() < data.size());
        CHECK(*st::deflateDecompress(*packed, format, 1 << 20) == data);
        CHECK_FALSE(st::deflateDecompress(*packed, format, 10).has_value());
        CHECK(st::deflateDecompress(*st::deflateCompress({}, format), format, 10)->empty());
    }
    auto gzip = *st::deflateCompress(data, DeflateFormat::Gzip);
    CHECK(gzip[0] == 0x1F);
    gzip[gzip.size() - 6] ^= 1;
    CHECK(st::deflateDecompress(gzip, DeflateFormat::Gzip, 1 << 20).error().find("checksum") != std::string::npos);
    CHECK_FALSE(st::deflateDecompress(test::bytesOf("not compressed"), DeflateFormat::Zlib, 1 << 20).has_value());
}

TEST_CASE("zlib output reads as a standard stream") {
    const auto packed = *st::deflateCompress(test::bytesOf("hello"), DeflateFormat::Zlib);
    CHECK(packed[0] == 0x78);
    CHECK(st::parseDeflateFormat("gzip") == DeflateFormat::Gzip);
    CHECK_FALSE(st::parseDeflateFormat("lzma").has_value());
}

TEST_CASE("zstd compression round trips") {
    const auto data = test::bytesOf(std::string(5000, 'x') + "end");
    const auto packed = st::zstdCompress(data, 19);
    REQUIRE(packed.has_value());
    CHECK(packed->size() < 100);
    CHECK(*st::zstdDecompress(*packed, {}, 1 << 20) == data);
    CHECK_FALSE(st::zstdCompress(data, 99).has_value());
}

TEST_CASE("lz-string base64 matches the JavaScript library") {
    const std::pair<std::string, std::string> cases[] = {
        {"Hello, world", "BIUwNmD2A0AEDukBOYAmQ==="},
        {"{\"gold\":250,\"name\":\"R\xC3\xA9 \xE6\x97\xA5\xE6\x9C\xAC\"}", "N4Ig5g9gNgJiBcAmArABgDQgHYEMC2ApgiAEoCXABIKemgNOYgC+QA=="},
        {std::string(38, 'a'), "IY18ZUkA"},
        {"\xF0\x9F\x98\x80x", "rwbgA9geQ==="},
    };
    for (const auto& [text, encoded] : cases) {
        CHECK(st::lzCompressToBase64(text) == encoded);
        CHECK(st::lzDecompressFromBase64(encoded).value() == text);
    }
    CHECK(st::lzDecompressFromBase64("").value().empty());
    CHECK_NOTHROW((void)st::lzDecompressFromBase64("////////"));
    CHECK_NOTHROW((void)st::lzDecompressFromBase64("BIUwNmD2"));
}

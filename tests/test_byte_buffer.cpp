#include <doctest.h>

#include "savetinker/byte_buffer.hpp"

using st::ByteBuffer;
using st::Bytes;
using st::Endian;

TEST_CASE("reads integers in both byte orders") {
    ByteBuffer buffer(Bytes{0x01, 0x02, 0x03, 0x04});
    CHECK(buffer.read<std::uint32_t>(0) == 0x04030201u);
    CHECK(buffer.read<std::uint32_t>(0, Endian::Big) == 0x01020304u);
    CHECK(buffer.read<std::uint16_t>(2) == 0x0403);
    CHECK(buffer.read<std::int8_t>(0) == 1);
}

TEST_CASE("writes integers and floats") {
    ByteBuffer buffer(Bytes(8));
    buffer.write<std::uint16_t>(0, 0xBEEF, Endian::Big);
    CHECK(buffer.bytes()[0] == 0xBE);
    CHECK(buffer.bytes()[1] == 0xEF);
    buffer.writeFloat(4, 1.5f);
    CHECK(buffer.readFloat(4) == 1.5f);
    buffer.writeDouble(0, -2.25, Endian::Big);
    CHECK(buffer.readDouble(0, Endian::Big) == -2.25);
}

TEST_CASE("rejects out of range access") {
    ByteBuffer buffer(Bytes(4));
    CHECK_THROWS_AS(buffer.read<std::uint32_t>(1), std::out_of_range);
    CHECK_THROWS_AS(buffer.write<std::uint8_t>(4, 1), std::out_of_range);
    CHECK_THROWS_AS(buffer.readBytes(2, 3), std::out_of_range);
    CHECK_THROWS_AS(buffer.read<std::uint8_t>(SIZE_MAX), std::out_of_range);
}

TEST_CASE("reads and writes bits") {
    ByteBuffer buffer(Bytes{0b0000'0100});
    CHECK(buffer.readBit(0, 2));
    CHECK_FALSE(buffer.readBit(0, 3));
    buffer.writeBit(0, 7, true);
    buffer.writeBit(0, 2, false);
    CHECK(buffer.bytes()[0] == 0b1000'0000);
    CHECK_THROWS(buffer.readBit(0, 8));
}

TEST_CASE("utf8 fields stop at NUL and pad on write") {
    ByteBuffer buffer(Bytes{'h', 'i', 0, 'x', 'y'});
    CHECK(buffer.readUtf8(0, 5) == "hi");
    buffer.writeUtf8(0, "abc", 5);
    CHECK(buffer.bytes() == Bytes{'a', 'b', 'c', 0, 0});
    CHECK_THROWS_AS(buffer.writeUtf8(0, "toolong", 5), std::length_error);
}

TEST_CASE("utf16 fields round trip including surrogate pairs") {
    ByteBuffer buffer(Bytes(20));
    const std::string text = "Zelda \xF0\x9F\x97\xA1";
    buffer.writeUtf16(0, text, 10);
    CHECK(buffer.readUtf16(0, 10) == text);
    buffer.writeUtf16(0, "AB", 10, Endian::Big);
    CHECK(buffer.bytes()[0] == 0);
    CHECK(buffer.bytes()[1] == 'A');
    CHECK(buffer.readUtf16(0, 10, Endian::Big) == "AB");
}

TEST_CASE("utf8 to utf16 rejects malformed input") {
    CHECK_THROWS(st::utf8ToUtf16("\xC3"));
    CHECK_THROWS(st::utf8ToUtf16("\xFF"));
    CHECK(st::utf8ToUtf16("\xC3\xA9") == u"é");
}

TEST_CASE("finds byte patterns") {
    ByteBuffer buffer(Bytes{1, 2, 3, 1, 2, 3});
    const Bytes pattern{2, 3};
    CHECK(buffer.find(pattern) == 1u);
    CHECK(buffer.find(pattern, 2) == 4u);
    CHECK_FALSE(buffer.find(pattern, 5).has_value());
}

#include <doctest.h>

#include "savetinker/checksum.hpp"
#include "test_support.hpp"

using test::viewOf;

TEST_CASE("crc32 matches zlib") {
    CHECK(st::crc32(viewOf("123456789")) == 0xCBF43926u);
    CHECK(st::crc32(viewOf("")) == 0u);
    const auto first = st::crc32(viewOf("12345"));
    CHECK(st::crc32(viewOf("6789"), first) == 0xCBF43926u);
}

TEST_CASE("crc16 ccitt-false and adler32") {
    CHECK(st::crc16Ccitt(viewOf("123456789")) == 0x29B1);
    CHECK(st::adler32(viewOf("Wikipedia")) == 0x11E60398u);
}

TEST_CASE("md5 test vectors") {
    CHECK(st::toHex(st::md5(viewOf(""))) == "d41d8cd98f00b204e9800998ecf8427e");
    CHECK(st::toHex(st::md5(viewOf("The quick brown fox jumps over the lazy dog"))) == "9e107d9d372bb6826bd81d3542a419d6");
}

TEST_CASE("sha1 test vectors") {
    CHECK(st::toHex(st::sha1(viewOf("abc"))) == "a9993e364706816aba3e25717850c26c9cd0d89d");
    CHECK(st::toHex(st::sha1(viewOf(""))) == "da39a3ee5e6b4b0d3255bfef95601890afd80709");
}

TEST_CASE("sha256 test vectors including multi-block input") {
    CHECK(st::toHex(st::sha256(viewOf("abc"))) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(st::toHex(st::sha256(viewOf("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"))) ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    const std::string million(1'000'000, 'a');
    CHECK(st::toHex(st::sha256(viewOf(million))) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("hex round trip") {
    CHECK(st::fromHex("DE ad be EF") == st::Bytes{0xDE, 0xAD, 0xBE, 0xEF});
    CHECK(st::toHex(st::fromHex("00ff10")) == "00ff10");
    CHECK_THROWS(st::fromHex("abc"));
    CHECK_THROWS(st::fromHex("zz"));
}

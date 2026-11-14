#include <doctest.h>

#include "savetinker/checksum.hpp"
#include "savetinker/crypto.hpp"
#include "test_support.hpp"

using st::AesMode;
using st::fromHex;
using st::toHex;

TEST_CASE("AES block encryption matches FIPS-197") {
    const auto plain = fromHex("00112233445566778899aabbccddeeff");
    const auto key128 = fromHex("000102030405060708090a0b0c0d0e0f");
    const auto key192 = fromHex("000102030405060708090a0b0c0d0e0f1011121314151617");
    const auto key256 = fromHex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
    CHECK(toHex(*st::aesEncrypt(AesMode::Ecb, key128, plain)) == "69c4e0d86a7b0430d8cdb78070b4c55a");
    CHECK(toHex(*st::aesEncrypt(AesMode::Ecb, key192, plain)) == "dda97ca4864cdfe06eaf70a0ec0d7191");
    CHECK(toHex(*st::aesEncrypt(AesMode::Ecb, key256, plain)) == "8ea2b7ca516745bfeafc49904b496089");
    CHECK(*st::aesDecrypt(AesMode::Ecb, key256, *st::aesEncrypt(AesMode::Ecb, key256, plain)) == plain);
}

TEST_CASE("AES CBC and CTR match SP 800-38A") {
    const auto key = fromHex("2b7e151628aed2a6abf7158809cf4f3c");
    const auto plain = fromHex("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51");
    const auto cbc = *st::aesEncrypt(AesMode::Cbc, key, plain, fromHex("000102030405060708090a0b0c0d0e0f"));
    CHECK(toHex(cbc) == "7649abac8119b246cee98e9b12e9197d5086cb9b507219ee95db113a917678b2");
    CHECK(*st::aesDecrypt(AesMode::Cbc, key, cbc, fromHex("000102030405060708090a0b0c0d0e0f")) == plain);

    const auto counter = fromHex("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff");
    const auto ctr = *st::aesEncrypt(AesMode::Ctr, key, plain, counter);
    CHECK(toHex(ctr) == "874d6191b620e3261bef6864990db6ce9806f66b7970fdff8617187bb9fffdff");
    const st::Bytes partial(plain.begin(), plain.begin() + 20);
    CHECK(toHex(*st::aesEncrypt(AesMode::Ctr, key, partial, counter)) == toHex(st::ByteView(ctr).first(20)));
}

TEST_CASE("AES rejects bad sizes") {
    const auto key = fromHex("2b7e151628aed2a6abf7158809cf4f3c");
    CHECK_FALSE(st::aesEncrypt(AesMode::Ecb, key, st::Bytes(15)).has_value());
    CHECK_FALSE(st::aesEncrypt(AesMode::Cbc, key, st::Bytes(16)).has_value());
    CHECK_FALSE(st::aesEncrypt(AesMode::Ecb, st::Bytes(10), st::Bytes(16)).has_value());
    CHECK_FALSE(st::parseAesMode("gcm").has_value());
}

TEST_CASE("AES-CMAC matches RFC 4493") {
    const auto key = fromHex("2b7e151628aed2a6abf7158809cf4f3c");
    CHECK(toHex(*st::aesCmac(key, {})) == "bb1d6929e95937287fa37d129b756746");
    CHECK(toHex(*st::aesCmac(key, fromHex("6bc1bee22e409f96e93d7e117393172a"))) == "070a16b46b4d4144f79bdd9dd04a287c");
    const auto forty = fromHex("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e5130c81c46a35ce411");
    CHECK(toHex(*st::aesCmac(key, forty)) == "dfa66747de9ae63030ca32611497c827");
}

TEST_CASE("HMAC-SHA256 matches RFC 4231 and xor repeats the key") {
    CHECK(toHex(st::hmacSha256(st::Bytes(20, 0x0b), test::viewOf("Hi There"))) ==
          "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
    CHECK(toHex(st::hmacSha256(test::viewOf("Jefe"), test::viewOf("what do ya want for nothing?"))) ==
          "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
    CHECK(st::xorWith(st::Bytes{1, 2, 3, 4}, st::Bytes{0xFF, 0x0F}) == st::Bytes{0xFE, 0x0D, 0xFC, 0x0B});
}

TEST_CASE("PBKDF2 matches RFC 6070 and RFC 7914") {
    CHECK(toHex(*st::pbkdf2(st::HashKind::Sha1, test::viewOf("password"), test::viewOf("salt"), 1, 20)) ==
          "0c60c80f961f0e71f3a9b524af6012062fe037a6");
    CHECK(toHex(*st::pbkdf2(st::HashKind::Sha1, test::viewOf("password"), test::viewOf("salt"), 4096, 20)) ==
          "4b007901b765489abead49d926f721d065a429c1");
    CHECK(toHex(*st::pbkdf2(st::HashKind::Sha256, test::viewOf("passwd"), test::viewOf("salt"), 1, 64)) ==
          "55ac046e56e3089fec1691c22544b605f94185216dde0465e68b9d57c20dacbc49ca9cccf179b645991664b39d77ef317c71b845b1e30bd509112041d3a19783");
    CHECK_FALSE(st::pbkdf2(st::HashKind::Sha1, {}, {}, 0, 16).has_value());
}

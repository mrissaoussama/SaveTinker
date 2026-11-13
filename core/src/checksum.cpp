#include "savetinker/checksum.hpp"

#include <bit>
#include <stdexcept>

namespace st {

namespace {

constexpr std::array<std::uint32_t, 256> makeCrc32Table() {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t value = i;
        for (int bit = 0; bit < 8; ++bit) value = (value & 1) ? (value >> 1) ^ 0xEDB88320u : value >> 1;
        table[i] = value;
    }
    return table;
}

constexpr auto crc32Table = makeCrc32Table();

// Pads per MD5/SHA rules; the bit length is appended in the given byte order.
Bytes padMessage(ByteView data, bool lengthBigEndian) {
    Bytes padded(data.begin(), data.end());
    const std::uint64_t bitLength = static_cast<std::uint64_t>(data.size()) * 8;
    padded.push_back(0x80);
    while (padded.size() % 64 != 56) padded.push_back(0);
    for (int i = 0; i < 8; ++i) {
        const int shift = lengthBigEndian ? (7 - i) * 8 : i * 8;
        padded.push_back(static_cast<std::uint8_t>(bitLength >> shift));
    }
    return padded;
}

std::uint32_t loadBigEndian32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

std::uint32_t loadLittleEndian32(const std::uint8_t* p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}

template <std::size_t N>
void storeWords(std::array<std::uint8_t, N>& out, const std::uint32_t* words, bool bigEndian) {
    for (std::size_t i = 0; i < N / 4; ++i) {
        for (int b = 0; b < 4; ++b) {
            const int shift = bigEndian ? (3 - b) * 8 : b * 8;
            out[i * 4 + b] = static_cast<std::uint8_t>(words[i] >> shift);
        }
    }
}

}

std::uint32_t crc32(ByteView data, std::uint32_t seed) {
    std::uint32_t crc = ~seed;
    for (auto byte : data) crc = crc32Table[(crc ^ byte) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

std::uint16_t crc16Ccitt(ByteView data, std::uint16_t initial) {
    std::uint16_t crc = initial;
    for (auto byte : data) {
        crc ^= static_cast<std::uint16_t>(byte << 8);
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 0x8000) ? static_cast<std::uint16_t>((crc << 1) ^ 0x1021) : static_cast<std::uint16_t>(crc << 1);
    }
    return crc;
}

std::uint32_t adler32(ByteView data) {
    constexpr std::uint32_t modulus = 65521;
    std::uint32_t a = 1, b = 0;
    for (auto byte : data) {
        a = (a + byte) % modulus;
        b = (b + a) % modulus;
    }
    return (b << 16) | a;
}

std::array<std::uint8_t, 16> md5(ByteView data) {
    static constexpr std::uint32_t shifts[64] = {
        7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
        5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
        4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
        6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
    static constexpr std::uint32_t constants[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};

    std::uint32_t state[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
    const Bytes padded = padMessage(data, false);
    for (std::size_t chunk = 0; chunk < padded.size(); chunk += 64) {
        std::uint32_t words[16];
        for (int i = 0; i < 16; ++i) words[i] = loadLittleEndian32(&padded[chunk + i * 4]);
        std::uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
        for (std::uint32_t i = 0; i < 64; ++i) {
            std::uint32_t f, g;
            if (i < 16) { f = (b & c) | (~b & d); g = i; }
            else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) % 16; }
            else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) % 16; }
            else { f = c ^ (b | ~d); g = (7 * i) % 16; }
            const std::uint32_t rotated = std::rotl(a + f + constants[i] + words[g], static_cast<int>(shifts[i]));
            a = d; d = c; c = b; b = b + rotated;
        }
        state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    }
    std::array<std::uint8_t, 16> digest{};
    storeWords(digest, state, false);
    return digest;
}

std::array<std::uint8_t, 20> sha1(ByteView data) {
    std::uint32_t state[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    const Bytes padded = padMessage(data, true);
    for (std::size_t chunk = 0; chunk < padded.size(); chunk += 64) {
        std::uint32_t w[80];
        for (int i = 0; i < 16; ++i) w[i] = loadBigEndian32(&padded[chunk + i * 4]);
        for (int i = 16; i < 80; ++i) w[i] = std::rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        std::uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
        for (int i = 0; i < 80; ++i) {
            std::uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6; }
            const std::uint32_t temp = std::rotl(a, 5) + f + e + k + w[i];
            e = d; d = c; c = std::rotl(b, 30); b = a; a = temp;
        }
        state[0] += a; state[1] += b; state[2] += c; state[3] += d; state[4] += e;
    }
    std::array<std::uint8_t, 20> digest{};
    storeWords(digest, state, true);
    return digest;
}

std::array<std::uint8_t, 32> sha256(ByteView data) {
    static constexpr std::uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

    std::uint32_t state[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                              0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    const Bytes padded = padMessage(data, true);
    for (std::size_t chunk = 0; chunk < padded.size(); chunk += 64) {
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i) w[i] = loadBigEndian32(&padded[chunk + i * 4]);
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = std::rotr(w[i - 15], 7) ^ std::rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = std::rotr(w[i - 2], 17) ^ std::rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
        std::uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t s1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
            const std::uint32_t choose = (e & f) ^ (~e & g);
            const std::uint32_t temp1 = h + s1 + choose + k[i] + w[i];
            const std::uint32_t s0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
            const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temp2 = s0 + majority;
            h = g; g = f; f = e; e = d + temp1; d = c; c = b; b = a; a = temp1 + temp2;
        }
        state[0] += a; state[1] += b; state[2] += c; state[3] += d;
        state[4] += e; state[5] += f; state[6] += g; state[7] += h;
    }
    std::array<std::uint8_t, 32> digest{};
    storeWords(digest, state, true);
    return digest;
}

std::string toHex(ByteView data) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(data.size() * 2);
    for (auto byte : data) {
        out.push_back(digits[byte >> 4]);
        out.push_back(digits[byte & 0xF]);
    }
    return out;
}

Bytes fromHex(std::string_view hex) {
    auto nibble = [](char c) -> std::uint8_t {
        if (c >= '0' && c <= '9') return static_cast<std::uint8_t>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<std::uint8_t>(c - 'a' + 10);
        if (c >= 'A' && c <= 'F') return static_cast<std::uint8_t>(c - 'A' + 10);
        throw std::invalid_argument("invalid hex digit");
    };
    Bytes out;
    std::uint8_t pending = 0;
    bool havePending = false;
    for (char c : hex) {
        if (c == ' ') continue;
        if (!havePending) { pending = nibble(c); havePending = true; }
        else { out.push_back(static_cast<std::uint8_t>((pending << 4) | nibble(c))); havePending = false; }
    }
    if (havePending) throw std::invalid_argument("odd number of hex digits");
    return out;
}

}

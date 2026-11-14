#include "savetinker/crypto.hpp"

#include <algorithm>
#include <vector>

#include "savetinker/checksum.hpp"

namespace st {

namespace {

using Block = std::array<std::uint8_t, 16>;

constexpr std::array<std::uint8_t, 256> kSbox = [] {
    std::array<std::uint8_t, 256> box{};
    std::uint8_t p = 1, q = 1;
    do {
        p = static_cast<std::uint8_t>(p ^ (p << 1) ^ (p & 0x80 ? 0x1B : 0));
        q ^= static_cast<std::uint8_t>(q << 1);
        q ^= static_cast<std::uint8_t>(q << 2);
        q ^= static_cast<std::uint8_t>(q << 4);
        if (q & 0x80) q ^= 0x09;
        auto rotl = [](std::uint8_t x, int s) { return static_cast<std::uint8_t>((x << s) | (x >> (8 - s))); };
        box[p] = static_cast<std::uint8_t>(q ^ rotl(q, 1) ^ rotl(q, 2) ^ rotl(q, 3) ^ rotl(q, 4) ^ 0x63);
    } while (p != 1);
    box[0] = 0x63;
    return box;
}();

constexpr std::array<std::uint8_t, 256> kInverseSbox = [] {
    std::array<std::uint8_t, 256> inverse{};
    for (int i = 0; i < 256; ++i) inverse[kSbox[i]] = static_cast<std::uint8_t>(i);
    return inverse;
}();

std::uint8_t xtime(std::uint8_t x) {
    return static_cast<std::uint8_t>((x << 1) ^ (x & 0x80 ? 0x1B : 0));
}

std::uint8_t multiply(std::uint8_t a, std::uint8_t b) {
    std::uint8_t result = 0;
    while (b) {
        if (b & 1) result ^= a;
        a = xtime(a);
        b >>= 1;
    }
    return result;
}

class Aes {
public:
    explicit Aes(ByteView key) : rounds_(static_cast<int>(key.size() / 4) + 6) {
        const std::size_t words = key.size() / 4;
        roundKeys_.assign(16 * (rounds_ + 1), 0);
        std::copy(key.begin(), key.end(), roundKeys_.begin());
        std::uint8_t rcon = 1;
        for (std::size_t i = words; i < 4 * static_cast<std::size_t>(rounds_ + 1); ++i) {
            std::array<std::uint8_t, 4> t;
            std::copy_n(roundKeys_.begin() + (i - 1) * 4, 4, t.begin());
            if (i % words == 0) {
                t = {static_cast<std::uint8_t>(kSbox[t[1]] ^ rcon), kSbox[t[2]], kSbox[t[3]], kSbox[t[0]]};
                rcon = xtime(rcon);
            } else if (words > 6 && i % words == 4) {
                for (auto& b : t) b = kSbox[b];
            }
            for (int j = 0; j < 4; ++j) roundKeys_[i * 4 + j] = roundKeys_[(i - words) * 4 + j] ^ t[j];
        }
    }

    Block encrypt(Block state) const {
        addRoundKey(state, 0);
        for (int round = 1; round <= rounds_; ++round) {
            for (auto& b : state) b = kSbox[b];
            shiftRows(state);
            if (round != rounds_) mixColumns(state);
            addRoundKey(state, round);
        }
        return state;
    }

    Block decrypt(Block state) const {
        addRoundKey(state, rounds_);
        for (int round = rounds_ - 1; round >= 0; --round) {
            inverseShiftRows(state);
            for (auto& b : state) b = kInverseSbox[b];
            addRoundKey(state, round);
            if (round != 0) inverseMixColumns(state);
        }
        return state;
    }

private:
    void addRoundKey(Block& state, int round) const {
        for (int i = 0; i < 16; ++i) state[i] ^= roundKeys_[round * 16 + i];
    }

    static void shiftRows(Block& s) {
        Block t = s;
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r) s[c * 4 + r] = t[((c + r) % 4) * 4 + r];
    }

    static void inverseShiftRows(Block& s) {
        Block t = s;
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r) s[((c + r) % 4) * 4 + r] = t[c * 4 + r];
    }

    static void mixColumns(Block& s) {
        for (int c = 0; c < 4; ++c) {
            std::uint8_t* col = &s[c * 4];
            const std::uint8_t a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
            const std::uint8_t all = a0 ^ a1 ^ a2 ^ a3;
            col[0] ^= all ^ xtime(a0 ^ a1);
            col[1] ^= all ^ xtime(a1 ^ a2);
            col[2] ^= all ^ xtime(a2 ^ a3);
            col[3] ^= all ^ xtime(a3 ^ a0);
        }
    }

    static void inverseMixColumns(Block& s) {
        for (int c = 0; c < 4; ++c) {
            std::uint8_t* col = &s[c * 4];
            const std::uint8_t a[4] = {col[0], col[1], col[2], col[3]};
            for (int r = 0; r < 4; ++r)
                col[r] = multiply(a[r], 14) ^ multiply(a[(r + 1) % 4], 11) ^ multiply(a[(r + 2) % 4], 13) ^ multiply(a[(r + 3) % 4], 9);
        }
    }

    int rounds_;
    std::vector<std::uint8_t> roundKeys_;
};

Block blockAt(ByteView data, std::size_t offset) {
    Block block{};
    std::copy_n(data.begin() + static_cast<std::ptrdiff_t>(offset), std::min<std::size_t>(16, data.size() - offset), block.begin());
    return block;
}

std::expected<void, std::string> check(AesMode mode, ByteView key, ByteView data, ByteView iv) {
    if (key.size() != 16 && key.size() != 24 && key.size() != 32) return std::unexpected("AES key must be 16, 24 or 32 bytes");
    if (mode != AesMode::Ctr && data.size() % 16 != 0) return std::unexpected("AES ECB and CBC data must be a multiple of 16 bytes");
    if (mode != AesMode::Ecb && iv.size() != 16) return std::unexpected("AES CBC and CTR need a 16-byte iv");
    return {};
}

Bytes ctr(const Aes& aes, ByteView data, ByteView iv) {
    Bytes out(data.begin(), data.end());
    Block counter = blockAt(iv, 0);
    for (std::size_t offset = 0; offset < out.size(); offset += 16) {
        const Block stream = aes.encrypt(counter);
        for (std::size_t i = 0; i < 16 && offset + i < out.size(); ++i) out[offset + i] ^= stream[i];
        for (int i = 15; i >= 0 && ++counter[i] == 0; --i) {
        }
    }
    return out;
}

}

std::expected<AesMode, std::string> parseAesMode(std::string_view name) {
    if (name == "ecb") return AesMode::Ecb;
    if (name == "cbc") return AesMode::Cbc;
    if (name == "ctr") return AesMode::Ctr;
    return std::unexpected("unknown AES mode \"" + std::string(name) + "\" (ecb, cbc or ctr)");
}

std::expected<Bytes, std::string> aesEncrypt(AesMode mode, ByteView key, ByteView data, ByteView iv) {
    if (auto valid = check(mode, key, data, iv); !valid) return std::unexpected(valid.error());
    const Aes aes(key);
    if (mode == AesMode::Ctr) return ctr(aes, data, iv);
    Bytes out(data.size());
    Block previous = mode == AesMode::Cbc ? blockAt(iv, 0) : Block{};
    for (std::size_t offset = 0; offset < data.size(); offset += 16) {
        Block block = blockAt(data, offset);
        if (mode == AesMode::Cbc)
            for (int i = 0; i < 16; ++i) block[i] ^= previous[i];
        previous = aes.encrypt(block);
        std::copy(previous.begin(), previous.end(), out.begin() + static_cast<std::ptrdiff_t>(offset));
    }
    return out;
}

std::expected<Bytes, std::string> aesDecrypt(AesMode mode, ByteView key, ByteView data, ByteView iv) {
    if (auto valid = check(mode, key, data, iv); !valid) return std::unexpected(valid.error());
    const Aes aes(key);
    if (mode == AesMode::Ctr) return ctr(aes, data, iv);
    Bytes out(data.size());
    Block previous = mode == AesMode::Cbc ? blockAt(iv, 0) : Block{};
    for (std::size_t offset = 0; offset < data.size(); offset += 16) {
        const Block cipher = blockAt(data, offset);
        Block plain = aes.decrypt(cipher);
        if (mode == AesMode::Cbc)
            for (int i = 0; i < 16; ++i) plain[i] ^= previous[i];
        previous = cipher;
        std::copy(plain.begin(), plain.end(), out.begin() + static_cast<std::ptrdiff_t>(offset));
    }
    return out;
}

std::expected<std::array<std::uint8_t, 16>, std::string> aesCmac(ByteView key, ByteView data) {
    if (key.size() != 16 && key.size() != 24 && key.size() != 32) return std::unexpected("AES key must be 16, 24 or 32 bytes");
    const Aes aes(key);
    auto doubled = [](Block block) {
        const bool carry = block[0] & 0x80;
        for (int i = 0; i < 15; ++i) block[i] = static_cast<std::uint8_t>((block[i] << 1) | (block[i + 1] >> 7));
        block[15] = static_cast<std::uint8_t>((block[15] << 1) ^ (carry ? 0x87 : 0));
        return block;
    };
    const Block k1 = doubled(aes.encrypt(Block{}));
    const Block k2 = doubled(k1);

    const std::size_t blocks = data.empty() ? 1 : (data.size() + 15) / 16;
    const bool complete = !data.empty() && data.size() % 16 == 0;
    Block state{};
    for (std::size_t n = 0; n < blocks; ++n) {
        Block block = blockAt(data, n * 16);
        if (n + 1 == blocks) {
            if (complete) {
                for (int i = 0; i < 16; ++i) block[i] ^= k1[i];
            } else {
                block[data.size() - n * 16] = 0x80;
                for (int i = 0; i < 16; ++i) block[i] ^= k2[i];
            }
        }
        for (int i = 0; i < 16; ++i) state[i] ^= block[i];
        state = aes.encrypt(state);
    }
    return state;
}

std::expected<HashKind, std::string> parseHashKind(std::string_view name) {
    if (name == "sha1") return HashKind::Sha1;
    if (name == "sha256") return HashKind::Sha256;
    return std::unexpected("unknown hash \"" + std::string(name) + "\" (sha1 or sha256)");
}

namespace {

Bytes digest(HashKind kind, ByteView data) {
    if (kind == HashKind::Sha1) {
        const auto value = sha1(data);
        return Bytes(value.begin(), value.end());
    }
    const auto value = sha256(data);
    return Bytes(value.begin(), value.end());
}

}

Bytes hmac(HashKind kind, ByteView key, ByteView data) {
    std::array<std::uint8_t, 64> block{};
    if (key.size() > block.size()) {
        const auto hashed = digest(kind, key);
        std::copy(hashed.begin(), hashed.end(), block.begin());
    } else {
        std::copy(key.begin(), key.end(), block.begin());
    }
    Bytes inner(64), outer(64);
    for (int i = 0; i < 64; ++i) {
        inner[i] = block[i] ^ 0x36;
        outer[i] = block[i] ^ 0x5C;
    }
    inner.insert(inner.end(), data.begin(), data.end());
    const auto innerDigest = digest(kind, inner);
    outer.insert(outer.end(), innerDigest.begin(), innerDigest.end());
    return digest(kind, outer);
}

std::array<std::uint8_t, 32> hmacSha256(ByteView key, ByteView data) {
    const auto value = hmac(HashKind::Sha256, key, data);
    std::array<std::uint8_t, 32> out{};
    std::copy(value.begin(), value.end(), out.begin());
    return out;
}

std::expected<Bytes, std::string> pbkdf2(HashKind kind, ByteView password, ByteView salt, std::uint32_t iterations, std::size_t length) {
    if (iterations == 0 || iterations > 1000000) return std::unexpected("iterations must be 1-1000000");
    if (length == 0 || length > 1024) return std::unexpected("length must be 1-1024");
    Bytes out;
    for (std::uint32_t block = 1; out.size() < length; ++block) {
        Bytes input(salt.begin(), salt.end());
        for (int shift = 24; shift >= 0; shift -= 8) input.push_back(static_cast<std::uint8_t>(block >> shift));
        Bytes u = hmac(kind, password, input);
        Bytes t = u;
        for (std::uint32_t i = 1; i < iterations; ++i) {
            u = hmac(kind, password, u);
            for (std::size_t j = 0; j < t.size(); ++j) t[j] ^= u[j];
        }
        out.insert(out.end(), t.begin(), t.end());
    }
    out.resize(length);
    return out;
}

Bytes xorWith(ByteView data, ByteView key) {
    Bytes out(data.begin(), data.end());
    if (key.empty()) return out;
    for (std::size_t i = 0; i < out.size(); ++i) out[i] ^= key[i % key.size()];
    return out;
}

}

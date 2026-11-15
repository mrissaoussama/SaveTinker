#include "savetinker/lzstring.hpp"

#include "savetinker/byte_buffer.hpp"

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace st {

namespace {

constexpr std::string_view kBase64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=";

void compress(const std::u16string& input, int bitsPerChar, std::string& out) {
    std::unordered_map<std::u16string, int> dictionary;
    std::unordered_map<std::u16string, bool> toCreate;
    std::u16string w;
    int enlargeIn = 2, dictSize = 3, numBits = 2;
    int value = 0, position = 0;

    auto writeBit = [&](int bit) {
        value = (value << 1) | bit;
        if (position == bitsPerChar - 1) {
            position = 0;
            out.push_back(kBase64[value]);
            value = 0;
        } else {
            ++position;
        }
    };
    auto writeBits = [&](int count, int bits) {
        for (int i = 0; i < count; ++i) {
            writeBit(bits & 1);
            bits >>= 1;
        }
    };
    auto enlarge = [&] {
        if (--enlargeIn == 0) {
            enlargeIn = 1 << numBits;
            ++numBits;
        }
    };
    auto emit = [&] {
        if (toCreate.contains(w)) {
            const int code = w[0];
            if (code < 256) {
                writeBits(numBits, 0);
                writeBits(8, code);
            } else {
                writeBits(numBits, 1);
                writeBits(16, code);
            }
            enlarge();
            toCreate.erase(w);
        } else {
            writeBits(numBits, dictionary[w]);
        }
        enlarge();
    };

    for (char16_t c : input) {
        const std::u16string single(1, c);
        if (!dictionary.contains(single)) {
            dictionary[single] = dictSize++;
            toCreate[single] = true;
        }
        std::u16string wc = w + c;
        if (dictionary.contains(wc)) {
            w = std::move(wc);
        } else {
            emit();
            dictionary[wc] = dictSize++;
            w = single;
        }
    }
    if (!w.empty()) emit();
    writeBits(numBits, 2);
    while (true) {
        value <<= 1;
        if (position == bitsPerChar - 1) {
            out.push_back(kBase64[value]);
            break;
        }
        ++position;
    }
}

}

std::string lzCompressToBase64(std::string_view utf8) {
    std::string out;
    compress(utf8ToUtf16(utf8), 6, out);
    while (out.size() % 4 != 0) out.push_back('=');
    return out;
}

std::expected<std::string, std::string> lzDecompressFromBase64(std::string_view input) {
    std::array<int, 256> lookup;
    lookup.fill(-1);
    for (std::size_t i = 0; i < kBase64.size(); ++i) lookup[static_cast<unsigned char>(kBase64[i])] = static_cast<int>(i);
    std::string clean;
    for (char c : input)
        if (lookup[static_cast<unsigned char>(c)] >= 0) clean.push_back(c);
    if (clean.empty()) return std::string();
    for (char c : clean)
        if (lookup[static_cast<unsigned char>(c)] < 0) return std::unexpected("not LZString base64");

    const auto length = clean.size();
    std::size_t index = 0;
    int value = lookup[static_cast<unsigned char>(clean[index++])];
    int position = 32;
    auto readBits = [&](int count) {
        int bits = 0;
        for (int power = 1, i = 0; i < count; ++i, power <<= 1) {
            const int bit = value & position;
            position >>= 1;
            if (position == 0) {
                position = 32;
                value = index < length ? lookup[static_cast<unsigned char>(clean[index])] : 0;
                ++index;
            }
            if (bit) bits |= power;
        }
        return bits;
    };

    std::vector<std::u16string> dictionary = {u"0", u"1", u"2"};
    int enlargeIn = 4, numBits = 3;
    std::u16string result;
    std::u16string w;
    switch (readBits(2)) {
    case 0: w = std::u16string(1, static_cast<char16_t>(readBits(8))); break;
    case 1: w = std::u16string(1, static_cast<char16_t>(readBits(16))); break;
    default: return std::string();
    }
    dictionary.push_back(w);
    result += w;
    constexpr std::size_t kMaxOutput = 64u << 20;
    while (true) {
        if (index > length) return std::unexpected("LZString data ends early");
        int code = readBits(numBits);
        if (code == 0 || code == 1) {
            dictionary.emplace_back(1, static_cast<char16_t>(readBits(code == 0 ? 8 : 16)));
            code = static_cast<int>(dictionary.size()) - 1;
            --enlargeIn;
        } else if (code == 2) {
            return utf16ToUtf8(result);
        }
        if (enlargeIn == 0) {
            enlargeIn = 1 << numBits;
            ++numBits;
        }
        std::u16string entry;
        if (static_cast<std::size_t>(code) < dictionary.size()) entry = dictionary[code];
        else if (static_cast<std::size_t>(code) == dictionary.size()) entry = w + w[0];
        else return std::unexpected("corrupt LZString data");
        result += entry;
        if (result.size() > kMaxOutput) return std::unexpected("LZString data too large");
        dictionary.push_back(w + entry[0]);
        --enlargeIn;
        w = std::move(entry);
        if (enlargeIn == 0) {
            enlargeIn = 1 << numBits;
            ++numBits;
        }
    }
}

}

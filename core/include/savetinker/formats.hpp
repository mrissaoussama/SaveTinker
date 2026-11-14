#pragma once

#include <cstdint>
#include <expected>
#include <map>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "savetinker/byte_buffer.hpp"

namespace st {


// zstd frames, optionally compressed with one of `dictionaries` (picked by the frame's dictionary id).
std::expected<Bytes, std::string> zstdDecompress(ByteView data, const std::vector<ByteView>& dictionaries,
                                                 std::uint64_t maxSize);

std::expected<Bytes, std::string> pixelsToTga(ByteView pixels, int width, int height, std::string_view format);

struct SarcEntry {
    std::string name;
    Bytes data;
};

std::expected<std::vector<SarcEntry>, std::string> readSarc(ByteView data);

struct BymlValue;
using BymlArray = std::vector<BymlValue>;
using BymlMap = std::map<std::string, BymlValue>;
using BymlHashMap = std::map<std::uint64_t, BymlValue>;

struct BymlValue {
    std::variant<std::monostate, bool, std::int64_t, std::uint64_t, double, std::string, Bytes,
                 std::shared_ptr<BymlArray>, std::shared_ptr<BymlMap>, std::shared_ptr<BymlHashMap>>
        value;
};

std::expected<BymlValue, std::string> readByml(ByteView data);

struct MsbtMessage {
    std::string label;
    // UTF-8 with the original control tags (0x0E group/type/size records) converted from UTF-16 if needed.
    std::string raw;
    std::string text;
};

std::expected<std::vector<MsbtMessage>, std::string> readMsbt(ByteView data);

}

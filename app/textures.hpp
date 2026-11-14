#pragma once

#include <cstdint>
#include <map>
#include <string>

#include "savetinker/byte_buffer.hpp"

namespace app {

struct Texture {
    std::uintptr_t id = 0;
    int width = 0;
    int height = 0;
    float uMax = 1.0f;
    float vMax = 1.0f;

    explicit operator bool() const { return id != 0; }
};

class Textures {
public:
    ~Textures();
    // Decodes and uploads an image once per key.
    Texture get(const std::string& key, st::ByteView encoded);
    void clear();

private:
    std::map<std::string, Texture> cache_;
};

}

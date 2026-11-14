#pragma once

#include <cstdint>

namespace app::gpu {

struct UploadedTexture {
    std::uintptr_t id = 0;
    // Part of the texture holding the image, below 1 when the GPU needed padding.
    float uMax = 1.0f;
    float vMax = 1.0f;
};

UploadedTexture upload(const std::uint8_t* rgba, int width, int height);
void destroy(std::uintptr_t id);

}

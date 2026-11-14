#include "textures.hpp"

#include "gpu.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_TGA
#define STBI_ONLY_BMP
#include <stb_image.h>

namespace app {

Textures::~Textures() {
    clear();
}

Texture Textures::get(const std::string& key, st::ByteView encoded) {
    if (auto found = cache_.find(key); found != cache_.end()) return found->second;
    Texture texture;
    if (!encoded.empty()) {
        int width = 0, height = 0, channels = 0;
        stbi_uc* pixels = stbi_load_from_memory(encoded.data(), static_cast<int>(encoded.size()), &width, &height, &channels, 4);
        if (pixels) {
            const auto uploaded = gpu::upload(pixels, width, height);
            stbi_image_free(pixels);
            texture = {uploaded.id, width, height, uploaded.uMax, uploaded.vMax};
        }
    }
    cache_[key] = texture;
    return texture;
}

void Textures::clear() {
    for (auto& [_, texture] : cache_)
        if (texture) gpu::destroy(texture.id);
    cache_.clear();
}

}

#include "textures.hpp"

#ifdef __SWITCH__
#include <GLES3/gl3.h>
#else
#include <SDL_opengl.h>
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
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
            GLuint id = 0;
            glGenTextures(1, &id);
            glBindTexture(GL_TEXTURE_2D, id);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            stbi_image_free(pixels);
            texture = {id, width, height};
        }
    }
    cache_[key] = texture;
    return texture;
}

void Textures::clear() {
    for (auto& [_, texture] : cache_) {
        if (texture) {
            const auto id = static_cast<GLuint>(texture.id);
            glDeleteTextures(1, &id);
        }
    }
    cache_.clear();
}

}

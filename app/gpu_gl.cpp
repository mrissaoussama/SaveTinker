#include "gpu.hpp"

#ifdef __SWITCH__
#include <GLES3/gl3.h>
#else
#include <SDL_opengl.h>
#endif

namespace app::gpu {

UploadedTexture upload(const std::uint8_t* rgba, int width, int height) {
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    return {id, 1.0f, 1.0f};
}

void destroy(std::uintptr_t id) {
    const auto texture = static_cast<GLuint>(id);
    glDeleteTextures(1, &texture);
}

}

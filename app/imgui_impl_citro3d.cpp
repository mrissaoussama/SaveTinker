#include "imgui_impl_citro3d.hpp"

#include <3ds.h>
#include <algorithm>
#include <citro3d.h>
#include <cstring>

#include "gpu.hpp"
#include "imgui_shader_shbin.h"

namespace {

constexpr std::size_t kMaxVertices = 65536;
constexpr std::size_t kMaxIndices = kMaxVertices * 3;

DVLB_s* shaderLibrary = nullptr;
shaderProgram_s shader;
int projectionLocation = -1;
ImDrawVert* vertices = nullptr;
ImDrawIdx* indices = nullptr;

int nextPowerOfTwo(int value) {
    int result = 8;
    while (result < value) result <<= 1;
    return result;
}

// RGBA8 rows into the GPU's 8x8 tiled layout, padded to power-of-two dimensions.
C3D_Tex* createTexture(const std::uint8_t* rgba, int width, int height) {
    const int textureWidth = nextPowerOfTwo(width);
    const int textureHeight = nextPowerOfTwo(height);
    auto* texture = new C3D_Tex;
    if (!C3D_TexInit(texture, static_cast<u16>(textureWidth), static_cast<u16>(textureHeight), GPU_RGBA8)) {
        delete texture;
        return nullptr;
    }
    C3D_TexSetFilter(texture, GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetWrap(texture, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    auto* out = static_cast<std::uint32_t*>(texture->data);
    std::memset(out, 0, texture->size);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::uint8_t* pixel = rgba + (static_cast<std::size_t>(y) * width + x) * 4;
            // The GPU's texture origin is the bottom-left corner.
            const int row = textureHeight - 1 - y;
            const std::uint32_t morton = (x & 1) | ((row & 1) << 1) | ((x & 2) << 1) | ((row & 2) << 2) |
                                         ((x & 4) << 2) | ((row & 4) << 3);
            const std::size_t tile = static_cast<std::size_t>((row >> 3) * (textureWidth >> 3) + (x >> 3));
            out[tile * 64 + morton] = (static_cast<std::uint32_t>(pixel[0]) << 24) | (static_cast<std::uint32_t>(pixel[1]) << 16) |
                                      (static_cast<std::uint32_t>(pixel[2]) << 8) | pixel[3];
        }
    }
    C3D_TexFlush(texture);
    return texture;
}

void destroyTexture(C3D_Tex* texture) {
    if (!texture) return;
    C3D_TexDelete(texture);
    delete texture;
}

// ImGui textures fill the whole GPU texture: ImGui sizes its atlas in powers of two.
void updateTexture(ImTextureData* data) {
    if (data->Status == ImTextureStatus_WantCreate || data->Status == ImTextureStatus_WantUpdates) {
        if (data->Status == ImTextureStatus_WantUpdates) destroyTexture(reinterpret_cast<C3D_Tex*>(data->GetTexID()));
        auto* texture = createTexture(static_cast<const std::uint8_t*>(data->GetPixels()), data->Width, data->Height);
        data->SetTexID(reinterpret_cast<ImTextureID>(texture));
        data->SetStatus(ImTextureStatus_OK);
    } else if (data->Status == ImTextureStatus_WantDestroy && data->UnusedFrames > 0) {
        destroyTexture(reinterpret_cast<C3D_Tex*>(data->GetTexID()));
        data->SetTexID(ImTextureID_Invalid);
        data->SetStatus(ImTextureStatus_Destroyed);
    }
}

void setupState(float screenWidth, float screenHeight) {
    C3D_BindProgram(&shader);
    C3D_AttrInfo* attributes = C3D_GetAttrInfo();
    AttrInfo_Init(attributes);
    AttrInfo_AddLoader(attributes, 0, GPU_FLOAT, 2);
    AttrInfo_AddLoader(attributes, 1, GPU_FLOAT, 2);
    AttrInfo_AddLoader(attributes, 2, GPU_UNSIGNED_BYTE, 4);

    C3D_Mtx projection;
    Mtx_OrthoTilt(&projection, 0.0f, screenWidth, screenHeight, 0.0f, 1.0f, -1.0f, true);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, projectionLocation, &projection);

    C3D_TexEnv* environment = C3D_GetTexEnv(0);
    C3D_TexEnvInit(environment);
    C3D_TexEnvSrc(environment, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
    C3D_TexEnvFunc(environment, C3D_Both, GPU_MODULATE);
    for (int stage = 1; stage < 6; ++stage) C3D_TexEnvInit(C3D_GetTexEnv(stage));

    C3D_CullFace(GPU_CULL_NONE);
    C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_ONE, GPU_ONE_MINUS_SRC_ALPHA);
}

}

namespace app::gpu {

UploadedTexture upload(const std::uint8_t* rgba, int width, int height) {
    auto* texture = createTexture(rgba, width, height);
    if (!texture) return {};
    return {reinterpret_cast<std::uintptr_t>(texture), static_cast<float>(width) / texture->width,
            static_cast<float>(height) / texture->height};
}

void destroy(std::uintptr_t id) {
    destroyTexture(reinterpret_cast<C3D_Tex*>(id));
}

}

bool ImGui_ImplCitro3D_Init() {
    auto& io = ImGui::GetIO();
    io.BackendRendererName = "imgui_impl_citro3d";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
    io.Fonts->TexMaxWidth = 1024;
    io.Fonts->TexMaxHeight = 1024;

    shaderLibrary = DVLB_ParseFile(reinterpret_cast<u32*>(const_cast<u8*>(imgui_shader_shbin)), imgui_shader_shbin_size);
    if (!shaderLibrary) return false;
    shaderProgramInit(&shader);
    shaderProgramSetVsh(&shader, &shaderLibrary->DVLE[0]);
    projectionLocation = shaderInstanceGetUniformLocation(shader.vertexShader, "projection");
    vertices = static_cast<ImDrawVert*>(linearAlloc(kMaxVertices * sizeof(ImDrawVert)));
    indices = static_cast<ImDrawIdx*>(linearAlloc(kMaxIndices * sizeof(ImDrawIdx)));
    return vertices && indices;
}

void ImGui_ImplCitro3D_Shutdown() {
    for (ImTextureData* data : ImGui::GetPlatformIO().Textures) {
        if (data->RefCount != 1) continue;
        destroyTexture(reinterpret_cast<C3D_Tex*>(data->GetTexID()));
        data->SetTexID(ImTextureID_Invalid);
        data->SetStatus(ImTextureStatus_Destroyed);
    }
    if (vertices) linearFree(vertices);
    if (indices) linearFree(indices);
    vertices = nullptr;
    indices = nullptr;
    shaderProgramFree(&shader);
    if (shaderLibrary) DVLB_Free(shaderLibrary);
    shaderLibrary = nullptr;
}

void ImGui_ImplCitro3D_RenderDrawData(ImDrawData* drawData, float screenWidth, float screenHeight) {
    if (drawData->Textures)
        for (ImTextureData* data : *drawData->Textures)
            if (data->Status != ImTextureStatus_OK) updateTexture(data);
    if (drawData->TotalVtxCount == 0) return;
    if (static_cast<std::size_t>(drawData->TotalVtxCount) > kMaxVertices ||
        static_cast<std::size_t>(drawData->TotalIdxCount) > kMaxIndices)
        return;

    setupState(screenWidth, screenHeight);

    std::size_t vertexOffset = 0;
    std::size_t indexOffset = 0;
    for (const ImDrawList* list : drawData->CmdLists) {
        std::memcpy(vertices + vertexOffset, list->VtxBuffer.Data, list->VtxBuffer.Size * sizeof(ImDrawVert));
        std::memcpy(indices + indexOffset, list->IdxBuffer.Data, list->IdxBuffer.Size * sizeof(ImDrawIdx));
        vertexOffset += list->VtxBuffer.Size;
        indexOffset += list->IdxBuffer.Size;
    }
    GSPGPU_FlushDataCache(vertices, vertexOffset * sizeof(ImDrawVert));
    GSPGPU_FlushDataCache(indices, indexOffset * sizeof(ImDrawIdx));

    const ImVec2 origin = drawData->DisplayPos;
    vertexOffset = 0;
    indexOffset = 0;
    for (const ImDrawList* list : drawData->CmdLists) {
        for (const ImDrawCmd& command : list->CmdBuffer) {
            if (command.UserCallback) {
                if (command.UserCallback == ImDrawCallback_ResetRenderState) setupState(screenWidth, screenHeight);
                else command.UserCallback(list, &command);
                continue;
            }
            const float left = std::max(command.ClipRect.x - origin.x, 0.0f);
            const float top = std::max(command.ClipRect.y - origin.y, 0.0f);
            const float right = std::min(command.ClipRect.z - origin.x, screenWidth);
            const float bottom = std::min(command.ClipRect.w - origin.y, screenHeight);
            if (right <= left || bottom <= top) continue;
            // The framebuffer is rotated: screen x runs along its height, screen y against its width.
            C3D_SetScissor(GPU_SCISSOR_NORMAL, static_cast<u32>(screenHeight - bottom), static_cast<u32>(screenWidth - right),
                           static_cast<u32>(screenHeight - top), static_cast<u32>(screenWidth - left));

            auto* texture = reinterpret_cast<C3D_Tex*>(command.GetTexID());
            if (texture) C3D_TexBind(0, texture);

            C3D_BufInfo* buffers = C3D_GetBufInfo();
            BufInfo_Init(buffers);
            BufInfo_Add(buffers, vertices + vertexOffset + command.VtxOffset, sizeof(ImDrawVert), 3, 0x210);
            C3D_DrawElements(GPU_TRIANGLES, static_cast<int>(command.ElemCount),
                             sizeof(ImDrawIdx) == 2 ? C3D_UNSIGNED_SHORT : C3D_UNSIGNED_BYTE,
                             indices + indexOffset + command.IdxOffset);
        }
        vertexOffset += list->VtxBuffer.Size;
        indexOffset += list->IdxBuffer.Size;
    }
    C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
}

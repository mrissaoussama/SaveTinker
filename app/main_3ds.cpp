#include <3ds.h>
#include <citro3d.h>
#include <imgui.h>

#include "app.hpp"
#include "imgui_impl_citro3d.hpp"
#include "system.hpp"

#ifdef SAVETINKER_AUTOPILOT
#include "autopilot.hpp"
#endif

namespace {

constexpr float kTopWidth = 400.0f;
constexpr float kScreenHeight = 240.0f;
constexpr u32 kBackground = 0x181A20FF;
constexpr u32 kDisplayTransferFlags =
    GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |
    GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) |
    GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);

#ifdef SAVETINKER_AUTOPILOT
// The render target is tiled RGBA8, 240 wide and 400 tall, rotated against the screen.
std::vector<unsigned char> readTarget(C3D_RenderTarget* target) {
    const int bufferWidth = target->frameBuf.width;
    const auto* buffer = static_cast<const u32*>(target->frameBuf.colorBuf);
    const int width = static_cast<int>(kTopWidth), height = static_cast<int>(kScreenHeight);
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int bx = bufferWidth - 1 - y;
            const int by = x;
            const u32 morton = (bx & 1) | ((by & 1) << 1) | ((bx & 2) << 1) | ((by & 2) << 2) | ((bx & 4) << 2) | ((by & 4) << 3);
            const u32 value = buffer[((by >> 3) * (bufferWidth >> 3) + (bx >> 3)) * 64 + morton];
            unsigned char* out = &pixels[(static_cast<std::size_t>(y) * width + x) * 4];
            out[0] = static_cast<unsigned char>(value >> 24);
            out[1] = static_cast<unsigned char>(value >> 16);
            out[2] = static_cast<unsigned char>(value >> 8);
            out[3] = 255;
        }
    }
    return pixels;
}
#endif

}

int main(int argc, char** argv) {
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C3D_RenderTarget* top = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    C3D_RenderTargetSetOutput(top, GFX_TOP, GFX_LEFT, kDisplayTransferFlags);
    C3D_RenderTarget* bottom = C3D_RenderTargetCreate(240, 320, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    C3D_RenderTargetSetOutput(bottom, GFX_BOTTOM, GFX_LEFT, kDisplayTransferFlags);

    auto system = app::createSystem(argc, argv);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.DisplaySize = ImVec2(kTopWidth, kScreenHeight);
    io.BackendPlatformName = "savetinker_3ds";
    const bool consoleGlyphs = system->loadFonts();
    ImGui_ImplCitro3D_Init();

    {
        app::App application(*system, consoleGlyphs);
#ifdef SAVETINKER_AUTOPILOT
        const auto dataRoot = application.platform().dataRoot();
        auto autopilot = app::Autopilot::load(dataRoot / "autopilot.txt", dataRoot / "screenshots");
        bool capturePrevious = false;
#endif
        u64 lastTick = osGetTime();
        while (application.running() && aptMainLoop()) {
            const u64 now = osGetTime();
            io.DeltaTime = std::max(0.001f, static_cast<float>(now - lastTick) / 1000.0f);
            lastTick = now;
            system->updateInput();
#ifdef SAVETINKER_AUTOPILOT
            if (autopilot && !autopilot->beforeFrame()) application.quit();
#endif
            ImGui::NewFrame();
            application.frame();
            ImGui::Render();

            C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
#ifdef SAVETINKER_AUTOPILOT
            // The previous frame has finished drawing once FrameBegin returns.
            if (capturePrevious) {
                autopilot->saveShot(readTarget(top), static_cast<int>(kTopWidth), static_cast<int>(kScreenHeight));
                capturePrevious = false;
            }
#endif
            C3D_RenderTargetClear(top, C3D_CLEAR_ALL, kBackground, 0);
            C3D_FrameDrawOn(top);
            ImGui_ImplCitro3D_RenderDrawData(ImGui::GetDrawData(), kTopWidth, kScreenHeight);
            C3D_RenderTargetClear(bottom, C3D_CLEAR_ALL, kBackground, 0);
            C3D_FrameEnd(0);
#ifdef SAVETINKER_AUTOPILOT
            if (autopilot && autopilot->wantsShot()) capturePrevious = true;
#endif
        }
    }

    ImGui_ImplCitro3D_Shutdown();
    ImGui::DestroyContext();
    C3D_RenderTargetDelete(bottom);
    C3D_RenderTargetDelete(top);
    C3D_Fini();
    system.reset();
    gfxExit();
    return 0;
}

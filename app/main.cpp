#include <SDL.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>

#ifdef __SWITCH__
#include <GLES3/gl3.h>
#else
#include <SDL_opengl.h>
#endif

#include "app.hpp"
#include "system.hpp"

#ifdef SAVETINKER_AUTOPILOT
#include "autopilot.hpp"
#endif

int main(int argc, char** argv) {
    auto system = app::createSystem(argc, argv);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_TIMER) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
#ifdef __SWITCH__
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    const char* glslVersion = "#version 300 es";
    const Uint32 windowFlags = SDL_WINDOW_OPENGL;
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    const char* glslVersion = "#version 150";
    const Uint32 windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    SDL_Window* window = SDL_CreateWindow("SaveTinker", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, windowFlags);
    if (!window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        return 1;
    }
    SDL_GLContext context = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, context);
    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    const bool consoleGlyphs = system->loadFonts();

    ImGui_ImplSDL2_InitForOpenGL(window, context);
    ImGui_ImplOpenGL3_Init(glslVersion);
#ifdef __SWITCH__
    ImGui_ImplSDL2_SetGamepadMode(ImGui_ImplSDL2_GamepadMode_Manual, nullptr, 0);
#endif

    {
        app::App application(*system, consoleGlyphs);
#ifdef SAVETINKER_AUTOPILOT
        const auto dataRoot = application.platform().dataRoot();
        auto autopilot = app::Autopilot::load(dataRoot / "autopilot.txt", dataRoot / "screenshots");
#ifndef __SWITCH__
        if (autopilot) SDL_SetWindowPosition(window, -4000, 100);
#endif
#endif
        while (application.running()) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                ImGui_ImplSDL2_ProcessEvent(&event);
                if (event.type == SDL_QUIT) application.quit();
            }
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplSDL2_NewFrame();
            // After the SDL backend, which clears the gamepad flag when SDL itself sees no controller.
            system->updateInput();
#ifdef SAVETINKER_AUTOPILOT
            if (autopilot && !autopilot->beforeFrame()) application.quit();
#endif
            ImGui::NewFrame();
            application.frame();
            ImGui::Render();

            glViewport(0, 0, static_cast<int>(io.DisplaySize.x * io.DisplayFramebufferScale.x),
                       static_cast<int>(io.DisplaySize.y * io.DisplayFramebufferScale.y));
            glClearColor(0.094f, 0.102f, 0.125f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#ifdef SAVETINKER_AUTOPILOT
            if (autopilot)
                autopilot->afterRender(static_cast<int>(io.DisplaySize.x * io.DisplayFramebufferScale.x),
                                       static_cast<int>(io.DisplaySize.y * io.DisplayFramebufferScale.y));
#endif
            SDL_GL_SwapWindow(window);
        }
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

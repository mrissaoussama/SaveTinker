#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace app {

// Test helper: replays button presses from a script and saves framebuffer screenshots.
// Script lines: "press A", "press DOWN 3", "wait 30", "shot name", "quit". Lines starting with # are ignored.
class Autopilot {
public:
    static std::optional<Autopilot> load(const std::filesystem::path& script, std::filesystem::path outputDir);

    // Call before ImGui::NewFrame. Returns false once the script asks to quit.
    bool beforeFrame();
    // Call after rendering, before swapping buffers. Reads the OpenGL framebuffer.
    void afterRender(int width, int height);
    // For renderers without glReadPixels: check, then hand over top-down RGBA pixels.
    bool wantsShot() const { return !pendingShot_.empty(); }
    void saveShot(const std::vector<unsigned char>& rgba, int width, int height);

private:
    struct Step {
        enum class Kind { Press, Type, Wait, Shot, Quit } kind;
        std::string argument;
        int count = 1;
    };

    std::vector<Step> steps_;
    std::filesystem::path outputDir_;
    std::size_t index_ = 0;
    int waitFrames_ = 0;
    int releaseKey_ = 0;
    std::string pendingShot_;
};

}

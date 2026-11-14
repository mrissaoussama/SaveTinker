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
    // Call after rendering, before swapping buffers.
    void afterRender(int width, int height);

private:
    struct Step {
        enum class Kind { Press, Wait, Shot, Quit } kind;
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

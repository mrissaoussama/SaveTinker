#include "autopilot.hpp"

#include <algorithm>
#include <fstream>
#include <imgui.h>
#include <map>
#include <sstream>
#include <vector>

#if defined(__3DS__)
#elif defined(__SWITCH__)
#include <GLES3/gl3.h>
#else
#include <SDL_opengl.h>
#endif

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace app {

namespace {

const std::map<std::string, ImGuiKey>& buttonKeys() {
    static const std::map<std::string, ImGuiKey> keys = {
        {"A", ImGuiKey_GamepadFaceDown}, {"B", ImGuiKey_GamepadFaceRight}, {"X", ImGuiKey_GamepadFaceUp},
        {"Y", ImGuiKey_GamepadFaceLeft}, {"L", ImGuiKey_GamepadL1},        {"R", ImGuiKey_GamepadR1},
        {"ZL", ImGuiKey_GamepadL2},      {"ZR", ImGuiKey_GamepadR2},       {"PLUS", ImGuiKey_GamepadStart},
        {"MINUS", ImGuiKey_GamepadBack}, {"UP", ImGuiKey_GamepadDpadUp},   {"DOWN", ImGuiKey_GamepadDpadDown},
        {"LEFT", ImGuiKey_GamepadDpadLeft}, {"RIGHT", ImGuiKey_GamepadDpadRight}, {"RSDOWN", ImGuiKey_GamepadRStickDown},
        {"ENTER", ImGuiKey_Enter},       {"BACKSPACE", ImGuiKey_Backspace},
    };
    return keys;
}

}

std::optional<Autopilot> Autopilot::load(const std::filesystem::path& script, std::filesystem::path outputDir) {
    std::ifstream stream(script);
    if (!stream) return std::nullopt;
    Autopilot autopilot;
    autopilot.outputDir_ = std::move(outputDir);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.front() == '#') continue;
        std::istringstream words(line);
        std::string command, argument;
        int count = 1;
        words >> command >> argument >> count;
        if (command == "press") autopilot.steps_.push_back({Step::Kind::Press, argument, std::max(1, count)});
        else if (command == "wait") autopilot.steps_.push_back({Step::Kind::Wait, {}, std::max(1, std::atoi(argument.c_str()))});
        else if (command == "shot") autopilot.steps_.push_back({Step::Kind::Shot, argument, 1});
        else if (command == "tap" || command == "drag") {
            std::istringstream numbers(line.substr(command.size()));
            float x = 0, y = 0, dx = 0, dy = 0;
            numbers >> x >> y >> dx >> dy;
            std::ostringstream path;
            path << x << ' ' << y << ' ' << dx << ' ' << dy;
            autopilot.steps_.push_back({Step::Kind::Touch, path.str(), 1});
        } else if (command == "type") autopilot.steps_.push_back({Step::Kind::Type, line.size() > 5 ? line.substr(5) : "", 1});
        else if (command == "quit") autopilot.steps_.push_back({Step::Kind::Quit, {}, 1});
    }
    return autopilot;
}

bool Autopilot::beforeFrame() {
    auto& io = ImGui::GetIO();
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    if (!touchPath_.empty()) {
        if (touchStep_ < touchPath_.size()) {
            io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
            io.AddMousePosEvent(touchPath_[touchStep_].first, touchPath_[touchStep_].second);
            if (touchStep_ == 0) io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            ++touchStep_;
        } else {
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            touchPath_.clear();
            waitFrames_ = std::max(waitFrames_, 3);
        }
        return true;
    }
    if (releaseKey_) {
        io.AddKeyEvent(static_cast<ImGuiKey>(releaseKey_), false);
        releaseKey_ = 0;
        waitFrames_ = std::max(waitFrames_, 3);
        return true;
    }
    if (waitFrames_ > 0) {
        --waitFrames_;
        return true;
    }
    while (index_ < steps_.size()) {
        auto& step = steps_[index_];
        switch (step.kind) {
        case Step::Kind::Wait:
            waitFrames_ = step.count;
            ++index_;
            return true;
        case Step::Kind::Shot:
            pendingShot_ = step.argument;
            ++index_;
            waitFrames_ = 2;
            return true;
        case Step::Kind::Quit: return false;
        case Step::Kind::Touch: {
            std::istringstream numbers(step.argument);
            float x = 0, y = 0, dx = 0, dy = 0;
            numbers >> x >> y >> dx >> dy;
            touchPath_.clear();
            touchStep_ = 0;
            const int moves = (dx != 0 || dy != 0) ? 12 : 0;
            touchPath_.emplace_back(x, y);
            touchPath_.emplace_back(x, y);
            for (int i = 1; i <= moves; ++i) touchPath_.emplace_back(x + dx * i / moves, y + dy * i / moves);
            ++index_;
            return true;
        }
        case Step::Kind::Type:
            io.AddInputCharactersUTF8(step.argument.c_str());
            ++index_;
            waitFrames_ = 2;
            return true;
        case Step::Kind::Press: {
            const auto found = buttonKeys().find(step.argument);
            if (found == buttonKeys().end()) {
                ++index_;
                continue;
            }
            io.AddKeyEvent(found->second, true);
            releaseKey_ = found->second;
            if (--step.count <= 0) ++index_;
            return true;
        }
        }
    }
    return true;
}

void Autopilot::afterRender(int width, int height) {
#ifdef __3DS__
    (void)width;
    (void)height;
#else
    if (pendingShot_.empty()) return;
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    std::vector<unsigned char> flipped(pixels.size());
    const std::size_t stride = static_cast<std::size_t>(width) * 4;
    for (int y = 0; y < height; ++y)
        std::copy_n(&pixels[static_cast<std::size_t>(height - 1 - y) * stride], stride, &flipped[static_cast<std::size_t>(y) * stride]);
    saveShot(flipped, width, height);
#endif
}

void Autopilot::saveShot(const std::vector<unsigned char>& rgba, int width, int height) {
    std::error_code error;
    std::filesystem::create_directories(outputDir_, error);
    const auto path = (outputDir_ / (pendingShot_ + ".png")).string();
    stbi_write_png(path.c_str(), width, height, 4, rgba.data(), width * 4);
    pendingShot_.clear();
}

}

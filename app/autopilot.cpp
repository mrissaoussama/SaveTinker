#include "autopilot.hpp"

#include <fstream>
#include <imgui.h>
#include <map>
#include <sstream>
#include <vector>

#ifdef __SWITCH__
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
        else if (command == "type") autopilot.steps_.push_back({Step::Kind::Type, line.size() > 5 ? line.substr(5) : "", 1});
        else if (command == "quit") autopilot.steps_.push_back({Step::Kind::Quit, {}, 1});
    }
    return autopilot;
}

bool Autopilot::beforeFrame() {
    auto& io = ImGui::GetIO();
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
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
    if (pendingShot_.empty()) return;
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    std::error_code error;
    std::filesystem::create_directories(outputDir_, error);
    const auto path = (outputDir_ / (pendingShot_ + ".png")).string();
    stbi_flip_vertically_on_write(1);
    stbi_write_png(path.c_str(), width, height, 4, pixels.data(), width * 4);
    pendingShot_.clear();
}

}

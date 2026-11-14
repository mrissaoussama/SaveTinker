#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "savetinker/platform.hpp"

namespace app {

enum class TextKind { Text, Number, Hex, Url };

struct TextRequest {
    std::string title;
    std::string initial;
    TextKind kind = TextKind::Text;
    std::size_t maxLength = 0;
};

class System {
public:
    virtual ~System() = default;

    virtual std::unique_ptr<st::Platform> createPlatform() = 0;
    // Adds the UI fonts to ImGui. Returns true if console button glyphs are available.
    virtual bool loadFonts() = 0;
    virtual void updateInput() {}
    // Blocking native keyboard. Nothing means unsupported; an empty inner value means cancelled.
    virtual std::optional<std::optional<std::string>> nativeTextInput(const TextRequest&) { return std::nullopt; }
    virtual std::string language() const { return "en"; }
    virtual std::filesystem::path appPath() const = 0;
    virtual bool hasTouchKeyboard() const { return false; }
    virtual bool usesKeyboard() const { return true; }
    virtual std::vector<std::string> defaultSaveFolders() const { return {}; }
    virtual std::string environmentWarning() const { return {}; }
    virtual void requestRestart() {}
};

std::unique_ptr<System> createSystem(int argc, char** argv);

}

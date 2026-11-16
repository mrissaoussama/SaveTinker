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
    virtual bool loadFonts() = 0;
    virtual void updateInput() {}
    // Blocking native keyboard. Nothing means unsupported; an empty inner value means cancelled.
    virtual std::optional<std::optional<std::string>> nativeTextInput(const TextRequest&) { return std::nullopt; }
    virtual std::string language() const { return "en"; }
    virtual std::filesystem::path appPath() const = 0;
    virtual bool hasTouchKeyboard() const { return false; }
    virtual bool usesKeyboard() const { return true; }
    virtual bool acceptsDroppedFiles() const { return false; }
    virtual std::vector<std::string> defaultSaveFolders() const { return {}; }
    virtual std::string environmentWarning() const { return {}; }
    // True when the app runs with less memory than the console could give it (album mode on Switch).
    virtual bool limitedMemory() const { return false; }
    // Starts `titleId` so that, with R held, the homebrew menu opens in its place with full memory.
    virtual std::optional<std::string> relaunchWithFullMemory(const std::string&) { return "not needed on this console"; }
    virtual void requestRestart() {}
    // The system's open dialog. Nothing means unavailable; an empty inner value means cancelled.
    virtual bool hasNativePicker() const { return false; }
    virtual std::optional<std::optional<std::filesystem::path>> pickPath(const std::string&, bool, const std::filesystem::path&) {
        return std::nullopt;
    }
};

std::unique_ptr<System> createSystem(int argc, char** argv);

}

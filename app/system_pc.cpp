#include <fstream>
#include <imgui.h>
#include <string_view>

#include "native_dialog.hpp"
#include "pc_platform.hpp"
#include "system.hpp"

namespace app {

namespace {

namespace fs = std::filesystem;

class PcSystem : public System {
public:
    PcSystem(fs::path console, fs::path executable) : console_(std::move(console)), executable_(std::move(executable)) {}

    std::unique_ptr<st::Platform> createPlatform() override {
        return std::make_unique<st::PcPlatform>(console_);
    }

    std::vector<std::string> defaultSaveFolders() const override {
        return {(console_ / "gba").string(), (console_ / "nds").string()};
    }

    bool loadFonts() override {
        auto& io = ImGui::GetIO();
        for (const char* candidate : {"C:/Windows/Fonts/segoeui.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                                      "/System/Library/Fonts/Supplemental/Arial.ttf"}) {
            if (fs::exists(candidate)) {
                io.Fonts->AddFontFromFileTTF(candidate, 0.0f);
                if (fs::exists("C:/Windows/Fonts/seguisym.ttf")) {
                    ImFontConfig merge;
                    merge.MergeMode = true;
                    io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/seguisym.ttf", 0.0f, &merge);
                }
                return false;
            }
        }
        io.Fonts->AddFontDefault();
        return false;
    }

    fs::path appPath() const override { return executable_; }
    bool acceptsDroppedFiles() const override { return true; }
    // --no-native-dialogs keeps the built-in browser, for controller-only setups.
    bool hasNativePicker() const override { return nativeDialogs_; }
    std::optional<std::optional<fs::path>> pickPath(const std::string& title, bool folder, const fs::path& start) override {
        auto picked = nativePickPath(title, folder, start);
        if (!picked) nativeDialogs_ = false;
        return picked;
    }
    void disableNativeDialogs() { nativeDialogs_ = false; }

private:
    fs::path console_;
    fs::path executable_;
    bool nativeDialogs_ = true;
};

}

namespace {

void prepareDemo(const fs::path& console) {
    const fs::path examples = SAVETINKER_EXAMPLES_DIR;
    std::error_code error;
    fs::copy(examples / "console", console, fs::copy_options::recursive | fs::copy_options::skip_existing, error);
    fs::create_directories(console / "SaveTinker" / "editors", error);
    for (const auto& editor : fs::directory_iterator(examples / "editors", error))
        fs::copy(editor.path(), console / "SaveTinker" / "editors" / editor.path().filename(),
                 fs::copy_options::recursive | fs::copy_options::overwrite_existing, error);
}

}

std::unique_ptr<System> createSystem(int argc, char** argv) {
    fs::path console = fs::current_path() / "console";
    bool demo = false;
    bool nativeDialogs = true;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];
        if (argument == "--console" && i + 1 < argc) console = argv[++i];
        else if (argument == "--demo") demo = true;
        else if (argument == "--no-native-dialogs") nativeDialogs = false;
    }
    if (demo) {
        console = fs::current_path() / "demo-console";
        prepareDemo(console);
    }
    auto system = std::make_unique<PcSystem>(console, fs::absolute(argv[0]));
    if (!nativeDialogs) system->disableNativeDialogs();
    return system;
}

}

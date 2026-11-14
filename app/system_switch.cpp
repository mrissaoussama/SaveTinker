#include <cstring>
#include <imgui.h>
#include <switch.h>

#include "switch_platform.hpp"
#include "system.hpp"

namespace app {

namespace {

class SwitchSystem : public System {
public:
    SwitchSystem() {
        socketInitializeDefault();
        plInitialize(PlServiceType_User);
        setInitialize();
        padConfigureInput(1, HidNpadStyleSet_NpadStandard);
        padInitializeDefault(&pad_);
    }

    ~SwitchSystem() override {
        setExit();
        plExit();
        socketExit();
    }

    std::unique_ptr<st::Platform> createPlatform() override {
        auto platform = std::make_unique<st::SwitchPlatform>();
        appletMode_ = platform->appletMode();
        return platform;
    }

    bool loadFonts() override {
        auto& io = ImGui::GetIO();
        PlFontData standard{}, extended{};
        if (R_FAILED(plGetSharedFontByType(&standard, PlSharedFontType_Standard))) return false;
        ImFontConfig config;
        config.FontDataOwnedByAtlas = false;
        io.Fonts->AddFontFromMemoryTTF(standard.address, static_cast<int>(standard.size), 0.0f, &config);
        if (R_SUCCEEDED(plGetSharedFontByType(&extended, PlSharedFontType_NintendoExt))) {
            ImFontConfig merge;
            merge.FontDataOwnedByAtlas = false;
            merge.MergeMode = true;
            io.Fonts->AddFontFromMemoryTTF(extended.address, static_cast<int>(extended.size), 0.0f, &merge);
            return true;
        }
        return false;
    }

    void updateInput() override {
        padUpdate(&pad_);
        const u64 held = padGetButtons(&pad_);
        auto& io = ImGui::GetIO();
        io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
        static constexpr std::pair<u64, ImGuiKey> buttons[] = {
            {HidNpadButton_A, ImGuiKey_GamepadFaceDown},   {HidNpadButton_B, ImGuiKey_GamepadFaceRight},
            {HidNpadButton_X, ImGuiKey_GamepadFaceUp},     {HidNpadButton_Y, ImGuiKey_GamepadFaceLeft},
            {HidNpadButton_L, ImGuiKey_GamepadL1},         {HidNpadButton_R, ImGuiKey_GamepadR1},
            {HidNpadButton_ZL, ImGuiKey_GamepadL2},        {HidNpadButton_ZR, ImGuiKey_GamepadR2},
            {HidNpadButton_Plus, ImGuiKey_GamepadStart},   {HidNpadButton_Minus, ImGuiKey_GamepadBack},
            {HidNpadButton_Up, ImGuiKey_GamepadDpadUp},    {HidNpadButton_Down, ImGuiKey_GamepadDpadDown},
            {HidNpadButton_Left, ImGuiKey_GamepadDpadLeft}, {HidNpadButton_Right, ImGuiKey_GamepadDpadRight},
        };
        for (const auto& [mask, key] : buttons) io.AddKeyEvent(key, (held & mask) != 0);

        const HidAnalogStickState stick = padGetStickPos(&pad_, 0);
        auto axis = [](s32 value) {
            const float normalized = static_cast<float>(value) / JOYSTICK_MAX;
            return normalized > 0.3f ? normalized : 0.0f;
        };
        io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickLeft, axis(-stick.x) > 0, axis(-stick.x));
        io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickRight, axis(stick.x) > 0, axis(stick.x));
        io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickUp, axis(stick.y) > 0, axis(stick.y));
        io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickDown, axis(-stick.y) > 0, axis(-stick.y));

        const HidAnalogStickState right = padGetStickPos(&pad_, 1);
        io.AddKeyAnalogEvent(ImGuiKey_GamepadRStickUp, axis(right.y) > 0, axis(right.y));
        io.AddKeyAnalogEvent(ImGuiKey_GamepadRStickDown, axis(-right.y) > 0, axis(-right.y));
    }

    std::optional<std::optional<std::string>> nativeTextInput(const TextRequest& request) override {
        SwkbdConfig keyboard;
        if (R_FAILED(swkbdCreate(&keyboard, 0))) return std::nullopt;
        swkbdConfigMakePresetDefault(&keyboard);
        swkbdConfigSetHeaderText(&keyboard, request.title.c_str());
        swkbdConfigSetGuideText(&keyboard, request.title.c_str());
        swkbdConfigSetInitialText(&keyboard, request.initial.c_str());
        if (request.maxLength) swkbdConfigSetStringLenMax(&keyboard, static_cast<u32>(request.maxLength));
        if (request.kind == TextKind::Number) {
            swkbdConfigSetType(&keyboard, SwkbdType_NumPad);
            swkbdConfigSetLeftOptionalSymbolKey(&keyboard, "-");
            swkbdConfigSetRightOptionalSymbolKey(&keyboard, ".");
        } else if (request.kind == TextKind::Hex || request.kind == TextKind::Url) {
            swkbdConfigSetType(&keyboard, SwkbdType_QWERTY);
        }
        char output[1024] = {};
        const Result rc = swkbdShow(&keyboard, output, sizeof(output));
        swkbdClose(&keyboard);
        if (R_FAILED(rc)) return std::optional<std::string>{};
        return std::optional<std::string>{std::string(output)};
    }

    std::string language() const override {
        u64 code = 0;
        if (R_FAILED(setGetSystemLanguage(&code))) return "en";
        char text[9] = {};
        std::memcpy(text, &code, 8);
        std::string language(text);
        if (auto dash = language.find('-'); dash != std::string::npos) language.resize(dash);
        return language.empty() ? "en" : language;
    }

    std::filesystem::path appPath() const override { return "sdmc:/switch/SaveTinker/SaveTinker.nro"; }
    bool hasTouchKeyboard() const override { return true; }

    std::string environmentWarning() const override {
        return appletMode_ ? "Album mode: less memory. Hold R while starting a game to launch with full memory." : "";
    }

    void requestRestart() override {
        envSetNextLoad(appPath().string().c_str(), appPath().string().c_str());
    }

private:
    PadState pad_{};
    bool appletMode_ = false;
};

}

std::unique_ptr<System> createSystem(int, char**) {
    return std::make_unique<SwitchSystem>();
}

}

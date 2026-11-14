#include <3ds.h>
#include <cstdlib>
#include <imgui.h>
#include <malloc.h>

#include "ctr_platform.hpp"
#include "system.hpp"

namespace app {

namespace {

constexpr std::size_t kSocketBufferSize = 0x100000;

class CtrSystem : public System {
public:
    CtrSystem() {
        socketBuffer_ = static_cast<u32*>(memalign(0x1000, kSocketBufferSize));
        socketsReady_ = socketBuffer_ && R_SUCCEEDED(socInit(socketBuffer_, kSocketBufferSize));
        cfgReady_ = R_SUCCEEDED(cfguInit());
    }

    ~CtrSystem() override {
        if (cfgReady_) cfguExit();
        if (socketsReady_) socExit();
        std::free(socketBuffer_);
    }

    std::unique_ptr<st::Platform> createPlatform() override { return std::make_unique<st::CtrPlatform>(); }

    bool loadFonts() override {
        ImGui::GetIO().Fonts->AddFontDefaultVector();
        return false;
    }

    void updateInput() override {
        hidScanInput();
        const u32 held = hidKeysHeld();
        auto& io = ImGui::GetIO();
        io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
        static constexpr std::pair<u32, ImGuiKey> buttons[] = {
            {KEY_A, ImGuiKey_GamepadFaceDown},    {KEY_B, ImGuiKey_GamepadFaceRight},  {KEY_X, ImGuiKey_GamepadFaceUp},
            {KEY_Y, ImGuiKey_GamepadFaceLeft},    {KEY_L, ImGuiKey_GamepadL1},         {KEY_R, ImGuiKey_GamepadR1},
            {KEY_ZL, ImGuiKey_GamepadL2},         {KEY_ZR, ImGuiKey_GamepadR2},        {KEY_START, ImGuiKey_GamepadStart},
            {KEY_SELECT, ImGuiKey_GamepadBack},   {KEY_DUP, ImGuiKey_GamepadDpadUp},   {KEY_DDOWN, ImGuiKey_GamepadDpadDown},
            {KEY_DLEFT, ImGuiKey_GamepadDpadLeft}, {KEY_DRIGHT, ImGuiKey_GamepadDpadRight},
            {KEY_CPAD_LEFT, ImGuiKey_GamepadLStickLeft}, {KEY_CPAD_RIGHT, ImGuiKey_GamepadLStickRight},
            {KEY_CPAD_UP, ImGuiKey_GamepadLStickUp},     {KEY_CPAD_DOWN, ImGuiKey_GamepadLStickDown},
            {KEY_CSTICK_UP, ImGuiKey_GamepadRStickUp},   {KEY_CSTICK_DOWN, ImGuiKey_GamepadRStickDown},
        };
        for (const auto& [mask, key] : buttons) io.AddKeyEvent(key, (held & mask) != 0);
    }

    std::optional<std::optional<std::string>> nativeTextInput(const TextRequest& request) override {
        const int maxLength = request.maxLength ? static_cast<int>(request.maxLength) : 256;
        SwkbdState keyboard;
        swkbdInit(&keyboard, request.kind == TextKind::Number ? SWKBD_TYPE_NUMPAD : SWKBD_TYPE_NORMAL, 2, maxLength);
        swkbdSetHintText(&keyboard, request.title.c_str());
        swkbdSetInitialText(&keyboard, request.initial.c_str());
        if (request.kind == TextKind::Number) swkbdSetNumpadKeys(&keyboard, L'-', L'.');
        std::string output(static_cast<std::size_t>(maxLength) * 4 + 1, '\0');
        const SwkbdButton button = swkbdInputText(&keyboard, output.data(), output.size());
        if (button != SWKBD_BUTTON_CONFIRM) return std::optional<std::string>{};
        output.resize(output.find('\0'));
        return std::optional<std::string>{output};
    }

    std::string language() const override {
        static constexpr const char* codes[] = {"ja", "en", "fr", "de", "it", "es", "zh", "ko", "nl", "pt", "ru", "zh"};
        u8 language = CFG_LANGUAGE_EN;
        if (!cfgReady_ || R_FAILED(CFGU_GetSystemLanguage(&language)) || language >= std::size(codes)) return "en";
        return codes[language];
    }

    std::filesystem::path appPath() const override { return "sdmc:/3ds/SaveTinker/SaveTinker.3dsx"; }
    bool hasTouchKeyboard() const override { return true; }
    bool usesKeyboard() const override { return false; }

private:
    u32* socketBuffer_ = nullptr;
    bool socketsReady_ = false;
    bool cfgReady_ = false;
};

}

std::unique_ptr<System> createSystem(int, char**) {
    return std::make_unique<CtrSystem>();
}

}

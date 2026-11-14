#include "ui.hpp"

#include <algorithm>
#include <format>
#include <imgui_internal.h>

#include "savetinker/qr.hpp"

namespace app::ui {

namespace {

bool keyPressed(ImGuiKey key, bool repeat) {
    return ImGui::IsKeyPressed(key, repeat);
}

bool ctrl() {
    return ImGui::GetIO().KeyCtrl;
}

}

bool pressed(Button button, bool repeat) {
    if (ImGui::GetIO().WantTextInput) return false;
    switch (button) {
    case Button::B: return keyPressed(ImGuiKey_GamepadFaceRight, false) || keyPressed(ImGuiKey_Escape, false) || keyPressed(ImGuiKey_Backspace, false);
    case Button::X: return keyPressed(ImGuiKey_GamepadFaceUp, false) || (!ctrl() && keyPressed(ImGuiKey_X, false));
    case Button::Y: return keyPressed(ImGuiKey_GamepadFaceLeft, false) || (!ctrl() && keyPressed(ImGuiKey_Y, false));
    case Button::L: return keyPressed(ImGuiKey_GamepadL1, repeat) || keyPressed(ImGuiKey_Q, repeat);
    case Button::R: return keyPressed(ImGuiKey_GamepadR1, repeat) || keyPressed(ImGuiKey_E, repeat);
    case Button::ZL: return keyPressed(ImGuiKey_GamepadL2, repeat) || (ctrl() && keyPressed(ImGuiKey_Z, repeat));
    case Button::ZR: return keyPressed(ImGuiKey_GamepadR2, repeat) || (ctrl() && keyPressed(ImGuiKey_Y, repeat));
    case Button::Plus: return keyPressed(ImGuiKey_GamepadStart, false) || (ctrl() && keyPressed(ImGuiKey_S, false));
    case Button::Minus: return keyPressed(ImGuiKey_GamepadBack, false) || keyPressed(ImGuiKey_F1, false);
    case Button::Left:
        return keyPressed(ImGuiKey_GamepadDpadLeft, true) || keyPressed(ImGuiKey_GamepadLStickLeft, true) || keyPressed(ImGuiKey_LeftArrow, true);
    case Button::Right:
        return keyPressed(ImGuiKey_GamepadDpadRight, true) || keyPressed(ImGuiKey_GamepadLStickRight, true) || keyPressed(ImGuiKey_RightArrow, true);
    }
    return false;
}

namespace {
bool focusRequested = false;
bool focusQueued = false;
bool focusHeld = false;
ImGuiID restoreTarget = 0;
int restoreFrames = 0;
ImGuiID lastFocused = 0;
ImGuiID adjustableNow = 0;
ImGuiID adjustableBefore = 0;
}

void focusNextRow() {
    focusQueued = true;
}

void cancelFocusRequest() {
    focusQueued = false;
    focusRequested = false;
}

void holdFocusRequests(bool hold) {
    focusHeld = hold;
}

ImGuiID focusedRow() {
    return lastFocused;
}

void restoreFocus(ImGuiID rowId) {
    if (!rowId) return focusNextRow();
    restoreTarget = rowId;
    restoreFrames = 2;
    focusQueued = false;
    focusRequested = false;
}

int horizontalInput(ImGuiID rowId, bool focused) {
    if (focused) adjustableNow = rowId;
    if (rowId != adjustableBefore && !focused) return 0;
    if (pressed(Button::Left, true)) return -1;
    if (pressed(Button::Right, true)) return 1;
    return 0;
}

bool wasDragged() {
    const float limit = ImGui::GetFontSize() * 0.6f;
    return ImGui::GetIO().MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] > limit * limit;
}

void dragScroll() {
    auto& io = ImGui::GetIO();
    static ImGuiWindow* target = nullptr;
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        target = GImGui->HoveredWindow;
        while (target && target->ScrollMax.y <= 0.0f && target->ParentWindow && !(target->Flags & ImGuiWindowFlags_Popup))
            target = target->ParentWindow;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) target = nullptr;
    if (!target || !wasDragged() || target->ScrollMax.y <= 0.0f) return;
    ImGui::SetScrollY(target, std::clamp(target->Scroll.y - io.MouseDelta.y, 0.0f, target->ScrollMax.y));
    ImGui::ClearActiveID();
}

void wrapNavigation() {
    ImGui::NavMoveRequestTryWrapping(ImGui::GetCurrentWindow(), ImGuiNavMoveFlags_LoopY);
}

void beginFrame() {
    if (restoreTarget && --restoreFrames < 0) {
        restoreTarget = 0;
        focusQueued = true;
    }
    focusRequested = focusRequested || focusQueued;
    focusQueued = false;
    adjustableBefore = adjustableNow;
    adjustableNow = 0;
}

float rowHeight() {
    return ImGui::GetFontSize() * 2.3f;
}

RowResult row(const char* id, const std::string& label, const std::string& value, ImU32 valueColor, bool selected, float height) {
    RowResult result;
    const float rowH = height > 0 ? height : rowHeight();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;

    ImGui::PushID(id);
    ImGui::PushStyleColor(ImGuiCol_Header, color::AccentDim);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, color::PanelHover);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, color::AccentDim);
    const bool restoring = restoreTarget && ImGui::GetID("##row") == restoreTarget;
    if (restoring || (focusRequested && !focusHeld && !restoreTarget)) {
        ImGui::SetKeyboardFocusHere();
        ImGui::SetNavCursorVisible(true);
        focusRequested = false;
        restoreTarget = 0;
    }
    result.activated = ImGui::Selectable("##row", selected, ImGuiSelectableFlags_AllowOverlap, ImVec2(width, rowH)) && !wasDragged();
    ImGui::PopStyleColor(3);
    result.id = ImGui::GetItemID();
    result.focused = ImGui::IsItemFocused();
    if (result.focused && !(ImGui::GetCurrentWindow()->RootWindow->Flags & ImGuiWindowFlags_Popup)) lastFocused = result.id;
    result.hovered = ImGui::IsItemHovered();
    ImGui::PopID();

    auto* draw = ImGui::GetWindowDrawList();
    const float padding = ImGui::GetStyle().FramePadding.x * 1.5f;
    const float textY = start.y + (rowH - ImGui::GetFontSize()) * 0.5f;
    if (result.focused) draw->AddRectFilled(start, ImVec2(start.x + 4 * ImGui::GetFontSize() / 24, start.y + rowH), color::Accent);
    const float valueWidth = value.empty() ? 0.0f : ImGui::CalcTextSize(value.c_str()).x;
    const float labelLimit = start.x + width - valueWidth - padding * 3;
    draw->PushClipRect(start, ImVec2(std::max(start.x + padding, labelLimit), start.y + rowH), true);
    draw->AddText(ImVec2(start.x + padding, textY), color::Text, label.c_str());
    draw->PopClipRect();
    if (!value.empty()) draw->AddText(ImVec2(start.x + width - valueWidth - padding, textY), valueColor, value.c_str());
    return result;
}

RowResult wrappedRow(const char* id, const std::string& label, bool selected, int maxLines) {
    const float padding = ImGui::GetStyle().FramePadding.x * 1.5f;
    const float wrapWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x - padding * 2);
    const float lineHeight = ImGui::GetTextLineHeight();
    const float textHeight = ImGui::CalcTextSize(label.c_str(), nullptr, false, wrapWidth).y;
    const float shown = std::min(textHeight, lineHeight * static_cast<float>(maxLines));
    const float height = std::max(rowHeight(), shown + ImGui::GetFontSize() * 1.3f);
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const auto result = row(id, "", {}, color::TextDim, selected, height);
    const ImVec2 textPos(start.x + padding, start.y + (height - shown) * 0.5f);
    const ImVec4 clip(textPos.x, textPos.y, textPos.x + wrapWidth, textPos.y + shown);
    ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize(), textPos, color::Text, label.c_str(), nullptr,
                                        wrapWidth, &clip);
    return result;
}

void toggleSwitch(ImVec2 rightEdge, float rowH, bool on) {
    auto* draw = ImGui::GetWindowDrawList();
    const float height = ImGui::GetFontSize() * 1.1f;
    const float width = height * 1.9f;
    const ImVec2 min(rightEdge.x - width, rightEdge.y + (rowH - height) * 0.5f);
    const ImVec2 max(rightEdge.x, min.y + height);
    draw->AddRectFilled(min, max, on ? color::Accent : IM_COL32(80, 86, 100, 255), height * 0.5f);
    const float radius = height * 0.5f - 3;
    const float knobX = on ? max.x - height * 0.5f : min.x + height * 0.5f;
    draw->AddCircleFilled(ImVec2(knobX, min.y + height * 0.5f), radius, IM_COL32_WHITE);
}

void badge(const std::string& text, ImU32 tint) {
    const ImVec2 size = ImGui::CalcTextSize(text.c_str());
    const float padX = ImGui::GetFontSize() * 0.4f, padY = ImGui::GetFontSize() * 0.1f;
    const ImVec2 start = ImGui::GetCursorScreenPos();
    auto* draw = ImGui::GetWindowDrawList();
    const ImU32 fill = (tint & 0x00FFFFFF) | (0x40u << 24);
    draw->AddRectFilled(start, ImVec2(start.x + size.x + padX * 2, start.y + size.y + padY * 2), fill, ImGui::GetFontSize() * 0.3f);
    draw->AddText(ImVec2(start.x + padX, start.y + padY), tint, text.c_str());
    ImGui::Dummy(ImVec2(size.x + padX * 2, size.y + padY * 2));
}

void sectionTitle(const std::string& text) {
    ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.3f));
    ImGui::PushStyleColor(ImGuiCol_Text, color::Accent);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopStyleColor();
    const ImVec2 position = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(position, ImVec2(position.x + ImGui::GetContentRegionAvail().x, position.y),
                                        IM_COL32(70, 76, 90, 255));
    ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.2f));
}

void dimText(const std::string& text) {
    ImGui::PushStyleColor(ImGuiCol_Text, color::TextDim);
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}

void wrappedText(const std::string& text, ImU32 tint) {
    ImGui::PushStyleColor(ImGuiCol_Text, tint);
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}

void emptyState(const std::string& title, const std::string& detail) {
    const ImVec2 available = ImGui::GetContentRegionAvail();
    ImGui::Dummy(ImVec2(0, available.y * 0.3f));
    const float titleWidth = ImGui::CalcTextSize(title.c_str()).x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (available.x - titleWidth) * 0.5f));
    ImGui::TextUnformatted(title.c_str());
    if (!detail.empty()) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + available.x);
        const float detailWidth = std::min(ImGui::CalcTextSize(detail.c_str()).x, available.x);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (available.x - detailWidth) * 0.5f));
        dimText(detail);
        ImGui::PopTextWrapPos();
    }
}

void iconTile(const Texture& texture, const std::string& name, float size) {
    if (texture) {
        ImGui::Image(ImTextureRef(static_cast<ImTextureID>(texture.id)), ImVec2(size, size), ImVec2(0, 0),
                     ImVec2(texture.uMax, texture.vMax));
        return;
    }
    const ImVec2 start = ImGui::GetCursorScreenPos();
    auto* draw = ImGui::GetWindowDrawList();
    const std::uint32_t hash = static_cast<std::uint32_t>(std::hash<std::string>{}(name));
    const ImU32 tint = IM_COL32(60 + (hash & 0x3F), 70 + ((hash >> 8) & 0x3F), 90 + ((hash >> 16) & 0x3F), 255);
    draw->AddRectFilled(start, ImVec2(start.x + size, start.y + size), tint, size * 0.12f);
    std::string initials;
    bool takeNext = true;
    for (char c : name) {
        if (takeNext && std::isalnum(static_cast<unsigned char>(c)) && initials.size() < 2) initials += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        takeNext = c == ' ';
    }
    const ImVec2 textSize = ImGui::CalcTextSize(initials.c_str());
    draw->AddText(ImVec2(start.x + (size - textSize.x) * 0.5f, start.y + (size - textSize.y) * 0.5f), color::Text, initials.c_str());
    ImGui::Dummy(ImVec2(size, size));
}

void qrCode(const std::string& text, float size) {
    const auto code = st::makeQrCode(text);
    if (!code) {
        dimText("Text too long for a QR code.");
        return;
    }
    const int border = 2;
    const float module = size / static_cast<float>(code->size + border * 2);
    const ImVec2 start = ImGui::GetCursorScreenPos();
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(start, ImVec2(start.x + size, start.y + size), IM_COL32_WHITE);
    for (int y = 0; y < code->size; ++y)
        for (int x = 0; x < code->size; ++x)
            if (code->dark(x, y)) {
                const ImVec2 min(start.x + (x + border) * module, start.y + (y + border) * module);
                draw->AddRectFilled(min, ImVec2(min.x + module + 0.5f, min.y + module + 0.5f), IM_COL32_BLACK);
            }
    ImGui::Dummy(ImVec2(size, size));
}

bool primaryButton(const char* label, float width) {
    ImGui::PushStyleColor(ImGuiCol_Button, color::AccentDim);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color::Accent);
    const bool clicked = ImGui::Button(label, ImVec2(width, 0));
    ImGui::PopStyleColor(2);
    return clicked;
}

void progressBar(float fraction, const std::string& overlay) {
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, color::Accent);
    ImGui::ProgressBar(fraction, ImVec2(-1, 0), overlay.c_str());
    ImGui::PopStyleColor();
}

const char* dropMark() {
    static const char* mark = [] {
        ImFont* font = ImGui::GetFont();
        if (font->IsGlyphInFont(0x25BE)) return "\xE2\x96\xBE";
        if (font->IsGlyphInFont(0x25BC)) return "\xE2\x96\xBC";
        return "\xE2\x80\xBA";
    }();
    return mark;
}

std::string stepper(const std::string& text) {
    static const bool quotes = ImGui::GetFont()->IsGlyphInFont(0x2039) && ImGui::GetFont()->IsGlyphInFont(0x203A);
    return quotes ? "\xE2\x80\xB9  " + text + "  \xE2\x80\xBA" : "<  " + text + "  >";
}

std::string formatSize(std::uint64_t bytes) {
    if (bytes < 1024) return std::format("{} B", bytes);
    if (bytes < 1024 * 1024) return std::format("{:.1f} KB", bytes / 1024.0);
    return std::format("{:.1f} MB", bytes / (1024.0 * 1024.0));
}

}

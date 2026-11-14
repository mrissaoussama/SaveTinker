#pragma once

#include <imgui.h>
#include <optional>
#include <string>

#include "textures.hpp"

namespace app::ui {

namespace color {
inline constexpr ImU32 Background = IM_COL32(24, 26, 32, 255);
inline constexpr ImU32 Panel = IM_COL32(33, 36, 44, 255);
inline constexpr ImU32 PanelHover = IM_COL32(44, 48, 58, 255);
inline constexpr ImU32 Accent = IM_COL32(0, 190, 210, 255);
inline constexpr ImU32 AccentDim = IM_COL32(0, 120, 135, 255);
inline constexpr ImU32 Text = IM_COL32(236, 238, 242, 255);
inline constexpr ImU32 TextDim = IM_COL32(150, 156, 170, 255);
inline constexpr ImU32 Warning = IM_COL32(245, 190, 70, 255);
inline constexpr ImU32 Danger = IM_COL32(240, 90, 90, 255);
inline constexpr ImU32 Success = IM_COL32(90, 210, 130, 255);
inline constexpr ImU32 Modified = IM_COL32(250, 140, 60, 255);
}

enum class Button { B, X, Y, L, R, ZL, ZR, Plus, Minus, Left, Right };

// Console buttons, with keyboard equivalents on desktop. Ignored while typing.
bool pressed(Button button, bool repeat = false);

float rowHeight();

// The next row drawn takes keyboard/controller focus.
void focusNextRow();
// Drops a pending focusNextRow request, for screens that place focus themselves.
void cancelFocusRequest();
// While held, rows leave a pending focus request for later rows (for side panels drawn first).
void holdFocusRequests(bool hold);
// Row focused outside popups during the last frame, and a way to focus it again after returning to a screen.
ImGuiID focusedRow();
void restoreFocus(ImGuiID rowId);

// Rows that react to left/right call this every frame. It returns -1, 0 or 1. Focus is taken from the
// previous frame because a left/right press makes navigation briefly move focus during that frame.
int horizontalInput(ImGuiID rowId, bool focused);
void beginFrame();
// True while or right after the pointer moved far enough to count as a drag rather than a tap.
bool wasDragged();
void dragScroll();
// Moving past the last row lands on the first and the other way round. Call before ending the window.
void wrapNavigation();

struct RowResult {
    ImGuiID id = 0;
    bool activated = false;
    bool focused = false;
    bool hovered = false;
};

RowResult row(const char* id, const std::string& label, const std::string& value = {}, ImU32 valueColor = color::TextDim,
              bool selected = false, float height = 0.0f);

// Row whose label wraps onto up to `maxLines` lines and is cut after that.
RowResult wrappedRow(const char* id, const std::string& label, bool selected, int maxLines);

void toggleSwitch(ImVec2 rightEdge, float rowHeight, bool on);
void badge(const std::string& text, ImU32 color);
void sectionTitle(const std::string& text);
void dimText(const std::string& text);
void wrappedText(const std::string& text, ImU32 color = color::Text);
void emptyState(const std::string& title, const std::string& detail = {});
void iconTile(const Texture& texture, const std::string& name, float size);
// Pixel-based QR code drawing.
void qrCode(const std::string& text, float size);
bool primaryButton(const char* label, float width = 0.0f);
void progressBar(float fraction, const std::string& overlay);
std::string formatSize(std::uint64_t bytes);
// "‹  text  ›" for values changed with left/right, with ASCII arrows when the font lacks the quotes.
std::string stepper(const std::string& text);
// Arrow shown after dropdown values, with a fallback for fonts without it.
const char* dropMark();

}

#include "app.hpp"
#include "i18n.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <map>

#include "curl_http_client.hpp"
#include "savetinker/manifest.hpp"
#include "ui.hpp"

#ifndef SAVETINKER_UPDATE_REPO
#define SAVETINKER_UPDATE_REPO ""
#endif

namespace app {

namespace {

#ifdef __SWITCH__
constexpr const char* kAppAsset = "SaveTinker.nro";
#else
constexpr const char* kAppAsset = "SaveTinker.exe";
#endif

double now() {
    return ImGui::GetTime();
}

}

App::App(System& system, bool consoleGlyphs)
    : system_(system),
      consoleGlyphs_(consoleGlyphs),
      platform_(std::make_unique<st::FileSavePlatform>(system.createPlatform())),
      settingsPath_(platform_->dataRoot() / "settings.json"),
      settings_(st::loadSettings(settingsPath_)),
      editors_(platform_->dataRoot() / "editors"),
      backups_(platform_->dataRoot() / "backups"),
      http_(std::make_unique<st::CurlHttpClient>("SaveTinker/" SAVETINKER_VERSION)),
      repoManager_(*http_, platform_->dataRoot() / "cache" / "repos"),
      updater_(*http_, SAVETINKER_UPDATE_REPO, SAVETINKER_VERSION, kAppAsset) {
    std::error_code ignored;
    std::filesystem::create_directories(editors_.libraryDir(), ignored);
    applyLanguage();
    applySaveFolders();
    repoStatuses_ = repoManager_.loadCached(settings_.repos, catalog_);
    reloadTitles();
    screens_.push_back(makeMainScreen());
    ui::focusNextRow();
    if (settings_.checkUpdatesOnStart && updater_.configured()) checkAppUpdate(true);
}

App::~App() = default;

void App::saveSettings() {
    if (!st::saveSettings(settingsPath_, settings_)) toast(tr("Could not save settings"), true);
}

void App::reloadTitles() {
    titles_.clear();
    userNames_.clear();
    for (const auto& user : platform_->users()) userNames_[user.id] = user.nickname;

    std::map<std::string, TitleEntry> byId;
    for (auto& title : platform_->titles()) byId[title.titleId].info = std::move(title);
    for (auto& save : platform_->saves()) {
        auto& entry = byId[save.titleId];
        if (entry.info.titleId.empty()) {
            entry.info.titleId = save.titleId;
            entry.info.platform = platform_->gamePlatform();
        }
        entry.saves.push_back(std::move(save));
    }
    for (auto& [id, entry] : byId) {
        if (entry.saves.empty()) continue;
        if (entry.info.name.empty()) entry.info.name = id;
        titles_.push_back(std::move(entry));
    }
    std::ranges::sort(titles_, [](const TitleEntry& a, const TitleEntry& b) { return a.info.name < b.info.name; });
}

const TitleEntry* App::findTitle(const std::string& titleId) const {
    for (const auto& title : titles_)
        if (title.info.titleId == titleId) return &title;
    return nullptr;
}

std::string App::userName(const std::string& userId) const {
    auto found = userNames_.find(userId);
    return found != userNames_.end() && !found->second.empty() ? found->second : userId;
}

std::string App::saveLabel(const st::SaveInfo& save) const {
    switch (save.kind) {
    case st::SaveKind::Account: return userName(save.userId);
    case st::SaveKind::Device: return tr("Device (shared)");
    case st::SaveKind::Bcat: return tr("BCAT data");
    case st::SaveKind::Cache: return tr("Cache");
    case st::SaveKind::System: return tr("System");
    case st::SaveKind::ExtData: return tr("Extra data");
    case st::SaveKind::DsCard: return tr("DS cartridge");
    case st::SaveKind::File: return save.path.filename().string();
    }
    return tr("Save");
}

void App::applySaveFolders() {
    std::vector<std::filesystem::path> folders;
    for (const auto& folder : settings_.saveFolders.empty() ? system_.defaultSaveFolders() : settings_.saveFolders)
        folders.emplace_back(folder);
    folders.push_back(platform_->dataRoot() / "saves");
    static_cast<st::FileSavePlatform&>(*platform_).setFolders(std::move(folders));
}

void App::applyLanguage() {
    loadTranslations(language(), platform_->dataRoot());
}

std::string App::language() const {
    return settings_.language == "auto" ? system_.language() : settings_.language;
}

void App::push(std::unique_ptr<Screen> screen) {
    if (!screens_.empty()) screens_.back()->savedFocus = ui::focusedRow();
    screens_.push_back(std::move(screen));
    ui::focusNextRow();
}

void App::pop() {
    if (screens_.size() <= 1) return;
    retired_.push_back(std::move(screens_.back()));
    screens_.pop_back();
    ui::restoreFocus(screens_.back()->savedFocus);
}

void App::replaceTop(std::unique_ptr<Screen> screen) {
    retired_.push_back(std::move(screens_.back()));
    screens_.back() = std::move(screen);
    ui::focusNextRow();
}

void App::runTask(std::string label, std::function<std::function<void(App&)>(Progress&)> work) {
    if (task_) {
        toast(tr("Another task is still running"), true);
        return;
    }
    auto progress = std::make_shared<Progress>();
    auto future = std::async(std::launch::async, [work = std::move(work), progress]() -> std::function<void(App&)> {
        try {
            return work(*progress);
        } catch (const std::exception& error) {
            std::string message = error.what();
            return [message](App& app) { app.showMessage(tr("Something went wrong"), message); };
        }
    });
    task_ = Task{std::move(label), std::move(progress), std::move(future)};
}

void App::promptText(TextRequest request, std::function<void(App&, std::string)> onDone) {
    if (auto native = system_.nativeTextInput(request)) {
        if (*native) onDone(*this, **native);
        return;
    }
    prompt_ = Prompt{request, std::move(onDone), request.initial};
}

void App::confirm(std::string title, std::string message, std::string confirmLabel, std::function<void(App&)> onConfirm) {
    dialog_ = Dialog{std::move(title), std::move(message), std::move(confirmLabel), std::move(onConfirm)};
}

void App::choose(std::string title, std::string message, std::vector<std::pair<std::string, std::function<void(App&)>>> choices) {
    dialog_ = Dialog{std::move(title), std::move(message), {}, {}};
    dialog_->choices = std::move(choices);
}

void App::showMessage(std::string title, std::string message) {
    dialog_ = Dialog{std::move(title), std::move(message), {}, {}};
}

void App::toast(std::string text, bool error) {
    toasts_.push_back({std::move(text), error, now() + (error ? 5.0 : 3.0)});
}

bool App::modalOpen() const {
    return task_ || prompt_ || dialog_ || popupWasOpen_ || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
}

void App::refreshRepos(bool notify) {
    auto repos = settings_.repos;
    runTask(tr("Refreshing repositories"), [this, repos, notify](Progress&) -> std::function<void(App&)> {
        st::RepoCatalog fresh;
        auto statuses = repoManager_.refresh(repos, fresh);
        return [fresh = std::move(fresh), statuses = std::move(statuses), notify](App& app) mutable {
            app.catalog_ = std::move(fresh);
            app.repoStatuses_ = std::move(statuses);
            const auto failed = std::ranges::count_if(app.repoStatuses_, [](const st::RepoStatus& s) { return !s.error.empty(); });
            if (notify) app.toast(failed ? trf("{} repositories could not be reached", failed) : tr("Repositories updated"), failed > 0);
        };
    });
}

void App::checkAppUpdate(bool silent) {
    runTask(tr("Checking for updates"), [this, silent](Progress&) -> std::function<void(App&)> {
        auto result = updater_.check();
        return [result = std::move(result), silent](App& app) {
            if (!result) {
                if (!silent) app.showMessage(tr("Update check failed"), result.error());
                return;
            }
            if (!*result) {
                if (!silent) app.toast(tr("SaveTinker is up to date"));
                return;
            }
            const auto release = **result;
            if (silent && release.version == app.settings().skippedVersion) return;
            app.confirm(trf("Update available: {}", release.version),
                        release.notes.empty() ? tr("A new version is available.") : release.notes, tr("Install"),
                        [release](App& app) {
                            app.runTask(tr("Downloading update"), [&app, release](Progress& progress) -> std::function<void(App&)> {
                                auto installed = app.updater().install(release, app.system().appPath(),
                                                                       [&progress](std::uint64_t done, std::uint64_t total) {
                                                                           progress.done = done;
                                                                           progress.total = total;
                                                                       });
                                return [installed](App& app) {
                                    if (!installed) {
                                        app.showMessage(tr("Update failed"), installed.error());
                                        return;
                                    }
                                    app.system().requestRestart();
                                    app.showMessage(tr("Update installed"), tr("Restart SaveTinker to use the new version."));
                                };
                            });
                        });
        };
    });
}

std::string App::buttonGlyph(const std::string& button) const {
    if (consoleGlyphs_) {
        static const std::map<std::string, std::string> glyphs = {
            {"A", "\xEE\x83\xA0"}, {"B", "\xEE\x83\xA1"}, {"X", "\xEE\x83\xA2"}, {"Y", "\xEE\x83\xA3"},
            {"L", "\xEE\x83\xA4"}, {"R", "\xEE\x83\xA5"}, {"ZL", "\xEE\x83\xA6"}, {"ZR", "\xEE\x83\xA7"},
            {"+", "\xEE\x83\xAF"}, {"-", "\xEE\x83\xB0"},
        };
        if (auto found = glyphs.find(button); found != glyphs.end()) return found->second;
        return button;
    }
    if (!system_.usesKeyboard()) {
        static const std::map<std::string, std::string> names = {{"+", "START"}, {"-", "SELECT"}, {"RS", "C-Stick"}};
        if (auto found = names.find(button); found != names.end()) return found->second;
        return button;
    }
    static const std::map<std::string, std::string> keys = {
        {"A", "Enter"}, {"B", "Esc"}, {"X", "X"}, {"Y", "Y"}, {"L", "Q"}, {"R", "E"},
        {"ZL", "Ctrl+Z"}, {"ZR", "Ctrl+Y"}, {"+", "Ctrl+S"}, {"-", "F1"}, {"RS", "PgUp/PgDn"},
    };
    if (auto found = keys.find(button); found != keys.end()) return found->second;
    return button;
}

void App::applyStyle() {
    ImGuiStyle style;
    ImGui::StyleColorsDark(&style);
    style.WindowRounding = 0;
    style.FrameRounding = 6;
    style.PopupRounding = 8;
    style.GrabRounding = 6;
    style.ScrollbarRounding = 6;
    style.WindowBorderSize = 0;
    style.PopupBorderSize = 1;
    style.FramePadding = ImVec2(12, 8);
    style.ItemSpacing = ImVec2(10, 6);
    style.WindowPadding = ImVec2(20, 14);
    style.ScrollbarSize = 10;
    style.SelectableTextAlign = ImVec2(0, 0.5f);

    auto* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImGui::ColorConvertU32ToFloat4(ui::color::Background);
    colors[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_PopupBg] = ImGui::ColorConvertU32ToFloat4(ui::color::Panel);
    colors[ImGuiCol_Text] = ImGui::ColorConvertU32ToFloat4(ui::color::Text);
    colors[ImGuiCol_TextDisabled] = ImGui::ColorConvertU32ToFloat4(ui::color::TextDim);
    colors[ImGuiCol_Button] = ImGui::ColorConvertU32ToFloat4(ui::color::Panel);
    colors[ImGuiCol_ButtonHovered] = ImGui::ColorConvertU32ToFloat4(ui::color::PanelHover);
    colors[ImGuiCol_ButtonActive] = ImGui::ColorConvertU32ToFloat4(ui::color::AccentDim);
    colors[ImGuiCol_FrameBg] = ImGui::ColorConvertU32ToFloat4(ui::color::Panel);
    colors[ImGuiCol_Header] = ImGui::ColorConvertU32ToFloat4(ui::color::AccentDim);
    colors[ImGuiCol_NavCursor] = ImGui::ColorConvertU32ToFloat4(ui::color::Accent);
    colors[ImGuiCol_CheckMark] = ImGui::ColorConvertU32ToFloat4(ui::color::Accent);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.6f);
    colors[ImGuiCol_Tab] = ImGui::ColorConvertU32ToFloat4(ui::color::Panel);
    colors[ImGuiCol_TabSelected] = ImGui::ColorConvertU32ToFloat4(ui::color::AccentDim);

    style.ScaleAllSizes(scale_);
    style.FontSizeBase = 22.0f * scale_;
    ImGui::GetStyle() = style;
    appliedScale_ = scale_;
}

void App::frame() {
    ui::beginFrame();
    popupWasOpen_ = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    scale_ = std::max(0.5f, display.y / 720.0f) * settings_.uiScale;
    if (scale_ != appliedScale_) applyStyle();

    if (task_ && task_->result.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto finish = task_->result.get();
        task_.reset();
        if (finish) finish(*this);
    }

    // Header and footer are plain drawings; the root window holds only the screen so navigation stays inside it.
    const ImVec2 margin = ImGui::GetStyle().WindowPadding;
    const float headerHeight = ImGui::GetFontSize() * 2.0f + margin.y;
    const float footerHeight = ImGui::GetFontSize() * 2.2f;
    auto* background = ImGui::GetBackgroundDrawList();
    drawHeader(background, ImVec2(margin.x, margin.y * 0.5f), display.x - margin.x * 2, headerHeight - margin.y * 0.5f);
    drawFooter(background, ImVec2(margin.x, display.y - footerHeight), display.x - margin.x * 2, footerHeight);

    ImGui::SetNextWindowPos(ImVec2(0, headerHeight));
    ImGui::SetNextWindowSize(ImVec2(display.x, display.y - headerHeight - footerHeight));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(margin.x, margin.y * 0.6f));
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar();
    try {
        screens_.back()->draw(*this);
    } catch (const std::exception& error) {
        // Keep the app alive; a broken screen is closed and the error shown.
        if (screens_.size() > 1) pop();
        showMessage(tr("Something went wrong"), error.what());
    }

    if (!modalOpen()) handleBack();
    drawDialog();
    drawPrompt();
    drawTask();
    ImGui::End();
    drawToasts();
    ui::dragScroll();
    handleFooterTaps();
    retired_.clear();
}

void App::handleBack() {
    if (!ui::pressed(ui::Button::B)) return;
    if (screens_.size() > 1) {
        if (screens_.back()->requestClose(*this)) pop();
    }
}

void App::drawHeader(ImDrawList* draw, ImVec2 origin, float width, float height) {
    const float textY = origin.y + (height - ImGui::GetFontSize()) * 0.5f;
    const char* name = "SaveTinker";
    draw->AddText(ImVec2(origin.x, textY), ui::color::Accent, name);
    std::string path;
    for (std::size_t i = 1; i < screens_.size(); ++i) path += "  \xE2\x80\xBA  " + screens_[i]->title();
    draw->AddText(ImVec2(origin.x + ImGui::CalcTextSize(name).x, textY), ui::color::TextDim, path.c_str());

    const std::string warning = system_.environmentWarning();
    const std::string right = warning.empty() ? "v" SAVETINKER_VERSION : warning;
    const float rightWidth = ImGui::CalcTextSize(right.c_str()).x;
    draw->AddText(ImVec2(origin.x + width - rightWidth, textY), warning.empty() ? ui::color::TextDim : ui::color::Warning, right.c_str());
    draw->AddLine(ImVec2(origin.x, origin.y + height), ImVec2(origin.x + width, origin.y + height), IM_COL32(60, 66, 80, 255));
}

void App::drawFooter(ImDrawList* draw, ImVec2 origin, float width, float height) {
    draw->AddLine(origin, ImVec2(origin.x + width, origin.y), IM_COL32(60, 66, 80, 255));
    float x = origin.x + width;
    const float y = origin.y + (height - ImGui::GetFontSize()) * 0.5f;
    const auto hints = screens_.back()->hints(*this);
    footerTargets_.clear();
    for (auto it = hints.rbegin(); it != hints.rend(); ++it) {
        const float right = x;
        const std::string glyph = buttonGlyph(it->button);
        const float labelWidth = ImGui::CalcTextSize(it->label.c_str()).x;
        const float glyphWidth = ImGui::CalcTextSize(glyph.c_str()).x;
        x -= labelWidth;
        draw->AddText(ImVec2(x, y), ui::color::Text, it->label.c_str());
        x -= glyphWidth + ImGui::GetFontSize() * 0.35f;
        if (!consoleGlyphs_) {
            const float pad = ImGui::GetFontSize() * 0.2f;
            draw->AddRectFilled(ImVec2(x - pad, y - pad * 0.5f), ImVec2(x + glyphWidth + pad, y + ImGui::GetFontSize() + pad * 0.5f),
                                IM_COL32(70, 76, 92, 255), pad);
        }
        draw->AddText(ImVec2(x, y), ui::color::Text, glyph.c_str());
        footerTargets_.push_back({ImVec2(x - ImGui::GetFontSize() * 0.4f, origin.y), ImVec2(right, origin.y + height), it->button});
        x -= ImGui::GetFontSize() * 1.4f;
    }
    footerLeft_ = ImVec2(origin.x, y);
}

// Footer hints double as buttons for touch: a tap sends the same input as the console button.
void App::handleFooterTaps() {
    auto& io = ImGui::GetIO();
    if (releaseFooterKey_ != ImGuiKey_None) {
        io.AddKeyEvent(releaseFooterKey_, false);
        releaseFooterKey_ = ImGuiKey_None;
    }
    if (!ImGui::IsMouseReleased(ImGuiMouseButton_Left) || ui::wasDragged()) return;
    static const std::map<std::string, ImGuiKey> keys = {
        {"A", ImGuiKey_GamepadFaceDown}, {"B", ImGuiKey_GamepadFaceRight}, {"X", ImGuiKey_GamepadFaceUp},
        {"Y", ImGuiKey_GamepadFaceLeft}, {"L", ImGuiKey_GamepadL1},        {"R", ImGuiKey_GamepadR1},
        {"ZL", ImGuiKey_GamepadL2},      {"ZR", ImGuiKey_GamepadR2},       {"+", ImGuiKey_GamepadStart},
        {"-", ImGuiKey_GamepadBack},     {"RS", ImGuiKey_GamepadRStickDown},
    };
    for (const auto& target : footerTargets_) {
        if (io.MousePos.x < target.min.x || io.MousePos.x > target.max.x || io.MousePos.y < target.min.y ||
            io.MousePos.y > target.max.y)
            continue;
        if (auto key = keys.find(target.button); key != keys.end()) {
            io.AddKeyEvent(key->second, true);
            releaseFooterKey_ = key->second;
        }
        break;
    }
}

void App::drawTask() {
    if (!task_) return;
    if (!ImGui::IsPopupOpen("##task")) ImGui::OpenPopup("##task");
    ImGui::SetNextWindowPos(ImGui::GetIO().DisplaySize * 0.5f, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 22, 0));
    if (ImGui::BeginPopupModal("##task", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(task_->label.c_str());
        const auto total = task_->progress->total.load();
        const auto done = task_->progress->done.load();
        if (total > 0) {
            ui::progressBar(static_cast<float>(done) / static_cast<float>(total),
                            ui::formatSize(done) + " / " + ui::formatSize(total));
        } else {
            const float phase = static_cast<float>(std::fmod(ImGui::GetTime(), 1.2) / 1.2);
            ui::progressBar(-1.0f * phase, "");
        }
        ImGui::EndPopup();
    }
}

void App::drawPrompt() {
    if (!prompt_) return;
    if (!prompt_->opened) {
        ImGui::OpenPopup("##prompt");
        prompt_->opened = true;
    }
    ImGui::SetNextWindowPos(ImGui::GetIO().DisplaySize * 0.5f, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 26, 0));
    bool close = false;
    if (ImGui::BeginPopupModal("##prompt", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(prompt_->request.title.c_str());
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(-1);
        prompt_->buffer.reserve(std::max<std::size_t>(prompt_->buffer.size() + 1, 512));
        ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue;
        if (prompt_->request.kind == TextKind::Hex) flags |= ImGuiInputTextFlags_CharsHexadecimal;
        if (prompt_->request.kind == TextKind::Number) flags |= ImGuiInputTextFlags_CharsScientific;
        char buffer[1024];
        std::snprintf(buffer, sizeof(buffer), "%s", prompt_->buffer.c_str());
        const bool submitted = ImGui::InputText("##value", buffer, sizeof(buffer), flags);
        prompt_->buffer = buffer;
        if (submitted || ui::primaryButton(trc("OK"))) {
            auto onDone = std::move(prompt_->onDone);
            auto value = prompt_->buffer;
            close = true;
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            prompt_.reset();
            onDone(*this, value);
            return;
        }
        ImGui::SameLine();
        if (ImGui::Button(trc("Cancel")) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            close = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (close) prompt_.reset();
}

void App::drawDialog() {
    if (!dialog_) return;
    if (!dialog_->opened) {
        ImGui::OpenPopup("##dialog");
        dialog_->opened = true;
    }
    ImGui::SetNextWindowPos(ImGui::GetIO().DisplaySize * 0.5f, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 28, 0));
    if (ImGui::BeginPopupModal("##dialog", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::color::Accent);
        ImGui::TextWrapped("%s", dialog_->title.c_str());
        ImGui::PopStyleColor();
        ImGui::Spacing();
        ImGui::BeginChild("##message", ImVec2(0, std::min(ImGui::GetFontSize() * 14,
                                                           ImGui::CalcTextSize(dialog_->message.c_str(), nullptr, false,
                                                                               ImGui::GetFontSize() * 26).y + 8)));
        ImGui::TextWrapped("%s", dialog_->message.c_str());
        ImGui::EndChild();
        ImGui::Spacing();

        bool done = false;
        std::function<void(App&)> action;
        if (!dialog_->choices.empty()) {
            for (std::size_t i = 0; i < dialog_->choices.size(); ++i) {
                if (i == 0 && ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
                const auto& [label, onChoose] = dialog_->choices[i];
                const bool chosen = i == 0 ? ui::primaryButton(label.c_str()) : ImGui::Button(label.c_str());
                if (chosen) {
                    action = onChoose;
                    done = true;
                }
                ImGui::SameLine();
            }
            if (ImGui::Button(trc("Cancel")) || (ui::pressed(ui::Button::B) && !ImGui::IsWindowAppearing())) done = true;
        } else if (dialog_->onConfirm) {
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            if (ui::primaryButton(dialog_->confirmLabel.c_str())) {
                action = dialog_->onConfirm;
                done = true;
            }
            ImGui::SameLine();
            if (ImGui::Button(trc("Cancel")) || (ui::pressed(ui::Button::B) && !ImGui::IsWindowAppearing())) done = true;
        } else {
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            if (ui::primaryButton(trc("OK")) || (ui::pressed(ui::Button::B) && !ImGui::IsWindowAppearing())) done = true;
        }
        if (done) {
            ImGui::CloseCurrentPopup();
            dialog_.reset();
        }
        ImGui::EndPopup();
        if (action) action(*this);
    }
}

void App::drawToasts() {
    std::erase_if(toasts_, [](const Toast& toast) { return toast.until < now(); });
    if (toasts_.empty()) return;
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    float y = display.y - ImGui::GetFontSize() * 3.2f;
    auto* draw = ImGui::GetForegroundDrawList();
    for (auto it = toasts_.rbegin(); it != toasts_.rend(); ++it) {
        const ImVec2 size = ImGui::CalcTextSize(it->text.c_str());
        const float pad = ImGui::GetFontSize() * 0.6f;
        const ImVec2 max(display.x - pad, y);
        const ImVec2 min(max.x - size.x - pad * 2, max.y - size.y - pad * 1.4f);
        draw->AddRectFilled(min, max, ui::color::Panel, pad * 0.6f);
        draw->AddRect(min, max, it->error ? ui::color::Danger : ui::color::Accent, pad * 0.6f, 0, 2.0f);
        draw->AddText(ImVec2(min.x + pad, min.y + pad * 0.7f), ui::color::Text, it->text.c_str());
        y = min.y - pad * 0.5f;
    }
}

}

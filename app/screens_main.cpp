#include <algorithm>
#include <format>

#include "app.hpp"
#include "savetinker/manifest.hpp"
#include "savetinker/version.hpp"
#include "ui.hpp"

namespace app {

namespace {

enum class Tab { Games, Editors, Repos, Settings };
constexpr const char* kTabNames[] = {"Games", "Editors", "Repositories", "Settings"};

bool gameRow(App& app, const TitleEntry& title, std::size_t installedEditors, std::size_t repoEditors) {
    const float height = ImGui::GetFontSize() * 3.4f;
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const auto result = ui::row(title.info.titleId.c_str(), "", "", ui::color::TextDim, false, height);
    const float pad = ImGui::GetFontSize() * 0.5f;
    const float iconSize = height - pad * 2;

    const ImVec2 after = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(start.x + pad * 1.4f, start.y + pad));
    ui::iconTile(app.textures().get(title.info.titleId, title.info.icon), title.info.name, iconSize);

    auto* draw = ImGui::GetWindowDrawList();
    const float textX = start.x + pad * 2.6f + iconSize;
    draw->AddText(ImVec2(textX, start.y + pad * 1.1f), ui::color::Text, title.info.name.c_str());
    std::string detail = title.info.publisher;
    if (!title.info.displayVersion.empty()) detail += (detail.empty() ? "" : "  \xC2\xB7  ") + std::string("v") + title.info.displayVersion;
    detail += (detail.empty() ? "" : "  \xC2\xB7  ") + std::format("{} save{}", title.saves.size(), title.saves.size() == 1 ? "" : "s");
    draw->AddText(ImVec2(textX, start.y + pad * 1.3f + ImGui::GetFontSize()), ui::color::TextDim, detail.c_str());

    std::string tag;
    ImU32 tagColor = ui::color::Success;
    if (installedEditors) tag = installedEditors == 1 ? "Editor ready" : std::format("{} editors", installedEditors);
    else if (repoEditors) {
        tag = std::format("{} in repos", repoEditors);
        tagColor = ui::color::Accent;
    }
    if (!tag.empty()) {
        const float width = ImGui::CalcTextSize(tag.c_str()).x + pad * 1.6f;
        ImGui::SetCursorScreenPos(ImVec2(start.x + ImGui::GetContentRegionAvail().x - width - pad, start.y + (height - ImGui::GetFontSize() * 1.2f) * 0.5f));
        ui::badge(tag, tagColor);
    }
    ImGui::SetCursorScreenPos(after);
    ImGui::Dummy(ImVec2(0, 0));
    return result.activated;
}

class MainScreen : public Screen {
public:
    std::string title() const override { return kTabNames[static_cast<int>(tab_)]; }

    std::vector<Hint> hints(const App&) const override {
        std::vector<Hint> hints = {{"A", "Open"}, {"L", "Tab"}, {"R", "Tab"}};
        if (tab_ == Tab::Games) hints.push_back({"X", "Reload"});
        if (tab_ == Tab::Editors || tab_ == Tab::Repos) hints.push_back({"X", "Refresh"});
        if (tab_ == Tab::Repos) hints.push_back({"Y", "Add"});
        hints.push_back({"+", "Exit"});
        return hints;
    }

    void draw(App& app) override {
        if (!app.modalOpen()) {
            if (ui::pressed(ui::Button::L)) switchTab(-1);
            if (ui::pressed(ui::Button::R)) switchTab(1);
            if (ui::pressed(ui::Button::Plus)) app.quit();
        }
        drawTabs();
        ImGui::BeginChild("##tab", ImVec2(0, 0), ImGuiChildFlags_NavFlattened);
        if (tabChanged_) {
            ImGui::SetScrollY(0);
            tabChanged_ = false;
        }
        switch (tab_) {
        case Tab::Games: drawGames(app); break;
        case Tab::Editors: drawEditors(app); break;
        case Tab::Repos: drawRepos(app); break;
        case Tab::Settings: drawSettings(app); break;
        }
        ImGui::EndChild();
    }

private:
    void switchTab(int delta) {
        tab_ = static_cast<Tab>((static_cast<int>(tab_) + delta + 4) % 4);
        tabChanged_ = true;
        ui::focusNextRow();
    }

    void drawTabs() {
        auto* draw = ImGui::GetWindowDrawList();
        const ImVec2 start = ImGui::GetCursorScreenPos();
        float x = start.x;
        const float pad = ImGui::GetFontSize() * 0.9f;
        const float height = ImGui::GetFontSize() * 1.9f;
        for (int i = 0; i < 4; ++i) {
            const bool selected = static_cast<int>(tab_) == i;
            const float width = ImGui::CalcTextSize(kTabNames[i]).x + pad * 2;
            ImGui::SetCursorScreenPos(ImVec2(x, start.y));
            ImGui::PushID(i);
            if (ImGui::InvisibleButton("##tab", ImVec2(width, height), ImGuiButtonFlags_None)) {
                tab_ = static_cast<Tab>(i);
                tabChanged_ = true;
            }
            ImGui::PopID();
            if (selected) draw->AddRectFilled(ImVec2(x, start.y + height - 4), ImVec2(x + width, start.y + height), ui::color::Accent, 2);
            draw->AddText(ImVec2(x + pad, start.y + (height - ImGui::GetFontSize()) * 0.5f),
                          selected ? ui::color::Text : ui::color::TextDim, kTabNames[i]);
            x += width;
        }
        ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + height));
        ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.4f));
    }

    void drawGames(App& app) {
        if (!app.modalOpen() && ui::pressed(ui::Button::X)) {
            app.reloadTitles();
            app.toast("Game list reloaded");
        }
        const auto& titles = app.titles();
        if (titles.empty()) {
            ui::emptyState("No saves found", "Play a game once so it creates save data, then come back.");
            return;
        }
        const auto running = app.platform().runningTitleId();
        if (running) {
            const auto* game = running->empty() ? nullptr : app.findTitle(*running);
            ui::wrappedText(std::format("{} is running. Close it from the HOME menu before editing its save.",
                                        game ? game->info.name : std::string("A game")),
                            ui::color::Warning);
        }
        for (const auto& title : titles) {
            const auto installed = app.editors().forGame(title.info.platform, title.info.titleId).size();
            const auto inRepos = app.catalog().findEditors(title.info.platform, title.info.titleId).size();
            if (gameRow(app, title, installed, inRepos)) app.push(makeGameScreen(title.info.titleId));
        }
    }

    void drawEditors(App& app) {
        if (!app.modalOpen() && ui::pressed(ui::Button::X)) app.refreshRepos(true);
        std::vector<st::BrokenEditor> broken;
        const auto editors = app.editors().list(&broken);
        const auto updates = app.catalog().findUpdates(app.editors().installedVersions());
        if (editors.empty() && broken.empty()) {
            ui::emptyState("No editors installed", "Add a repository in the Repositories tab, or copy editor folders to " +
                                                       app.editors().root().generic_string());
            return;
        }
        for (const auto& editor : editors) {
            std::string platforms;
            for (const auto& platform : editor.manifest.platforms())
                platforms += (platforms.empty() ? "" : ", ") + st::platformDisplayName(platform);
            const bool hasUpdate = std::ranges::any_of(updates, [&](const st::EditorUpdate& u) { return u.editor->id == editor.manifest.id; });
            const std::string value = hasUpdate ? "Update available" : "v" + editor.manifest.version;
            if (ui::row(editor.manifest.id.c_str(), editor.manifest.name + "   " + platforms, value,
                        hasUpdate ? ui::color::Accent : ui::color::TextDim)
                    .activated)
                app.push(makeEditorDetailScreen(editor.manifest.id));
        }
        for (const auto& item : broken) {
            if (ui::row(item.directory.string().c_str(), item.directory.filename().string(), "Broken", ui::color::Danger).activated)
                app.showMessage("Editor could not be loaded", item.error);
        }
    }

    void drawRepos(App& app) {
        if (!app.modalOpen()) {
            if (ui::pressed(ui::Button::X)) app.refreshRepos(true);
            if (ui::pressed(ui::Button::Y)) addRepo(app);
        }
        if (ui::row("##add", "Add repository...", "", ui::color::Accent).activated) addRepo(app);
        auto& repos = app.settings().repos;
        if (repos.empty()) {
            ui::dimText("Repositories list editors you can install. Anyone can host one: it is a repo.json file on any web server.");
            return;
        }
        for (const auto& repo : repos) {
            const auto source = std::ranges::find(app.catalog().repos(), repo.url, &st::RepoSource::url);
            const bool loaded = source != app.catalog().repos().end();
            const auto status = std::ranges::find(app.repoStatuses(), repo.url, &st::RepoStatus::url);
            std::string name = loaded && !source->index.name.empty() ? source->index.name : repo.url;
            std::string value;
            ImU32 valueColor = ui::color::TextDim;
            if (!repo.enabled) value = "Disabled";
            else if (status != app.repoStatuses().end() && !status->error.empty() && !loaded) {
                value = "Unreachable";
                valueColor = ui::color::Danger;
            } else if (loaded) {
                value = std::format("{} editors", source->index.editors.size());
                if (status != app.repoStatuses().end() && status->fromCache) value += " (offline copy)";
            } else {
                value = "Not loaded";
            }
            if (ui::row(repo.url.c_str(), name, value, valueColor).activated) app.push(makeRepoScreen(repo.url));
        }
    }

    static void addRepo(App& app) {
        app.promptText({"Repository URL (repo.json)", "https://", TextKind::Url, 512}, [](App& app, std::string url) {
            while (!url.empty() && url.back() == ' ') url.pop_back();
            if (!url.starts_with("https://") && !url.starts_with("http://")) {
                app.toast("The URL must start with https://", true);
                return;
            }
            auto& repos = app.settings().repos;
            if (std::ranges::find(repos, url, &st::RepoEntry::url) != repos.end()) {
                app.toast("That repository is already added", true);
                return;
            }
            repos.push_back({url, true});
            app.saveSettings();
            app.refreshRepos(true);
        });
    }

    void drawSettings(App& app) {
        auto& settings = app.settings();
        bool changed = false;
        ui::sectionTitle("Saving");
        {
            const ImVec2 start = ImGui::GetCursorScreenPos();
            const float width = ImGui::GetContentRegionAvail().x;
            if (ui::row("##autobackup", "Back up before writing a save").activated) {
                settings.autoBackup = !settings.autoBackup;
                changed = true;
            }
            ui::toggleSwitch(ImVec2(start.x + width - ImGui::GetFontSize() * 0.6f, start.y), ui::rowHeight(), settings.autoBackup);
        }
        {
            const auto result = ui::row("##keep", "Backups kept per save", std::format("\xE2\x80\xB9  {}  \xE2\x80\xBA", settings.backupsToKeep));
            const int step = ui::horizontalInput(result.id, result.focused);
            if (step < 0 && settings.backupsToKeep > 1) {
                --settings.backupsToKeep;
                changed = true;
            }
            if (step > 0 && settings.backupsToKeep < 100) {
                ++settings.backupsToKeep;
                changed = true;
            }
        }

        ui::sectionTitle("Display");
        {
            static constexpr float sizes[] = {0.85f, 1.0f, 1.15f, 1.3f};
            static constexpr const char* names[] = {"Small", "Normal", "Large", "Larger"};
            int index = 1;
            for (int i = 0; i < 4; ++i)
                if (std::abs(sizes[i] - settings.uiScale) < 0.01f) index = i;
            const auto result = ui::row("##scale", "Interface size", std::format("\xE2\x80\xB9  {}  \xE2\x80\xBA", names[index]));
            const int step = ui::horizontalInput(result.id, result.focused);
            int next = index;
            if (step < 0) next = std::max(0, index - 1);
            if (step > 0) next = std::min(3, index + 1);
            if (result.activated) next = (index + 1) % 4;
            if (next != index) {
                settings.uiScale = sizes[next];
                changed = true;
            }
        }
        {
            static const std::vector<std::pair<std::string, std::string>> languages = {
                {"auto", "System"}, {"en", "English"}, {"fr", "Français"}, {"de", "Deutsch"}, {"es", "Español"},
                {"it", "Italiano"}, {"ja", "日本語"}, {"ar", "العربية"}};
            std::size_t index = 0;
            for (std::size_t i = 0; i < languages.size(); ++i)
                if (languages[i].first == settings.language) index = i;
            const auto result = ui::row("##lang", "Editor label language", std::format("\xE2\x80\xB9  {}  \xE2\x80\xBA", languages[index].second));
            const int step = ui::horizontalInput(result.id, result.focused);
            std::size_t next = index;
            if (step > 0 || result.activated) next = (index + 1) % languages.size();
            if (step < 0) next = (index + languages.size() - 1) % languages.size();
            if (next != index) {
                settings.language = languages[next].first;
                changed = true;
            }
        }

        ui::sectionTitle("Updates");
        {
            const ImVec2 start = ImGui::GetCursorScreenPos();
            const float width = ImGui::GetContentRegionAvail().x;
            if (ui::row("##checkstart", "Check for SaveTinker updates on start").activated) {
                settings.checkUpdatesOnStart = !settings.checkUpdatesOnStart;
                changed = true;
            }
            ui::toggleSwitch(ImVec2(start.x + width - ImGui::GetFontSize() * 0.6f, start.y), ui::rowHeight(), settings.checkUpdatesOnStart);
        }
        if (ui::row("##checknow", "Check for updates now", app.updater().configured() ? "" : "Not available in this build").activated) {
            if (app.updater().configured()) app.checkAppUpdate(false);
            else app.toast("This build has no update source", true);
        }
        if (ui::row("##repos", "Refresh repositories").activated) app.refreshRepos(true);

        ui::sectionTitle("About");
        ui::row("##version", "Version", SAVETINKER_VERSION);
        ui::row("##data", "Data folder", app.platform().dataRoot().generic_string());
        if (ui::row("##licenses", "Open source licenses").activated)
            app.showMessage("Open source licenses",
                            "Lua 5.4 (MIT), Dear ImGui (MIT), nlohmann/json (MIT), miniz (MIT), "
                            "QR Code generator by Nayuki (MIT), stb_image (public domain), doctest (MIT), "
                            "SDL2 (zlib), libcurl (curl license), libnx (ISC).");
        if (changed) app.saveSettings();
    }

    Tab tab_ = Tab::Games;
    bool tabChanged_ = false;
};

}

std::unique_ptr<Screen> makeMainScreen() {
    return std::make_unique<MainScreen>();
}

}

#include <algorithm>
#include <format>

#include "app.hpp"
#include "i18n.hpp"
#include "savetinker/manifest.hpp"
#include "savetinker/version.hpp"
#include "ui.hpp"

namespace app {

namespace {

enum class Tab { Games, Editors, Repos, Settings };
constexpr const char* kTabNames[] = {tr_noop("Games"), tr_noop("Editors"), tr_noop("Repositories"), tr_noop("Settings")};

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
    detail += (detail.empty() ? "" : "  \xC2\xB7  ") + (title.saves.size() == 1 ? tr("1 save") : trf("{} saves", title.saves.size()));
    draw->AddText(ImVec2(textX, start.y + pad * 1.3f + ImGui::GetFontSize()), ui::color::TextDim, detail.c_str());

    if (installedEditors) {
        // A green dot marks a game that has an editor ready; a number beside it counts several.
        const float radius = ImGui::GetFontSize() * 0.3f;
        const std::string count = installedEditors > 1 ? std::to_string(installedEditors) : "";
        const float countWidth = count.empty() ? 0 : ImGui::CalcTextSize(count.c_str()).x + radius;
        const float right = start.x + ImGui::GetContentRegionAvail().x - pad;
        const ImVec2 centre(right - countWidth - radius, start.y + height * 0.5f);
        draw->AddCircleFilled(centre, radius, ui::color::Success);
        if (!count.empty())
            draw->AddText(ImVec2(right - countWidth + radius, start.y + (height - ImGui::GetFontSize()) * 0.5f), ui::color::Success, count.c_str());
    }
    const std::string tag = !installedEditors && repoEditors ? trf("{} in repos", repoEditors) : "";
    const ImU32 tagColor = ui::color::Accent;
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
    std::string title() const override { return tr(kTabNames[static_cast<int>(tab_)]); }

    std::vector<Hint> hints(const App&) const override {
        std::vector<Hint> hints = {{"A", tr("Open")}, {"L", tr("Tab")}, {"R", tr("Tab")}};
        if (tab_ == Tab::Games) hints.push_back({"X", tr("Reload")});
        if (tab_ == Tab::Editors || tab_ == Tab::Repos) hints.push_back({"X", tr("Refresh")});
        if (tab_ == Tab::Repos) hints.push_back({"Y", tr("Add")});
        hints.push_back({"+", tr("Exit")});
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
            const float width = ImGui::CalcTextSize(trc(kTabNames[i])).x + pad * 2;
            ImGui::SetCursorScreenPos(ImVec2(x, start.y));
            ImGui::PushID(i);
            if (ImGui::InvisibleButton("##tab", ImVec2(width, height), ImGuiButtonFlags_None)) {
                tab_ = static_cast<Tab>(i);
                tabChanged_ = true;
            }
            ImGui::PopID();
            if (selected) draw->AddRectFilled(ImVec2(x, start.y + height - 4), ImVec2(x + width, start.y + height), ui::color::Accent, 2);
            draw->AddText(ImVec2(x + pad, start.y + (height - ImGui::GetFontSize()) * 0.5f),
                          selected ? ui::color::Text : ui::color::TextDim, trc(kTabNames[i]));
            x += width;
        }
        ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + height));
        ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.4f));
    }

    void drawGames(App& app) {
        if (!app.modalOpen() && ui::pressed(ui::Button::X)) {
            app.reloadTitles();
            app.toast(tr("Game list reloaded"));
        }
        const auto& titles = app.titles();
        if (titles.empty()) {
            ui::emptyState(tr("No saves found"), tr("Play a game once so it creates save data, then come back."));
            return;
        }
        const auto running = app.platform().runningTitleId();
        if (running) {
            const auto* game = running->empty() ? nullptr : app.findTitle(*running);
            ui::wrappedText(trf("{} is running. Close it from the HOME menu before editing its save.",
                                        game ? game->info.name : std::string(tr("A game"))),
                            ui::color::Warning);
        }
        for (const auto& title : titles) {
            const auto& platform = title.info.platform;
            const auto& titleId = title.info.titleId;
            const auto installed = std::ranges::count_if(app.editors().forGame(platform, titleId), [&](const st::InstalledEditorInfo& editor) {
                return !editor.manifest.findGame(platform, titleId)->generic();
            });
            const auto inRepos = std::ranges::count_if(app.catalog().findEditors(platform, titleId), [&](const st::EditorMatch& match) {
                return !match.editor->findGame(platform, titleId)->generic();
            });
            if (gameRow(app, title, static_cast<std::size_t>(installed), static_cast<std::size_t>(inRepos))) app.push(makeGameScreen(title.info.titleId));
        }
    }

    void drawEditors(App& app) {
        if (!app.modalOpen() && ui::pressed(ui::Button::X)) app.refreshRepos(true);
        std::vector<st::BrokenEditor> broken;
        const auto editors = app.editors().list(&broken);
        const auto updates = app.catalog().findUpdates(app.editors().installedVersions());
        if (editors.empty() && broken.empty()) {
            ui::emptyState(tr("No editors installed"), trf("Add a repository in the Repositories tab, or copy editor folders to {}",
                                                           app.editors().root().generic_string()));
            return;
        }
        for (const auto& editor : editors) {
            std::string platforms;
            for (const auto& platform : editor.manifest.platforms())
                platforms += (platforms.empty() ? "" : ", ") + st::platformDisplayName(platform);
            if (editor.manifest.library()) platforms = tr("Library");
            const auto update = std::ranges::find_if(updates, [&](const st::EditorUpdate& u) { return u.editor->id == editor.manifest.id; });
            const bool hasUpdate = update != updates.end();
            // An arrow and the new version mark an update, so the row needs no extra words.
            const std::string value = hasUpdate ? std::string(ui::glyph("â", "^")) + " v" + update->editor->version
                                                : "v" + editor.manifest.version;
            if (ui::row(editor.manifest.id.c_str(), editor.manifest.name + "   " + platforms, value,
                        hasUpdate ? ui::color::Accent : ui::color::TextDim)
                    .activated)
                app.push(makeEditorDetailScreen(editor.manifest.id));
        }
        for (const auto& item : broken) {
            if (ui::row(item.directory.string().c_str(), item.directory.filename().string(), tr("Broken"), ui::color::Danger).activated)
                app.showMessage(tr("Editor could not be loaded"), item.error);
        }
    }

    void drawRepos(App& app) {
        if (!app.modalOpen()) {
            if (ui::pressed(ui::Button::X)) app.refreshRepos(true);
            if (ui::pressed(ui::Button::Y)) addRepo(app);
        }
        if (ui::row("##add", tr("Add repository..."), "", ui::color::Accent).activated) addRepo(app);
        auto& repos = app.settings().repos;
        if (repos.empty()) {
            ui::dimText(tr("Repositories list editors you can install. Anyone can host one: it is a repo.json file on any web server."));
            return;
        }
        for (const auto& repo : repos) {
            const auto source = std::ranges::find(app.catalog().repos(), repo.url, &st::RepoSource::url);
            const bool loaded = source != app.catalog().repos().end();
            const auto status = std::ranges::find(app.repoStatuses(), repo.url, &st::RepoStatus::url);
            std::string name = loaded && !source->index.name.empty() ? source->index.name : repo.url;
            std::string value;
            ImU32 valueColor = ui::color::TextDim;
            if (!repo.enabled) value = tr("Disabled");
            else if (status != app.repoStatuses().end() && !status->error.empty() && !loaded) {
                value = tr("Unreachable");
                valueColor = ui::color::Danger;
            } else if (loaded) {
                value = trf("{} editors", source->index.editors.size());
                if (status != app.repoStatuses().end() && status->fromCache) value += tr(" (offline copy)");
            } else {
                value = tr("Not loaded");
            }
            if (ui::row(repo.url.c_str(), name, value, valueColor).activated) app.push(makeRepoScreen(repo.url));
        }
    }

    static void addRepo(App& app) {
        app.promptText({tr("Repository URL (repo.json)"), "https://", TextKind::Url, 512}, [](App& app, std::string url) {
            while (!url.empty() && url.back() == ' ') url.pop_back();
            if (!url.starts_with("https://") && !url.starts_with("http://")) {
                app.toast(tr("The URL must start with https://"), true);
                return;
            }
            auto& repos = app.settings().repos;
            if (std::ranges::find(repos, url, &st::RepoEntry::url) != repos.end()) {
                app.toast(tr("That repository is already added"), true);
                return;
            }
            repos.push_back({url, true});
            app.saveSettings();
            app.refreshRepos(true);
        });
    }

    static st::SaveLocation* findLocation(App& app, const std::string& path) {
        auto& locations = app.settings().saveLocations;
        auto found = std::ranges::find(locations, path, &st::SaveLocation::path);
        return found == locations.end() ? nullptr : &*found;
    }

    static void updateLocation(App& app, const std::string& path, const std::function<void(st::SaveLocation&)>& change) {
        auto* location = findLocation(app, path);
        if (!location) return;
        change(*location);
        app.saveSettings();
        app.applySaveFolders();
        app.reloadTitles();
    }

    static void addLocation(App& app) {
        auto add = [](App& app, const std::filesystem::path& path) { app.addSaveLocation(path); };
        std::vector<std::pair<std::string, std::function<void(App&)>>> choices;
        if (app.system().hasNativePicker()) {
            // The system dialog picks either files or folders, never both.
            choices.push_back({tr("Pick a file..."), [add](App& app) {
                pickPath(app, tr("Pick a save file"), FilePick::File, defaultBrowseFolder(), add);
            }});
            choices.push_back({tr("Pick a folder..."), [add](App& app) {
                pickPath(app, tr("Pick the folder that holds the save files"), FilePick::Folder, defaultBrowseFolder(), add);
            }});
        } else {
            choices.push_back({tr("Browse..."), [add](App& app) {
                pickPath(app, tr("Pick a save file or folder"), FilePick::Either, defaultBrowseFolder(), add);
            }});
        }
        choices.push_back({tr("Type a path..."), [](App& app) { typeLocation(app); }});
        app.choose(tr("Add a save"), tr("Pick the save file, or the folder that holds the save files."), std::move(choices));
    }

    static void typeLocation(App& app) {
        app.promptText({tr("Save file or folder"), "", TextKind::Text, 512}, [](App& app, std::string text) {
            while (!text.empty() && (text.back() == ' ' || text.back() == '"')) text.pop_back();
            while (!text.empty() && (text.front() == ' ' || text.front() == '"')) text.erase(text.begin());
            if (text.empty()) return;
            const auto path = st::pathFromUtf8(text);
            std::error_code error;
            if (!std::filesystem::exists(path, error)) {
                app.toast(tr("Nothing found at that path"), true);
                return;
            }
            app.addSaveLocation(path);
        });
    }

    static void editLocation(App& app, const st::SaveLocation& location) {
        const std::string path = location.path;
        const std::string name = location.name;
        const std::string titleId = location.titleId;
        app.choose(location.name, location.path, {
            {tr("Rename"), [path, name](App& app) {
                app.promptText({tr("Name"), name, TextKind::Text, 128}, [path](App& app, std::string text) {
                    if (!text.empty()) updateLocation(app, path, [&](st::SaveLocation& l) { l.name = text; });
                });
            }},
            {tr("Platform"), [path](App& app) {
                std::vector<std::pair<std::string, std::function<void(App&)>>> choices;
                for (const char* platform : {"pc", "switch", "switch2", "3ds", "nds", "gba"})
                    choices.emplace_back(st::platformDisplayName(platform), [path, platform](App& app) {
                        updateLocation(app, path, [&](st::SaveLocation& l) { l.platform = platform; });
                    });
                app.choose(tr("Platform"), tr("Editors are matched by platform and game id."), std::move(choices));
            }},
            {tr("Game"), [path, titleId](App& app) {
                std::vector<std::pair<std::string, std::function<void(App&)>>> choices;
                for (const auto& game : app.knownGames()) {
                    const auto label = (game.name.empty() ? game.titleId : game.name) + "  (" + st::platformDisplayName(game.platform) + ")";
                    choices.emplace_back(label, [path, game](App& app) {
                        updateLocation(app, path, [&](st::SaveLocation& l) {
                            l.platform = game.platform;
                            l.titleId = game.titleId;
                            if (!game.name.empty()) l.name = game.name;
                        });
                    });
                }
                choices.emplace_back(tr("Enter a title id..."), [path, titleId](App& app) {
                    app.promptText({tr("Title id (16 hex digits)"), titleId, TextKind::Text, 16}, [path](App& app, std::string text) {
                        if (!text.empty() && !st::isValidTitleId(text)) {
                            app.toast(tr("A title id is 16 hex digits"), true);
                            return;
                        }
                        updateLocation(app, path, [&](st::SaveLocation& l) { l.titleId = st::normalizeTitleId(text); });
                    });
                });
                choices.emplace_back(tr("None"), [path](App& app) {
                    updateLocation(app, path, [](st::SaveLocation& l) { l.titleId.clear(); });
                });
                app.choose(tr("Game"), tr("The save opens with the editors for this game."), std::move(choices));
            }},
            {tr("Remove"), [path](App& app) {
                std::erase_if(app.settings().saveLocations, [&](const st::SaveLocation& l) { return l.path == path; });
                app.saveSettings();
                app.applySaveFolders();
                app.reloadTitles();
            }},
        });
    }

    void drawSettings(App& app) {
        auto& settings = app.settings();
        bool changed = false;
        ui::sectionTitle(tr("Saving"));
        {
            const ImVec2 start = ImGui::GetCursorScreenPos();
            const float width = ImGui::GetContentRegionAvail().x;
            if (ui::row("##autobackup", tr("Back up before writing a save")).activated) {
                settings.autoBackup = !settings.autoBackup;
                changed = true;
            }
            ui::toggleSwitch(ImVec2(start.x + width - ImGui::GetFontSize() * 0.6f, start.y), ui::rowHeight(), settings.autoBackup);
        }
        {
            const auto result = ui::row("##keep", tr("Backups kept per save"), ui::stepper(std::to_string(settings.backupsToKeep)));
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

        ui::sectionTitle(tr("Save file folders (GBA, DS)"));
        {
            const bool defaults = settings.saveFolders.empty();
            const auto folders = defaults ? app.system().defaultSaveFolders() : settings.saveFolders;
            for (std::size_t i = 0; i < folders.size(); ++i) {
                if (ui::row(("##folder" + std::to_string(i)).c_str(), folders[i], defaults ? tr("default") : tr("Remove"),
                            defaults ? ui::color::TextDim : ui::color::Danger)
                        .activated &&
                    !defaults) {
                    settings.saveFolders.erase(settings.saveFolders.begin() + static_cast<std::ptrdiff_t>(i));
                    changed = true;
                    app.applySaveFolders();
                    app.reloadTitles();
                }
            }
            if (ui::row("##addfolder", tr("Add folder..."), "", ui::color::Accent).activated) {
                pickPath(app, tr("Pick a folder with save files"), FilePick::Folder, defaultBrowseFolder(),
                                         [folders](App& app, const std::filesystem::path& path) {
                                             auto& list = app.settings().saveFolders;
                                             if (list.empty()) list = folders;
                                             list.push_back(st::pathToUtf8(path));
                                             app.saveSettings();
                                             app.applySaveFolders();
                                             app.reloadTitles();
                                         });
            }
        }

        ui::sectionTitle(tr("Added saves"));
        {
            const auto& locations = settings.saveLocations;
            for (std::size_t i = 0; i < locations.size(); ++i) {
                const auto& location = locations[i];
                if (ui::row(("##location" + std::to_string(i)).c_str(), location.name, location.path, ui::color::TextDim).activated) {
                    editLocation(app, location);
                    break;
                }
            }
            const std::string label = app.system().acceptsDroppedFiles() ? tr("Add a save file or folder (or drop it here)...") : tr("Add a save file or folder...");
            if (ui::row("##addlocation", label, "", ui::color::Accent).activated) addLocation(app);
        }

        ui::sectionTitle(tr("Display"));
        {
            static constexpr float sizes[] = {0.85f, 1.0f, 1.15f, 1.3f};
            static constexpr const char* names[] = {tr_noop("Small"), tr_noop("Normal"), tr_noop("Large"), tr_noop("Larger")};
            int index = 1;
            for (int i = 0; i < 4; ++i)
                if (std::abs(sizes[i] - settings.uiScale) < 0.01f) index = i;
            const auto result = ui::row("##scale", tr("Interface size"), ui::stepper(tr(names[index])));
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
                {"auto", tr_noop("System")}, {"en", "English"}, {"fr", "Français"}};
            std::size_t index = 0;
            for (std::size_t i = 0; i < languages.size(); ++i)
                if (languages[i].first == settings.language) index = i;
            const auto result = ui::row("##lang", tr("Language"), ui::stepper(tr(languages[index].second)));
            const int step = ui::horizontalInput(result.id, result.focused);
            std::size_t next = index;
            if (step > 0 || result.activated) next = (index + 1) % languages.size();
            if (step < 0) next = (index + languages.size() - 1) % languages.size();
            if (next != index) {
                settings.language = languages[next].first;
                changed = true;
                app.applyLanguage();
            }
        }

        ui::sectionTitle(tr("Updates"));
        {
            const ImVec2 start = ImGui::GetCursorScreenPos();
            const float width = ImGui::GetContentRegionAvail().x;
            if (ui::row("##checkstart", tr("Check for SaveTinker updates on start")).activated) {
                settings.checkUpdatesOnStart = !settings.checkUpdatesOnStart;
                changed = true;
            }
            ui::toggleSwitch(ImVec2(start.x + width - ImGui::GetFontSize() * 0.6f, start.y), ui::rowHeight(), settings.checkUpdatesOnStart);
        }
        if (ui::row("##checknow", tr("Check for updates now"), app.updater().configured() ? "" : tr("Not available in this build")).activated) {
            if (app.updater().configured()) app.checkAppUpdate(false);
            else app.toast(tr("This build has no update source"), true);
        }
        if (ui::row("##repos", tr("Refresh repositories")).activated) app.refreshRepos(true);

        ui::sectionTitle(tr("About"));
        ui::row("##version", tr("Version"), SAVETINKER_VERSION);
        ui::row("##data", tr("Data folder"), app.platform().dataRoot().generic_string());
        if (ui::row("##licenses", tr("Open source licenses")).activated)
            app.showMessage(tr("Open source licenses"),
                            "Lua 5.4 (MIT), Dear ImGui (MIT), nlohmann/json (MIT), miniz (MIT), QR Code generator by Nayuki (MIT), stb_image (public domain), doctest (MIT), SDL2 (zlib), libcurl (curl license), libnx (ISC).");
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

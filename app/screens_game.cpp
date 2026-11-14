#include <algorithm>
#include <format>

#include "app.hpp"
#include "i18n.hpp"
#include "save_flow.hpp"
#include "savetinker/manifest.hpp"
#include "ui.hpp"

namespace app {

namespace {

class GameScreen : public Screen {
public:
    explicit GameScreen(std::string titleId) : titleId_(std::move(titleId)) {}

    std::string title() const override { return name_; }

    std::vector<Hint> hints(const App&) const override {
        return {{"A", tr("Select")}, {"Y", tr("Back up all")}, {"B", tr("Back")}};
    }

    void draw(App& app) override {
        const auto* entry = app.findTitle(titleId_);
        if (!entry) {
            ui::emptyState(tr("This game has no saves anymore"));
            return;
        }
        name_ = entry->info.name;
        if (!app.modalOpen() && ui::pressed(ui::Button::Y)) backUpAll(app, *entry);

        drawInfo(app, *entry);
        const auto running = app.platform().runningTitleId();
        if (running && (running->empty() || *running == titleId_))
            ui::wrappedText(tr("This game is running. Close it from the HOME menu before editing or restoring."), ui::color::Warning);

        ui::sectionTitle(tr("Saves"));
        for (const auto& save : entry->saves) {
            const std::string id = save.slotName();
            if (ui::row(id.c_str(), app.saveLabel(save), ui::formatSize(save.size)).activated) {
                selected_ = save;
                ImGui::OpenPopup("##saveactions");
            }
        }
        drawSaveActions(app, *entry);

        auto installed = app.editors().forGame(entry->info.platform, titleId_);
        const auto generic = std::ranges::stable_partition(installed, [&](const st::InstalledEditorInfo& editor) {
            return !editor.manifest.findGame(entry->info.platform, titleId_)->generic();
        });
        const auto genericCount = static_cast<std::size_t>(std::ranges::distance(generic));
        ui::sectionTitle(tr("Editors"));
        if (installed.size() == genericCount)
            ui::dimText(tr("No editor installed for this game. Use the hex editor or look in your repositories."));
        for (std::size_t i = 0; i < installed.size(); ++i) {
            const auto& editor = installed[i];
            if (i == installed.size() - genericCount) {
                ui::sectionTitle(tr("Generic editors"));
                ui::dimText(tr("Made for a game engine or save format rather than this game. They check the save when opened."));
            }
            const auto* game = editor.manifest.findGame(entry->info.platform, titleId_);
            const bool supported = !game || entry->info.displayVersion.empty() || game->supportsVersion(entry->info.displayVersion);
            if (ui::row(("ed-" + editor.manifest.id).c_str(), editor.manifest.name,
                        supported ? "v" + editor.manifest.version : trf("Not tested with v{}", entry->info.displayVersion),
                        supported ? ui::color::TextDim : ui::color::Warning)
                    .activated)
                app.push(makeEditorDetailScreen(editor.manifest.id));
        }
        if (ui::row("##creator", tr("Create an editor"), hasEditorDraft(app, titleId_)
                        ? trf("draft, {} fields", editorDraft(app, titleId_).fields.size()) : "").activated)
            app.push(makeDraftScreen(titleId_));

        const auto matches = app.catalog().findEditors(entry->info.platform, titleId_, entry->info.displayVersion);
        if (!matches.empty()) {
            ui::sectionTitle(tr("Available in your repositories"));
            for (const auto& match : matches) {
                const std::string id = match.repo->url + "/" + match.editor->id;
                std::string value = match.versionSupported ? match.repo->index.name : trf("other version  \xC2\xB7  {}", match.repo->index.name);
                if (match.editor->needsNewerApp()) value = tr("needs a newer SaveTinker");
                else if (match.editor->findGame(entry->info.platform, titleId_)->generic()) value = trf("generic  \xC2\xB7  {}", value);
                if (ui::row(id.c_str(), match.editor->name + "  v" + match.editor->version, value).activated)
                    app.push(makeRepoEditorScreen(match.repo->url, match.editor->id));
            }
        } else if (ui::row("##search", tr("Search repositories again"), "", ui::color::Accent).activated) {
            app.refreshRepos(true);
        }
    }

private:
    void drawInfo(App& app, const TitleEntry& entry) {
        const float iconSize = ImGui::GetFontSize() * 4.5f;
        ui::iconTile(app.textures().get(titleId_, entry.info.icon), entry.info.name, iconSize);
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.2f));
        ImGui::TextUnformatted(entry.info.name.c_str());
        if (!entry.info.publisher.empty()) ui::dimText(entry.info.publisher);
        std::string detail = st::platformDisplayName(entry.info.platform) + "  \xC2\xB7  " + titleId_;
        if (!entry.info.displayVersion.empty()) detail += "  \xC2\xB7  v" + entry.info.displayVersion;
        ui::dimText(detail);
        ImGui::EndGroup();
    }

    void drawSaveActions(App& app, const TitleEntry& entry) {
        ImGui::SetNextWindowPos(ImGui::GetIO().DisplaySize * 0.5f, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 24, 0));
        if (!ImGui::BeginPopup("##saveactions")) return;
        ui::sectionTitle(app.saveLabel(selected_));
        for (const auto& editor : app.editors().forGame(entry.info.platform, titleId_)) {
            if (ui::row(("open-" + editor.manifest.id).c_str(), trf("Edit with {}", editor.manifest.name)).activated) {
                ImGui::CloseCurrentPopup();
                const auto* game = editor.manifest.findGame(entry.info.platform, titleId_);
                const auto& version = entry.info.displayVersion;
                if (game && !version.empty() && !game->supportsVersion(version)) {
                    std::string supported;
                    for (const auto& pattern : game->versions) supported += (supported.empty() ? "" : ", ") + pattern;
                    app.confirm(tr("Untested game version"),
                                trf("{} was made for version {} and this game is version {}. It may misread the save. A backup is made before anything is written.",
                                    editor.manifest.name, supported, version),
                                tr("Open anyway"), [save = selected_, editor](App& app) { app.push(makeEditorScreen(app, save, editor)); });
                } else {
                    app.push(makeEditorScreen(app, selected_, editor));
                }
            }
        }
        if (ui::row("##hex", tr("Hex editor")).activated) {
            ImGui::CloseCurrentPopup();
            app.push(makeHexScreen(app, selected_));
        }
        if (ui::row("##backup", tr("Back up now")).activated) {
            ImGui::CloseCurrentPopup();
            backUp(app, selected_, entry.info.name);
        }
        const auto count = app.backups().list(titleId_, selected_.slotName()).size();
        if (ui::row("##backups", tr("Backups"), std::to_string(count)).activated) {
            ImGui::CloseCurrentPopup();
            app.push(makeBackupsScreen(selected_));
        }
        if (ui::pressed(ui::Button::B)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    static void backUp(App& app, const st::SaveInfo& save, const std::string& name) {
        auto mount = app.platform().mount(save);
        if (!mount) {
            app.showMessage(tr("Cannot open the save"), mount.error());
            return;
        }
        auto record = app.backups().create(**mount, save, name, "manual");
        if (record) app.toast(trf("Backed up {}", ui::formatSize(record->totalSize())));
        else app.showMessage(tr("Backup failed"), record.error());
    }

    void backUpAll(App& app, const TitleEntry& entry) {
        for (const auto& save : entry.saves) backUp(app, save, entry.info.name);
    }

    std::string titleId_;
    std::string name_ = tr("Game");
    st::SaveInfo selected_;
};

class BackupsScreen : public Screen {
public:
    explicit BackupsScreen(st::SaveInfo save) : save_(std::move(save)) {}

    std::string title() const override { return tr("Backups"); }
    std::vector<Hint> hints(const App&) const override { return {{"A", tr("Select")}, {"B", tr("Back")}}; }

    void draw(App& app) override {
        const auto records = app.backups().list(save_.titleId, save_.slotName());
        ui::dimText(app.saveLabel(save_) + "  \xC2\xB7  " + save_.titleId);
        if (records.empty()) {
            ui::emptyState(tr("No backups yet"), tr("Backups are made automatically before SaveTinker writes a save."));
            return;
        }
        for (const auto& record : records) {
            const std::string label = record.createdAt + "   " + record.reason;
            if (ui::row(record.directory.string().c_str(), label, ui::formatSize(record.totalSize())).activated) {
                selected_ = record;
                ImGui::OpenPopup("##backupactions");
            }
        }
        ImGui::SetNextWindowPos(ImGui::GetIO().DisplaySize * 0.5f, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 22, 0));
        if (ImGui::BeginPopup("##backupactions")) {
            ui::sectionTitle(selected_.createdAt);
            if (ui::row("##restore", tr("Restore this backup")).activated) {
                ImGui::CloseCurrentPopup();
                auto record = selected_;
                auto save = save_;
                app.confirm(tr("Restore backup?"), trf("The current save will be replaced by the backup from {}. A backup of the current save is made first.",
                                                       record.createdAt),
                            tr("Restore"), [record, save](App& app) {
                                auto mount = app.platform().mount(save);
                                if (!mount) return app.showMessage(tr("Cannot open the save"), mount.error());
                                if (auto safety = app.backups().create(**mount, save, record.titleName, tr("before restore")); !safety)
                                    return app.showMessage(tr("Restore cancelled"), trf("Could not back up the current save: {}", safety.error()));
                                auto restored = app.backups().restore(record, **mount);
                                if (restored) app.toast(tr("Backup restored"));
                                else app.showMessage(tr("Restore failed"), restored.error());
                            });
            }
            if (ui::row("##verify", tr("Check integrity")).activated) {
                ImGui::CloseCurrentPopup();
                auto verified = app.backups().verify(selected_);
                if (verified) app.toast(tr("Backup is intact"));
                else app.showMessage(tr("Backup is damaged"), verified.error());
            }
            if (ui::row("##delete", tr("Delete"), "", ui::color::Danger).activated) {
                ImGui::CloseCurrentPopup();
                auto record = selected_;
                app.confirm(tr("Delete backup?"), tr("This cannot be undone."), tr("Delete"), [record](App& app) {
                    if (app.backups().remove(record)) app.toast(tr("Backup deleted"));
                });
            }
            if (ui::pressed(ui::Button::B)) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

private:
    st::SaveInfo save_;
    st::BackupRecord selected_;
};

}

std::unique_ptr<Screen> makeGameScreen(const std::string& titleId) {
    return std::make_unique<GameScreen>(titleId);
}

std::unique_ptr<Screen> makeBackupsScreen(const st::SaveInfo& save) {
    return std::make_unique<BackupsScreen>(save);
}

}

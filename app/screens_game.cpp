#include <format>

#include "app.hpp"
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
        return {{"A", "Select"}, {"Y", "Back up all"}, {"B", "Back"}};
    }

    void draw(App& app) override {
        const auto* entry = app.findTitle(titleId_);
        if (!entry) {
            ui::emptyState("This game has no saves anymore");
            return;
        }
        name_ = entry->info.name;
        if (!app.modalOpen() && ui::pressed(ui::Button::Y)) backUpAll(app, *entry);

        drawInfo(app, *entry);
        const auto running = app.platform().runningTitleId();
        if (running && (running->empty() || *running == titleId_))
            ui::wrappedText("This game is running. Close it from the HOME menu before editing or restoring.", ui::color::Warning);

        ui::sectionTitle("Saves");
        for (const auto& save : entry->saves) {
            const std::string id = save.slotName();
            if (ui::row(id.c_str(), app.saveLabel(save), ui::formatSize(save.size)).activated) {
                selected_ = save;
                ImGui::OpenPopup("##saveactions");
            }
        }
        drawSaveActions(app, *entry);

        const auto installed = app.editors().forGame(entry->info.platform, titleId_);
        ui::sectionTitle("Editors");
        if (installed.empty()) ui::dimText("No editor installed for this game. Use the hex editor or look in your repositories.");
        for (const auto& editor : installed) {
            const auto* game = editor.manifest.findGame(entry->info.platform, titleId_);
            const bool supported = !game || entry->info.displayVersion.empty() || game->supportsVersion(entry->info.displayVersion);
            if (ui::row(("ed-" + editor.manifest.id).c_str(), editor.manifest.name,
                        supported ? "v" + editor.manifest.version : "Not tested with v" + entry->info.displayVersion,
                        supported ? ui::color::TextDim : ui::color::Warning)
                    .activated)
                app.push(makeEditorDetailScreen(editor.manifest.id));
        }
        if (ui::row("##creator", "Create an editor", hasEditorDraft(app, titleId_)
                        ? std::format("draft, {} fields", editorDraft(app, titleId_).fields.size()) : "").activated)
            app.push(makeDraftScreen(titleId_));

        const auto matches = app.catalog().findEditors(entry->info.platform, titleId_, entry->info.displayVersion);
        if (!matches.empty()) {
            ui::sectionTitle("Available in your repositories");
            for (const auto& match : matches) {
                const std::string id = match.repo->url + "/" + match.editor->id;
                const std::string value = (match.versionSupported ? "" : "other version  \xC2\xB7  ") + match.repo->index.name;
                if (ui::row(id.c_str(), match.editor->name + "  v" + match.editor->version, value).activated)
                    app.push(makeRepoEditorScreen(match.repo->url, match.editor->id));
            }
        } else if (ui::row("##search", "Search repositories again", "", ui::color::Accent).activated) {
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
            if (ui::row(("open-" + editor.manifest.id).c_str(), "Edit with " + editor.manifest.name).activated) {
                ImGui::CloseCurrentPopup();
                app.push(makeEditorScreen(app, selected_, editor));
            }
        }
        if (ui::row("##hex", "Hex editor").activated) {
            ImGui::CloseCurrentPopup();
            app.push(makeHexScreen(app, selected_));
        }
        if (ui::row("##backup", "Back up now").activated) {
            ImGui::CloseCurrentPopup();
            backUp(app, selected_, entry.info.name);
        }
        const auto count = app.backups().list(titleId_, selected_.slotName()).size();
        if (ui::row("##backups", "Backups", std::to_string(count)).activated) {
            ImGui::CloseCurrentPopup();
            app.push(makeBackupsScreen(selected_));
        }
        if (ui::pressed(ui::Button::B)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    static void backUp(App& app, const st::SaveInfo& save, const std::string& name) {
        auto mount = app.platform().mount(save);
        if (!mount) {
            app.showMessage("Cannot open the save", mount.error());
            return;
        }
        auto record = app.backups().create(**mount, save, name, "manual");
        if (record) app.toast("Backed up " + ui::formatSize(record->totalSize()));
        else app.showMessage("Backup failed", record.error());
    }

    void backUpAll(App& app, const TitleEntry& entry) {
        for (const auto& save : entry.saves) backUp(app, save, entry.info.name);
    }

    std::string titleId_;
    std::string name_ = "Game";
    st::SaveInfo selected_;
};

class BackupsScreen : public Screen {
public:
    explicit BackupsScreen(st::SaveInfo save) : save_(std::move(save)) {}

    std::string title() const override { return "Backups"; }
    std::vector<Hint> hints(const App&) const override { return {{"A", "Select"}, {"B", "Back"}}; }

    void draw(App& app) override {
        const auto records = app.backups().list(save_.titleId, save_.slotName());
        ui::dimText(app.saveLabel(save_) + "  \xC2\xB7  " + save_.titleId);
        if (records.empty()) {
            ui::emptyState("No backups yet", "Backups are made automatically before SaveTinker writes a save.");
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
            if (ui::row("##restore", "Restore this backup").activated) {
                ImGui::CloseCurrentPopup();
                auto record = selected_;
                auto save = save_;
                app.confirm("Restore backup?", "The current save will be replaced by the backup from " + record.createdAt +
                                                   ". A backup of the current save is made first.",
                            "Restore", [record, save](App& app) {
                                auto mount = app.platform().mount(save);
                                if (!mount) return app.showMessage("Cannot open the save", mount.error());
                                if (auto safety = app.backups().create(**mount, save, record.titleName, "before restore"); !safety)
                                    return app.showMessage("Restore cancelled", "Could not back up the current save: " + safety.error());
                                auto restored = app.backups().restore(record, **mount);
                                if (restored) app.toast("Backup restored");
                                else app.showMessage("Restore failed", restored.error());
                            });
            }
            if (ui::row("##verify", "Check integrity").activated) {
                ImGui::CloseCurrentPopup();
                auto verified = app.backups().verify(selected_);
                if (verified) app.toast("Backup is intact");
                else app.showMessage("Backup is damaged", verified.error());
            }
            if (ui::row("##delete", "Delete", "", ui::color::Danger).activated) {
                ImGui::CloseCurrentPopup();
                auto record = selected_;
                app.confirm("Delete backup?", "This cannot be undone.", "Delete", [record](App& app) {
                    if (app.backups().remove(record)) app.toast("Backup deleted");
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

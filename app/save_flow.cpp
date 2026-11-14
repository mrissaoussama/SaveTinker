#include "save_flow.hpp"

#include <format>
#include <map>

#include "i18n.hpp"
#include "ui.hpp"

namespace app {

std::expected<st::Document, std::string> loadSave(App& app, const st::SaveInfo& save) {
    auto mount = app.platform().mount(save);
    if (!mount) return std::unexpected(mount.error());
    try {
        return st::loadDocument(**mount);
    } catch (const std::exception& error) {
        return std::unexpected(trf("cannot read the save: {}", error.what()));
    }
}

std::expected<void, std::string> writeSave(App& app, SaveTarget& target, const st::Document& document, const std::string& reason) {
    auto mount = app.platform().mount(target.save);
    if (!mount) return std::unexpected(mount.error());
    if (app.settings().autoBackup && !target.backedUp) {
        auto record = app.backups().create(**mount, target.save, target.titleName, reason);
        if (!record) return std::unexpected(trf("backup failed, nothing was written: {}", record.error()));
        target.backedUp = true;
        app.backups().prune(target.save.titleId, target.save.slotName(), static_cast<std::size_t>(app.settings().backupsToKeep));
    }
    try {
        st::storeDocument(document, **mount);
    } catch (const std::exception& error) {
        return std::unexpected(trf("write failed: {}", error.what()));
    }
    return {};
}

bool drawReviewChanges(App& app, const st::Document& document, const std::function<void()>& onConfirm) {
    bool open = true;
    ImGui::SetNextWindowPos(ImGui::GetIO().DisplaySize * 0.5f, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 30, ImGui::GetFontSize() * 20));
    if (!ImGui::IsPopupOpen(trc("Review changes"))) ImGui::OpenPopup(trc("Review changes"));
    if (ImGui::BeginPopupModal(trc("Review changes"), nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        const auto ranges = document.changesSinceLoad();
        std::map<std::string, std::pair<std::size_t, std::size_t>> perFile;
        for (const auto& range : ranges) {
            perFile[range.file].first += 1;
            perFile[range.file].second += range.length;
        }
        ImGui::BeginChild("##changes", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 1.4f));
        if (ranges.empty()) {
            ui::dimText(tr("The save matches what was loaded. Writing it again is harmless."));
        }
        for (const auto& [file, totals] : perFile) {
            ImGui::TextUnformatted(file.c_str());
            ImGui::SameLine();
            ui::dimText(trf("{} bytes in {} places", totals.second, totals.first));
        }
        const auto labels = document.appliedLabels();
        if (!labels.empty()) {
            ui::sectionTitle(tr("Edits"));
            for (const auto& label : labels) ImGui::BulletText("%s", label.c_str());
        }
        if (app.settings().autoBackup) ui::dimText(tr("\nA backup is made before the first write."));
        else ui::wrappedText(tr("\nAutomatic backups are off in Settings."), ui::color::Warning);
        ImGui::EndChild();

        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        if (ui::primaryButton(trc("Write save"))) {
            ImGui::CloseCurrentPopup();
            open = false;
            onConfirm();
        }
        ImGui::SameLine();
        if (ImGui::Button(trc("Cancel")) || ui::pressed(ui::Button::B)) {
            ImGui::CloseCurrentPopup();
            open = false;
        }
        ImGui::EndPopup();
    }
    return open;
}

}

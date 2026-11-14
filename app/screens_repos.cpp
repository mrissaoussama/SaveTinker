#include <algorithm>
#include <format>

#include "app.hpp"
#include "savetinker/checksum.hpp"
#include "savetinker/code_inspect.hpp"
#include "savetinker/manifest.hpp"
#include "savetinker/version.hpp"
#include "ui.hpp"

namespace app {

namespace {

void drawGames(const std::vector<st::GameTarget>& games) {
    for (const auto& game : games) {
        std::string versions;
        for (const auto& version : game.versions) versions += (versions.empty() ? "" : ", ") + version;
        ImGui::BulletText("%s  %s  %s", st::platformDisplayName(game.platform).c_str(),
                          (game.name.empty() ? game.titleId : game.name + " (" + game.titleId + ")").c_str(),
                          versions.empty() ? "" : ("versions " + versions).c_str());
    }
}

void drawLink(const std::string& label, const std::string& url) {
    if (url.empty()) return;
    ui::sectionTitle(label);
    ui::dimText(url);
    ui::qrCode(url, ImGui::GetFontSize() * 6);
}

const st::RepoSource* findRepo(App& app, const std::string& url) {
    auto found = std::ranges::find(app.catalog().repos(), url, &st::RepoSource::url);
    return found == app.catalog().repos().end() ? nullptr : &*found;
}

const st::RepoEditorEntry* findRepoEditor(const st::RepoSource* repo, const std::string& id) {
    if (!repo) return nullptr;
    auto found = std::ranges::find(repo->index.editors, id, &st::RepoEditorEntry::id);
    return found == repo->index.editors.end() ? nullptr : &*found;
}

// Downloads a repo editor, then shows the inspector with an install button.
void downloadAndInspect(App& app, const std::string& repoUrl, const st::RepoEditorEntry& entry) {
    app.runTask("Downloading " + entry.name, [&app, repoUrl, entry](Progress& progress) -> std::function<void(App&)> {
        auto bytes = app.repoManager().downloadEditor(entry, [&progress](std::uint64_t done, std::uint64_t total) {
            progress.done = done;
            progress.total = total;
        });
        if (!bytes) return [error = bytes.error()](App& app) { app.showMessage("Download failed", error); };
        auto package = st::openPackage(*bytes);
        if (!package) return [error = package.error()](App& app) { app.showMessage("This package is not valid", error); };
        if (package->manifest.id != entry.id)
            return [](App& app) { app.showMessage("This package is not valid", "The package id does not match the repository listing."); };
        const auto hash = st::toHex(st::sha256(*bytes));
        return [package = std::move(*package), repoUrl, hash](App& app) mutable {
            std::optional<st::EditorPackage> previous;
            if (auto installed = app.editors().load(package.manifest.id)) previous = std::move(*installed);
            const std::string title = "Review " + package.manifest.name;
            auto copy = package;
            app.push(makeInspectScreen(title, std::move(package), std::move(previous), [copy, repoUrl, hash](App& app) {
                auto installed = app.editors().install(copy, repoUrl, hash);
                if (!installed) return app.showMessage("Install failed", installed.error());
                app.pop();
                app.toast(copy.manifest.name + " installed");
            }));
        };
    });
}

class EditorDetailScreen : public Screen {
public:
    explicit EditorDetailScreen(std::string id) : id_(std::move(id)) {}

    std::string title() const override { return name_; }
    std::vector<Hint> hints(const App&) const override { return {{"A", "Select"}, {"B", "Back"}}; }

    void draw(App& app) override {
        const auto editor = app.editors().find(id_);
        if (!editor) {
            ui::emptyState("Editor removed");
            return;
        }
        const auto& manifest = editor->manifest;
        name_ = manifest.name;
        ImGui::TextUnformatted((manifest.name + "   v" + manifest.version).c_str());
        if (!manifest.description.empty()) ui::dimText(manifest.description);
        if (!manifest.authors.empty()) {
            std::string authors;
            for (const auto& author : manifest.authors) authors += (authors.empty() ? "" : ", ") + author;
            ui::dimText("By " + authors);
        }
        const auto* repo = findRepo(app, editor->repoUrl);
        ui::dimText(editor->repoUrl.empty() ? "Installed manually" : "From " + (repo ? repo->index.name : editor->repoUrl));

        ui::sectionTitle("Actions");
        if (ui::row("##inspect", "Inspect code").activated) {
            if (auto package = app.editors().load(id_))
                app.push(makeInspectScreen(manifest.name, std::move(*package), std::nullopt, nullptr));
        }
        const auto updates = app.catalog().findUpdates({{manifest.id, manifest.version, editor->repoUrl}});
        if (!updates.empty()) {
            const auto& update = updates.front();
            if (ui::row("##update", "Review update", "v" + update.editor->version, ui::color::Accent).activated)
                downloadAndInspect(app, update.repo->url, *update.editor);
        }
        if (ui::row("##remove", "Remove", "", ui::color::Danger).activated) {
            const auto id = id_;
            app.confirm("Remove " + manifest.name + "?", "Backups and saves are not affected.", "Remove", [id](App& app) {
                if (auto removed = app.editors().remove(id); !removed) return app.showMessage("Could not remove", removed.error());
                app.pop();
                app.toast("Editor removed");
            });
        }

        ui::sectionTitle("Games");
        drawGames(manifest.games);
        drawLink("Source", manifest.source);
    }

private:
    std::string id_;
    std::string name_ = "Editor";
};

class RepoScreen : public Screen {
public:
    explicit RepoScreen(std::string url) : url_(std::move(url)) {}

    std::string title() const override { return name_; }
    std::vector<Hint> hints(const App&) const override { return {{"A", "Select"}, {"Y", "Options"}, {"B", "Back"}}; }

    void draw(App& app) override {
        const auto* repo = findRepo(app, url_);
        auto& entries = app.settings().repos;
        auto entry = std::ranges::find(entries, url_, &st::RepoEntry::url);
        if (entry == entries.end()) {
            ui::emptyState("Repository removed");
            return;
        }
        name_ = repo && !repo->index.name.empty() ? repo->index.name : "Repository";
        if (!app.modalOpen() && ui::pressed(ui::Button::Y)) ImGui::OpenPopup("##repooptions");

        ImGui::TextUnformatted(name_.c_str());
        ui::dimText(url_);
        if (repo && !repo->index.maintainer.empty()) ui::dimText("Maintained by " + repo->index.maintainer);
        if (repo && !repo->index.description.empty()) ui::dimText(repo->index.description);
        const auto status = std::ranges::find(app.repoStatuses(), url_, &st::RepoStatus::url);
        if (status != app.repoStatuses().end() && !status->error.empty())
            ui::wrappedText((status->fromCache ? "Showing the offline copy. " : "") + status->error, ui::color::Warning);
        if (!entry->enabled) ui::wrappedText("This repository is disabled.", ui::color::Warning);

        ui::sectionTitle("Editors");
        if (!repo || repo->index.editors.empty()) ui::dimText("No editors listed.");
        if (repo) {
            for (const auto& editor : repo->index.editors) {
                const auto installed = app.editors().find(editor.id);
                std::string state;
                if (installed) {
                    const auto mine = st::Version::parse(installed->manifest.version);
                    const auto theirs = st::Version::parse(editor.version);
                    state = mine && theirs && *theirs > *mine ? "Update" : "Installed";
                } else {
                    state = "v" + editor.version;
                }
                std::string platforms;
                for (const auto& game : editor.games)
                    if (platforms.find(st::platformDisplayName(game.platform)) == std::string::npos)
                        platforms += (platforms.empty() ? "" : ", ") + st::platformDisplayName(game.platform);
                if (ui::row(editor.id.c_str(), editor.name + "   " + platforms, state,
                            state == "Update" ? ui::color::Accent : ui::color::TextDim)
                        .activated)
                    app.push(makeRepoEditorScreen(url_, editor.id));
            }
        }
        if (repo && !repo->index.homepage.empty()) drawLink("Homepage", repo->index.homepage);
        drawLink("Share this repository", url_);
        drawOptions(app, *entry);
    }

private:
    void drawOptions(App& app, st::RepoEntry& entry) {
        ImGui::SetNextWindowPos(ImGui::GetIO().DisplaySize * 0.5f, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 20, 0));
        if (!ImGui::BeginPopup("##repooptions")) return;
        if (ui::row("##refresh", "Refresh").activated) {
            ImGui::CloseCurrentPopup();
            app.refreshRepos(true);
        }
        if (ui::row("##toggle", entry.enabled ? "Disable" : "Enable").activated) {
            ImGui::CloseCurrentPopup();
            entry.enabled = !entry.enabled;
            app.saveSettings();
            app.refreshRepos(false);
        }
        if (ui::row("##remove", "Remove repository", "", ui::color::Danger).activated) {
            ImGui::CloseCurrentPopup();
            const auto url = url_;
            app.confirm("Remove repository?", url + "\n\nInstalled editors stay installed.", "Remove", [url](App& app) {
                std::erase_if(app.settings().repos, [&](const st::RepoEntry& e) { return e.url == url; });
                app.catalog().removeRepo(url);
                app.saveSettings();
                app.pop();
            });
        }
        ImGui::EndPopup();
    }

    std::string url_;
    std::string name_ = "Repository";
};

class RepoEditorScreen : public Screen {
public:
    RepoEditorScreen(std::string repoUrl, std::string id) : repoUrl_(std::move(repoUrl)), id_(std::move(id)) {}

    std::string title() const override { return name_; }
    std::vector<Hint> hints(const App&) const override { return {{"A", "Select"}, {"B", "Back"}}; }

    void draw(App& app) override {
        const auto* repo = findRepo(app, repoUrl_);
        const auto* entry = findRepoEditor(repo, id_);
        if (!entry) {
            ui::emptyState("No longer listed in this repository");
            return;
        }
        name_ = entry->name;
        ImGui::TextUnformatted((entry->name + "   v" + entry->version).c_str());
        if (!entry->description.empty()) ui::dimText(entry->description);
        if (!entry->authors.empty()) {
            std::string authors;
            for (const auto& author : entry->authors) authors += (authors.empty() ? "" : ", ") + author;
            ui::dimText("By " + authors);
        }
        ui::dimText("From " + repo->index.name + (entry->size ? "  \xC2\xB7  " + ui::formatSize(entry->size) : "") +
                    (entry->updated.empty() ? "" : "  \xC2\xB7  updated " + entry->updated));
        if (entry->sha256.empty())
            ui::wrappedText("The repository does not list a checksum for this download.", ui::color::Warning);

        ui::sectionTitle("Actions");
        const auto installed = app.editors().find(id_);
        const std::string action = installed ? (installed->manifest.version == entry->version ? "Reinstall" : "Review update")
                                             : "Review and install";
        if (ui::row("##install", action, installed ? "installed v" + installed->manifest.version : "", ui::color::Accent).activated)
            downloadAndInspect(app, repoUrl_, *entry);
        if (!entry->changelog.empty()) {
            ui::sectionTitle("Changes");
            ui::wrappedText(entry->changelog);
        }
        ui::sectionTitle("Games");
        drawGames(entry->games);
        drawLink("Source", entry->source);
    }

private:
    std::string repoUrl_;
    std::string id_;
    std::string name_ = "Editor";
};

class InspectScreen : public Screen {
public:
    InspectScreen(std::string title, st::EditorPackage package, std::optional<st::EditorPackage> previous,
                  std::function<void(App&)> installAction)
        : title_(std::move(title)), package_(std::move(package)), previous_(std::move(previous)), install_(std::move(installAction)) {
        for (std::size_t i = 0; i < package_.files.size(); ++i)
            if (package_.files[i].path == package_.manifest.entry) fileIndex_ = i;
        for (const auto& file : package_.files)
            if (file.path.ends_with(".lua"))
                for (auto finding : st::summarizeScript(file.text())) findings_.push_back({file.path, std::move(finding)});
    }

    std::string title() const override { return title_; }

    std::vector<Hint> hints(const App&) const override {
        std::vector<Hint> hints = {{"L", "File"}, {"R", "File"}, {"RS", "Scroll"}};
        if (previous_) hints.push_back({"X", showDiff_ ? "Show code" : "Show changes"});
        if (install_) hints.push_back({"+", "Install"});
        hints.push_back({"B", "Back"});
        return hints;
    }

    void draw(App& app) override {
        if (!app.modalOpen()) {
            if (ui::pressed(ui::Button::L)) selectFile(-1);
            if (ui::pressed(ui::Button::R)) selectFile(1);
            if (previous_ && ui::pressed(ui::Button::X)) showDiff_ = !showDiff_;
            if (install_ && ui::pressed(ui::Button::Plus)) {
                auto action = install_;
                action(app);
                return;
            }
        }
        drawSummary();

        const float listWidth = ImGui::GetFontSize() * 11;
        ImGui::BeginChild("##files", ImVec2(listWidth, 0), ImGuiChildFlags_NavFlattened);
        for (std::size_t i = 0; i < package_.files.size(); ++i) {
            const auto& file = package_.files[i];
            const bool changed = previous_ && changedFile(file);
            if (ui::row(file.path.c_str(), file.path, changed ? "\xE2\x97\x8F" : "", ui::color::Modified, i == fileIndex_).activated) {
                fileIndex_ = i;
                scrollReset_ = true;
            }
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##code", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
        scrollWithController();
        const auto& file = package_.files[fileIndex_];
        if (showDiff_ && previous_) drawDiff(file);
        else drawCode(file);
        ImGui::EndChild();
    }

private:
    struct Finding {
        std::string file;
        st::CodeFinding finding;
    };

    void selectFile(int delta) {
        const auto count = static_cast<int>(package_.files.size());
        fileIndex_ = static_cast<std::size_t>((static_cast<int>(fileIndex_) + delta + count) % count);
        scrollReset_ = true;
    }

    bool changedFile(const st::PackageFile& file) const {
        const auto* old = previous_->find(file.path);
        return !old || old->content != file.content;
    }

    void drawSummary() {
        ImGui::TextUnformatted((package_.manifest.name + "   v" + package_.manifest.version).c_str());
        if (previous_) {
            ImGui::SameLine();
            ui::badge("installed v" + previous_->manifest.version, ui::color::TextDim);
        }
        std::size_t writes = 0;
        std::vector<std::string> modules, data;
        for (const auto& [file, finding] : findings_) {
            if (finding.kind == st::CodeFinding::Kind::Writes) ++writes;
            if (finding.kind == st::CodeFinding::Kind::Requires) modules.push_back(finding.detail);
            if (finding.kind == st::CodeFinding::Kind::ReadsData) data.push_back(finding.detail);
        }
        std::string summary = std::format("{} files  \xC2\xB7  writes to the save in {} places", package_.files.size(), writes);
        if (!modules.empty()) summary += std::format("  \xC2\xB7  modules: {}", modules.size());
        if (!data.empty()) summary += std::format("  \xC2\xB7  data files: {}", data.size());
        ui::dimText(summary);
        ui::dimText("Editors run in a sandbox without file, network or system access. They can only change the save being edited, and a backup is made first.");
        ImGui::Spacing();
    }

    void scrollWithController() {
        if (scrollReset_) {
            ImGui::SetScrollY(0);
            scrollReset_ = false;
        }
        const float step = ImGui::GetTextLineHeightWithSpacing() * 3;
        auto held = [](ImGuiKey a, ImGuiKey b) { return ImGui::IsKeyPressed(a, true) || ImGui::IsKeyPressed(b, true); };
        if (held(ImGuiKey_GamepadRStickDown, ImGuiKey_PageDown)) ImGui::SetScrollY(ImGui::GetScrollY() + step * 4);
        if (held(ImGuiKey_GamepadRStickUp, ImGuiKey_PageUp)) ImGui::SetScrollY(ImGui::GetScrollY() - step * 4);
    }

    void drawCode(const st::PackageFile& file) {
        if (!file.path.ends_with(".lua") && !file.path.ends_with(".json") && !file.path.ends_with(".txt") && !file.path.ends_with(".md")) {
            ui::dimText(std::format("Binary file, {}", ui::formatSize(file.content.size())));
            return;
        }
        const auto lines = st::splitLines(file.text());
        const bool lua = file.path.ends_with(".lua");
        st::LuaHighlighter highlighter;
        const float numberWidth = ImGui::CalcTextSize("0000").x;
        auto* draw = ImGui::GetWindowDrawList();
        const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
        std::vector<std::vector<st::Token>> tokens;
        if (lua) {
            tokens.reserve(lines.size());
            for (const auto& line : lines) tokens.push_back(highlighter.line(line));
        }
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(lines.size()), lineHeight);
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                ImVec2 position = ImGui::GetCursorScreenPos();
                const auto number = std::to_string(i + 1);
                draw->AddText(ImVec2(position.x + numberWidth - ImGui::CalcTextSize(number.c_str()).x, position.y), ui::color::TextDim,
                              number.c_str());
                float x = position.x + numberWidth + ImGui::GetFontSize();
                if (lua) {
                    for (const auto& token : tokens[static_cast<std::size_t>(i)]) {
                        const std::string text(token.text);
                        draw->AddText(ImVec2(x, position.y), tokenColor(token.kind), text.c_str());
                        x += ImGui::CalcTextSize(text.c_str()).x;
                    }
                } else {
                    draw->AddText(ImVec2(x, position.y), ui::color::Text, lines[static_cast<std::size_t>(i)].c_str());
                    x += ImGui::CalcTextSize(lines[static_cast<std::size_t>(i)].c_str()).x;
                }
                ImGui::Dummy(ImVec2(x - position.x, lineHeight));
            }
        }
    }

    void drawDiff(const st::PackageFile& file) {
        const auto* old = previous_->find(file.path);
        const auto diff = st::diffLines(old ? old->text() : std::string_view{}, file.text());
        const bool anyChange = std::ranges::any_of(diff, [](const st::DiffLine& l) { return l.kind != st::DiffKind::Same; });
        if (!anyChange) {
            ui::dimText("No changes in this file.");
            return;
        }
        auto* draw = ImGui::GetWindowDrawList();
        const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
        for (const auto& line : diff) {
            const ImVec2 position = ImGui::GetCursorScreenPos();
            const char* marker = line.kind == st::DiffKind::Added ? "+" : line.kind == st::DiffKind::Removed ? "-" : " ";
            const ImU32 tint = line.kind == st::DiffKind::Added     ? ui::color::Success
                               : line.kind == st::DiffKind::Removed ? ui::color::Danger
                                                                    : ui::color::TextDim;
            if (line.kind != st::DiffKind::Same)
                draw->AddRectFilled(position, ImVec2(position.x + ImGui::GetContentRegionAvail().x, position.y + lineHeight),
                                    (tint & 0x00FFFFFF) | (0x30u << 24));
            draw->AddText(position, tint, marker);
            draw->AddText(ImVec2(position.x + ImGui::GetFontSize(), position.y), line.kind == st::DiffKind::Same ? ui::color::TextDim : ui::color::Text,
                          line.text.c_str());
            ImGui::Dummy(ImVec2(ImGui::GetFontSize() + ImGui::CalcTextSize(line.text.c_str()).x, lineHeight));
        }
    }

    static ImU32 tokenColor(st::TokenKind kind) {
        switch (kind) {
        case st::TokenKind::Keyword: return IM_COL32(200, 140, 255, 255);
        case st::TokenKind::Builtin: return IM_COL32(90, 200, 230, 255);
        case st::TokenKind::String: return IM_COL32(150, 210, 120, 255);
        case st::TokenKind::Number: return IM_COL32(240, 170, 100, 255);
        case st::TokenKind::Comment: return IM_COL32(120, 128, 145, 255);
        case st::TokenKind::Plain: return ui::color::Text;
        }
        return ui::color::Text;
    }

    std::string title_;
    st::EditorPackage package_;
    std::optional<st::EditorPackage> previous_;
    std::function<void(App&)> install_;
    std::vector<Finding> findings_;
    std::size_t fileIndex_ = 0;
    bool showDiff_ = false;
    bool scrollReset_ = false;
};

}

std::unique_ptr<Screen> makeEditorDetailScreen(const std::string& editorId) {
    return std::make_unique<EditorDetailScreen>(editorId);
}

std::unique_ptr<Screen> makeRepoScreen(const std::string& repoUrl) {
    return std::make_unique<RepoScreen>(repoUrl);
}

std::unique_ptr<Screen> makeRepoEditorScreen(const std::string& repoUrl, const std::string& editorId) {
    return std::make_unique<RepoEditorScreen>(repoUrl, editorId);
}

std::unique_ptr<Screen> makeInspectScreen(std::string title, st::EditorPackage package, std::optional<st::EditorPackage> previous,
                                          std::function<void(App&)> installAction) {
    return std::make_unique<InspectScreen>(std::move(title), std::move(package), std::move(previous), std::move(installAction));
}

}

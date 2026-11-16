#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "savetinker/backup.hpp"
#include "savetinker/editor_store.hpp"
#include "savetinker/editor_template.hpp"
#include "savetinker/file_saves.hpp"
#include "savetinker/network.hpp"
#include "savetinker/platform.hpp"
#include "savetinker/repository.hpp"
#include "savetinker/settings.hpp"
#include <imgui.h>

#include "system.hpp"
#include "textures.hpp"

namespace app {

class App;

struct Hint {
    std::string button;
    std::string label;
};

class Screen {
public:
    virtual ~Screen() = default;
    virtual std::string title() const = 0;
    virtual void draw(App& app) = 0;
    virtual std::vector<Hint> hints(const App& app) const = 0;
    virtual bool requestClose(App&) { return true; }

    ImGuiID savedFocus = 0;
};

struct TitleEntry {
    st::TitleInfo info;
    std::vector<st::SaveInfo> saves;
};

struct Progress {
    std::atomic<std::uint64_t> done{0};
    std::atomic<std::uint64_t> total{0};
};

class App {
public:
    App(System& system, bool consoleGlyphs);
    ~App();

    void frame();
    bool running() const { return running_; }
    void quit() { running_ = false; }

    System& system() { return system_; }
    st::Platform& platform() { return *platform_; }
    st::Settings& settings() { return settings_; }
    void saveSettings();
    st::EditorStore& editors() { return editors_; }
    st::BackupManager& backups() { return backups_; }
    st::RepoManager& repoManager() { return repoManager_; }
    st::RepoCatalog& catalog() { return catalog_; }
    std::vector<st::RepoStatus>& repoStatuses() { return repoStatuses_; }
    st::AppUpdater& updater() { return updater_; }
    Textures& textures() { return textures_; }

    const std::vector<TitleEntry>& titles() const { return titles_; }
    const TitleEntry* findTitle(const std::string& titleId) const;
    std::string userName(const std::string& userId) const;
    std::string saveLabel(const st::SaveInfo& save) const;
    void reloadTitles();

    void push(std::unique_ptr<Screen> screen);
    void pop();
    void replaceTop(std::unique_ptr<Screen> screen);

    void runTask(std::string label, std::function<std::function<void(App&)>(Progress&)> work);
    bool busy() const { return task_.has_value(); }

    void promptText(TextRequest request, std::function<void(App&, std::string)> onDone);
    void confirm(std::string title, std::string message, std::string confirmLabel, std::function<void(App&)> onConfirm);
    void choose(std::string title, std::string message, std::vector<std::pair<std::string, std::function<void(App&)>>> choices);
    void showMessage(std::string title, std::string message);
    void toast(std::string text, bool error = false);
    // Appends a line to <data>/logs/savetinker.log, kept under 512 KB.
    void log(const std::string& source, const std::string& text);
    std::filesystem::path logPath() const;

    void refreshRepos(bool notify);
    void checkAppUpdate(bool silent);

    std::string buttonGlyph(const std::string& button) const;
    bool consoleGlyphs() const { return consoleGlyphs_; }
    bool modalOpen() const;
    float scale() const { return scale_; }
    std::string language() const;
    void applyLanguage();
    void applySaveFolders();
    void addSaveLocation(const std::filesystem::path& path);
    // Games named by installed editors and by the repository catalog, for linking added saves.
    std::vector<st::GameTarget> knownGames();
    ImVec2 footerTextPosition() const { return footerLeft_; }
    // Width left of the button hints, for status text.
    float footerTextRoom() const { return footerRoom_; }

private:
    void applyStyle();
    void drawHeader(ImDrawList* draw, ImVec2 origin, float width, float height);
    void drawFooter(ImDrawList* draw, ImVec2 origin, float width, float height);
    void drawTask();
    void drawPrompt();
    void drawDialog();
    void drawToasts();
    void handleFooterTaps();
    void handleBack();

    struct Task {
        std::string label;
        std::shared_ptr<Progress> progress;
        std::future<std::function<void(App&)>> result;
    };
    struct Prompt {
        TextRequest request;
        std::function<void(App&, std::string)> onDone;
        std::string buffer;
        bool opened = false;
    };
    struct Dialog {
        std::string title;
        std::string message;
        std::string confirmLabel;
        std::function<void(App&)> onConfirm;
        bool opened = false;
        std::vector<std::pair<std::string, std::function<void(App&)>>> choices;
    };
    struct Toast {
        std::string text;
        bool error;
        double until;
    };

    System& system_;
    bool consoleGlyphs_;
    bool running_ = true;
    bool popupWasOpen_ = false;
    float scale_ = 1.0f;
    float appliedScale_ = 0.0f;
    ImVec2 footerLeft_{};
    float footerRoom_ = 0.0f;
    std::unique_ptr<st::Platform> platform_;
    std::filesystem::path settingsPath_;
    st::Settings settings_;
    st::EditorStore editors_;
    st::BackupManager backups_;
    std::unique_ptr<st::HttpClient> http_;
    st::RepoManager repoManager_;
    st::RepoCatalog catalog_;
    std::vector<st::RepoStatus> repoStatuses_;
    st::AppUpdater updater_;
    Textures textures_;

    std::vector<TitleEntry> titles_;
    std::map<std::string, std::string> userNames_;
    std::vector<std::unique_ptr<Screen>> screens_;
    std::vector<std::unique_ptr<Screen>> retired_;
    std::optional<Task> task_;
    std::optional<Prompt> prompt_;
    std::optional<Dialog> dialog_;
    std::vector<Toast> toasts_;
    struct FooterTarget {
        ImVec2 min;
        ImVec2 max;
        std::string button;
    };
    std::vector<FooterTarget> footerTargets_;
    ImGuiKey releaseFooterKey_ = ImGuiKey_None;
};

enum class FilePick { File, Folder, Either };

std::unique_ptr<Screen> makeMainScreen();
// Lets the user walk the SD card (or the disks on PC) and pick a file or folder.
std::unique_ptr<Screen> makeFileBrowser(std::string title, FilePick pick, std::filesystem::path start,
                                        std::function<void(App&, const std::filesystem::path&)> onPick);
std::filesystem::path defaultBrowseFolder();
// The system's own dialog where there is one (PC), the file browser otherwise.
void pickPath(App& app, std::string title, FilePick pick, std::filesystem::path start,
              std::function<void(App&, const std::filesystem::path&)> onPick);
std::unique_ptr<Screen> makeGameScreen(const std::string& titleId);
std::unique_ptr<Screen> makeEditorScreen(App& app, const st::SaveInfo& save, const st::InstalledEditorInfo& editor);
std::unique_ptr<Screen> makeHexScreen(App& app, const st::SaveInfo& save);
std::unique_ptr<Screen> makeBackupsScreen(const st::SaveInfo& save);
std::unique_ptr<Screen> makeEditorDetailScreen(const std::string& editorId);
std::unique_ptr<Screen> makeRepoScreen(const std::string& repoUrl);
std::unique_ptr<Screen> makeRepoEditorScreen(const std::string& repoUrl, const std::string& editorId);
std::unique_ptr<Screen> makeInspectScreen(std::string title, st::EditorPackage package,
                                          std::optional<st::EditorPackage> previous,
                                          std::function<void(App&)> installAction);

st::EditorTemplate& editorDraft(App& app, const std::string& titleId);
void storeEditorDraft(App& app, const std::string& titleId);
void discardEditorDraft(App& app, const std::string& titleId);
bool hasEditorDraft(App& app, const std::string& titleId);
std::unique_ptr<Screen> makeFieldScreen(const std::string& titleId, st::TemplateField field, st::Bytes content,
                                        std::optional<std::size_t> index = std::nullopt);
std::unique_ptr<Screen> makeDraftScreen(const std::string& titleId);

}

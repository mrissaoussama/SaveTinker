#include <algorithm>
#include <cstdlib>
#include <format>

#include "app.hpp"
#include "i18n.hpp"
#include "native_dialog.hpp"
#include "system.hpp"
#include "savetinker/file_saves.hpp"
#include "ui.hpp"

namespace app {

namespace fs = std::filesystem;

namespace {

struct Entry {
    fs::path path;
    std::string name;
    bool folder = false;
    std::uintmax_t size = 0;
};

std::string lower(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

class FileBrowserScreen : public Screen {
public:
    FileBrowserScreen(std::string title, FilePick pick, fs::path start, std::function<void(App&, const fs::path&)> onPick)
        : title_(std::move(title)), pick_(pick), onPick_(std::move(onPick)) {
        open(start);
    }

    std::string title() const override { return title_; }

    std::vector<Hint> hints(const App&) const override {
        std::vector<Hint> hints = {{"A", tr("Open")}};
        if (pick_ != FilePick::File) hints.push_back({"Y", tr("Pick this folder")});
        hints.push_back({"X", tr("Go to path")});
        hints.push_back({"B", tr("Back")});
        return hints;
    }

    void draw(App& app) override {
        if (!app.modalOpen()) {
            if (pick_ != FilePick::File && ui::pressed(ui::Button::Y) && !current_.empty()) return choose(app, current_);
            if (ui::pressed(ui::Button::X))
                app.promptText({tr("Path"), st::pathToUtf8(current_), TextKind::Text, 512}, [this](App&, std::string text) {
                    if (!text.empty()) open(st::pathFromUtf8(text));
                });
        }
        ui::dimText(current_.empty() ? tr("Drives") : st::pathToUtf8(current_));
        if (!error_.empty()) ui::wrappedText(error_, ui::color::Warning);
        if (!current_.empty() && ui::row("##up", "..", tr("Up")).activated) return open(parentOf(current_));
        for (const auto& entry : entries_) {
            const std::string value = entry.folder ? ui::glyph("\xE2\x80\xBA", ">") : ui::formatSize(entry.size);
            if (ui::row(st::pathToUtf8(entry.path).c_str(), entry.folder ? entry.name + "/" : entry.name, value).activated) {
                if (entry.folder) return open(entry.path);
                if (pick_ != FilePick::Folder) return choose(app, entry.path);
            }
        }
        if (entries_.empty() && error_.empty()) ui::dimText(tr("This folder is empty."));
        ui::wrapNavigation();
    }

    bool requestClose(App& app) override {
        if (!current_.empty() && current_ != start_) {
            open(parentOf(current_));
            return false;
        }
        (void)app;
        return true;
    }

private:
    static fs::path parentOf(const fs::path& path) {
        if (path == path.root_path() || !path.has_relative_path()) return {};
        return path.parent_path();
    }

    void choose(App& app, const fs::path& path) {
        auto callback = onPick_;
        app.pop();
        callback(app, path);
    }

    void open(const fs::path& path) {
        error_.clear();
        entries_.clear();
        if (start_.empty() && !path.empty()) start_ = path;
        current_ = path;
        std::error_code error;
        if (path.empty()) {
            for (const auto& root : roots()) entries_.push_back({root, st::pathToUtf8(root), true, 0});
            return;
        }
        if (!fs::is_directory(path, error)) {
            if (fs::is_regular_file(path, error)) {
                current_ = path.parent_path();
                return open(current_);
            }
            error_ = tr("This folder cannot be opened.");
            return;
        }
        for (fs::directory_iterator it(path, fs::directory_options::skip_permission_denied, error), end; !error && it != end;
             it.increment(error)) {
            Entry entry;
            entry.path = it->path();
            entry.name = st::pathToUtf8(it->path().filename());
            std::error_code statError;
            entry.folder = it->is_directory(statError);
            if (!entry.folder) entry.size = it->file_size(statError);
            if (!entry.name.empty() && entry.name.front() == '.') continue;
            entries_.push_back(std::move(entry));
        }
        if (error) error_ = error.message();
        std::ranges::sort(entries_, [](const Entry& a, const Entry& b) {
            if (a.folder != b.folder) return a.folder;
            return lower(a.name) < lower(b.name);
        });
    }

    static std::vector<fs::path> roots() {
#if defined(__SWITCH__) || defined(__3DS__)
        return {"sdmc:/"};
#elif defined(_WIN32)
        std::vector<fs::path> drives;
        for (char letter = 'A'; letter <= 'Z'; ++letter) {
            const fs::path drive = std::string(1, letter) + ":/";
            std::error_code error;
            if (fs::exists(drive, error)) drives.push_back(drive);
        }
        return drives;
#else
        return {"/"};
#endif
    }

    std::string title_;
    FilePick pick_;
    std::function<void(App&, const fs::path&)> onPick_;
    fs::path start_;
    fs::path current_;
    std::vector<Entry> entries_;
    std::string error_;
};

}

std::filesystem::path defaultBrowseFolder() {
#if defined(__SWITCH__) || defined(__3DS__)
    return "sdmc:/";
#else
    std::error_code error;
    for (const char* variable : {"USERPROFILE", "HOME"})
        if (const char* home = std::getenv(variable); home && fs::is_directory(home, error)) return st::pathFromUtf8(home);
    return fs::current_path(error);
#endif
}

void pickPath(App& app, std::string title, FilePick pick, std::filesystem::path start,
              std::function<void(App&, const std::filesystem::path&)> onPick) {
    if (app.system().hasNativePicker()) {
        if (auto picked = app.system().pickPath(title, pick == FilePick::Folder, start)) {
            if (*picked) onPick(app, **picked);
            return;
        }
    }
    app.push(makeFileBrowser(std::move(title), pick, std::move(start), std::move(onPick)));
}

std::unique_ptr<Screen> makeFileBrowser(std::string title, FilePick pick, std::filesystem::path start,
                                        std::function<void(App&, const std::filesystem::path&)> onPick) {
    return std::make_unique<FileBrowserScreen>(std::move(title), pick, std::move(start), std::move(onPick));
}

}

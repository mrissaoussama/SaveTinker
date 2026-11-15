#include "savetinker/file_saves.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <format>
#include <fstream>
#include <set>

#include "savetinker/checksum.hpp"
#include "savetinker/game_data.hpp"
#include "savetinker/manifest.hpp"

namespace st {

namespace fs = std::filesystem;

namespace {

constexpr std::uintmax_t kMaxSaveSize = 32u << 20;
const std::set<std::string> kSaveExtensions = {".sav", ".srm", ".sa1", ".dsv"};

std::string lower(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string printable(const char* data, std::size_t length) {
    std::string text;
    for (std::size_t i = 0; i < length && data[i]; ++i)
        text.push_back(std::isprint(static_cast<unsigned char>(data[i])) ? data[i] : '?');
    while (!text.empty() && text.back() == ' ') text.pop_back();
    return text;
}

bool isGameCode(const std::string& code) {
    return code.size() == 4 && std::ranges::all_of(code, [](unsigned char c) { return std::isalnum(c); });
}

}

std::optional<RomIdentity> readRomIdentity(const fs::path& rom) {
    std::ifstream stream(rom, std::ios::binary);
    if (!stream) return std::nullopt;
    char header[0xC0] = {};
    stream.read(header, sizeof(header));
    if (stream.gcount() < static_cast<std::streamsize>(sizeof(header))) return std::nullopt;
    const auto extension = lower(rom.extension().string());
    if (extension == ".gba" && static_cast<unsigned char>(header[0xB2]) == 0x96) {
        RomIdentity identity{"gba", printable(header + 0xAC, 4), printable(header + 0xA0, 12)};
        if (isGameCode(identity.gameCode)) return identity;
    }
    if (extension == ".nds" || extension == ".dsi") {
        RomIdentity identity{"nds", printable(header + 0x0C, 4), printable(header, 12)};
        if (isGameCode(identity.gameCode)) return identity;
    }
    return std::nullopt;
}

std::string cartridgeTitleId(const std::string& gameCode) {
    std::string id = "00000000";
    for (unsigned char c : gameCode) id += std::format("{:02X}", c);
    return id;
}

namespace {

bool isHex(const std::string& text) {
    return !text.empty() && std::ranges::all_of(text, [](unsigned char c) { return std::isxdigit(c) != 0; });
}

bool isNumber(const std::string& text) {
    return !text.empty() && std::ranges::all_of(text, [](unsigned char c) { return std::isdigit(c) != 0; });
}

std::uintmax_t folderSize(const fs::path& folder) {
    std::uintmax_t total = 0;
    std::error_code error;
    for (fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, error), end; !error && it != end;
         it.increment(error))
        if (it->is_regular_file(error)) total += it->file_size(error);
    return total;
}

// Folder names that say nothing about the game.
bool isGenericFolder(const std::string& name) {
    static const std::set<std::string> generic = {"save", "saves", "savegames", "saved", "savedata", "save data", "data", "www",
                                                  "local", "locallow", "roaming", "appdata", "backup", "backups", "user",
                                                  "users", "profiles", "profile", "documents", "my games", "steam", "remote"};
    return name.empty() || generic.contains(lower(name)) || isHex(name) ||
           std::ranges::all_of(name, [](unsigned char c) { return std::isdigit(c) || c == '_' || c == '-'; });
}

}

fs::path pathFromUtf8(const std::string& text) {
    return fs::path(std::u8string(text.begin(), text.end()));
}

std::string pathToUtf8(const fs::path& path) {
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}

SaveLocation describeSaveLocation(const fs::path& path) {
    SaveLocation location;
    location.path = pathToUtf8(path);
    std::vector<std::string> parts;
    for (const auto& part : path) parts.push_back(pathToUtf8(part));

    for (std::size_t i = 0; i < parts.size(); ++i) {
        const auto& part = parts[i];
        if (part.size() == 16 && isHex(part) && part != "0000000000000000") {
            location.titleId = normalizeTitleId(part);
            location.platform = location.titleId.starts_with("0004") ? "3ds" : "switch";
        } else if (lower(part) == "userdata" && i + 2 < parts.size() && isNumber(parts[i + 1]) && isNumber(parts[i + 2]) &&
                   parts[i + 2].size() <= 10) {
            location.titleId = steamTitleId(std::stoull(parts[i + 2]));
            location.platform = "pc";
        } else if (lower(part) == "title" && i + 2 < parts.size() && parts[i + 1].size() == 8 && parts[i + 2].size() == 8 &&
                   isHex(parts[i + 1]) && isHex(parts[i + 2])) {
            location.titleId = normalizeTitleId(parts[i + 1] + parts[i + 2]);
            location.platform = "3ds";
        }
    }

    std::error_code error;
    const bool folder = fs::is_directory(path, error);
    fs::path game = folder ? path : path.parent_path();
    while (game.has_parent_path() && game != game.root_path() && isGenericFolder(pathToUtf8(game.filename()))) game = game.parent_path();
    std::string name = game == game.root_path() ? "" : pathToUtf8(game.filename());
    if (!folder) name = name.empty() ? pathToUtf8(path.stem()) : name + " - " + pathToUtf8(path.stem());
    if (!location.titleId.empty())
        name = location.platform == "pc" ? "Steam app " + std::to_string(std::stoull(location.titleId, nullptr, 16)) : location.titleId;
    location.name = name.empty() ? pathToUtf8(path) : name;
    return location;
}

std::string saveLocationTitleId(const SaveLocation& location) {
    if (isValidTitleId(location.titleId)) return normalizeTitleId(location.titleId);
    const auto& path = location.path;
    return std::format("FFFFFFFE{:08X}", crc32(ByteView(reinterpret_cast<const std::uint8_t*>(path.data()), path.size())));
}

std::optional<fs::path> rpgMakerDataFolder(const fs::path& save) {
    std::error_code error;
    fs::path folder = fs::is_directory(save, error) ? save : save.parent_path();
    for (int depth = 0; depth < 3 && !folder.empty(); ++depth) {
        for (const char* data : {"data", "www/data"})
            if (fs::is_regular_file(folder / data / "System.json", error)) return folder / data;
        if (folder == folder.root_path()) break;
        folder = folder.parent_path();
    }
    return std::nullopt;
}

FileSaveMount::FileSaveMount(fs::path path, std::string name) : path_(std::move(path)), name_(std::move(name)) {}

std::vector<SaveFileEntry> FileSaveMount::listFiles() {
    return {{name_, fs::file_size(path_)}};
}

Bytes FileSaveMount::read(const std::string& path) {
    if (path != name_) throw std::runtime_error("no such file: " + path);
    std::ifstream stream(path_, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot open " + path_.string());
    return Bytes(std::istreambuf_iterator<char>(stream), {});
}

// Written next to the original and renamed over it, so a full SD card cannot leave half a save behind.
void FileSaveMount::write(const std::string& path, ByteView data) {
    if (path != name_) throw std::runtime_error("this save only holds " + name_);
    const fs::path temporary = path_.string() + ".savetinker-tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        stream.close();
        if (!stream) {
            std::error_code ignored;
            fs::remove(temporary, ignored);
            throw std::runtime_error("cannot write " + path_.string() + " (is the card full?)");
        }
    }
    fs::rename(temporary, path_);
}

void FileSaveMount::remove(const std::string&) {
    throw std::runtime_error("save files cannot be removed from here");
}

FileSavePlatform::FileSavePlatform(std::unique_ptr<Platform> inner) : inner_(std::move(inner)) {}

void FileSavePlatform::setFolders(std::vector<fs::path> folders) {
    folders_ = std::move(folders);
}

void FileSavePlatform::setLocations(std::vector<SaveLocation> locations) {
    locations_ = std::move(locations);
}

void FileSavePlatform::setKnownGames(std::vector<GameTarget> games) {
    knownGames_ = std::move(games);
}

namespace {

std::string environment(const char* name) {
    const char* value = std::getenv(name);
    return value ? value : "";
}

std::string homeFolder() {
    auto home = environment("USERPROFILE");
    return home.empty() ? environment("HOME") : home;
}

bool wildcardMatch(std::string_view pattern, std::string_view text) {
    if (pattern.empty()) return text.empty();
    if (pattern[0] == '*')
        return wildcardMatch(pattern.substr(1), text) || (!text.empty() && wildcardMatch(pattern, text.substr(1)));
    if (text.empty()) return false;
    if (pattern[0] != '?' && std::tolower(static_cast<unsigned char>(pattern[0])) != std::tolower(static_cast<unsigned char>(text[0])))
        return false;
    return wildcardMatch(pattern.substr(1), text.substr(1));
}

}

std::vector<fs::path> expandSavePath(std::string_view pattern) {
    std::string text(pattern);
    const std::pair<const char*, std::string> variables[] = {
        {"%APPDATA%", environment("APPDATA")},
        {"%LOCALAPPDATA%", environment("LOCALAPPDATA")},
        {"%USERPROFILE%", homeFolder()},
        {"%HOME%", homeFolder()},
        {"%DOCUMENTS%", homeFolder().empty() ? "" : homeFolder() + "/Documents"},
    };
    for (const auto& [name, value] : variables) {
        for (auto at = text.find(name); at != std::string::npos; at = text.find(name, at + value.size())) {
            if (value.empty()) return {};
            text.replace(at, std::strlen(name), value);
        }
    }
    if (text.find('%') != std::string::npos) return {};
    std::ranges::replace(text, '\\', '/');

    std::vector<fs::path> current;
    std::vector<std::string> parts;
    for (std::size_t start = 0; start <= text.size();) {
        const auto end = std::min(text.find('/', start), text.size());
        parts.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    if (parts.empty()) return {};
    current.push_back(pathFromUtf8(parts[0].empty() ? "/" : parts[0] + "/"));
    constexpr std::size_t kMaxMatches = 64;
    for (std::size_t i = 1; i < parts.size(); ++i) {
        const auto& part = parts[i];
        if (part.empty()) continue;
        std::vector<fs::path> next;
        for (const auto& base : current) {
            std::error_code error;
            if (part.find_first_of("*?") == std::string::npos) {
                if (fs::exists(base / pathFromUtf8(part), error)) next.push_back(base / pathFromUtf8(part));
                continue;
            }
            for (fs::directory_iterator it(base, fs::directory_options::skip_permission_denied, error), end; !error && it != end;
                 it.increment(error)) {
                if (wildcardMatch(part, pathToUtf8(it->path().filename())) && next.size() < kMaxMatches) next.push_back(it->path());
            }
        }
        current = std::move(next);
        if (current.empty()) break;
    }
    std::ranges::sort(current);
    return current;
}

std::vector<FileSavePlatform::Found> FileSavePlatform::scanLocations() const {
    std::vector<Found> found;
    std::set<fs::path> seen;
    for (const auto& game : knownGames_) {
        for (const auto& pattern : game.savePaths) {
            for (const auto& path : expandSavePath(pattern)) {
                if (!seen.insert(path).second) continue;
                std::error_code error;
                const bool folder = fs::is_directory(path, error);
                Found entry;
                entry.title.platform = game.platform;
                entry.title.titleId = game.titleId;
                entry.title.name = game.name.empty() ? game.titleId : game.name;
                entry.save.titleId = game.titleId;
                entry.save.kind = SaveKind::File;
                entry.save.size = folder ? folderSize(path) : fs::file_size(path, error);
                entry.save.path = path;
                found.push_back(std::move(entry));
            }
        }
    }
    for (const auto& location : locations_) {
        if (seen.contains(pathFromUtf8(location.path))) continue;
        const fs::path path = pathFromUtf8(location.path);
        std::error_code error;
        const bool folder = fs::is_directory(path, error);
        if (!folder && !fs::is_regular_file(path, error)) continue;
        Found entry;
        entry.title.platform = location.platform.empty() ? "pc" : location.platform;
        entry.title.titleId = saveLocationTitleId(location);
        entry.title.name = location.name.empty() ? pathToUtf8(path.filename()) : location.name;
        entry.save.titleId = entry.title.titleId;
        entry.save.kind = SaveKind::File;
        entry.save.size = folder ? folderSize(path) : fs::file_size(path, error);
        entry.save.path = path;
        found.push_back(std::move(entry));
    }
    return found;
}

std::vector<FileSavePlatform::Found> FileSavePlatform::scan() const {
    std::vector<Found> found;
    std::set<std::string> seenPaths;
    for (const auto& folder : folders_) {
        std::error_code error;
        if (!fs::is_directory(folder, error)) continue;
        for (fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, error), end;
             !error && it != end; it.increment(error)) {
            if (it.depth() > 2) it.disable_recursion_pending();
            if (!it->is_regular_file(error) || !kSaveExtensions.contains(lower(it->path().extension().string()))) continue;
            const auto size = it->file_size(error);
            if (error || size == 0 || size > kMaxSaveSize) continue;
            const auto canonical = it->path().generic_string();
            if (!seenPaths.insert(canonical).second) continue;

            std::optional<RomIdentity> identity;
            for (const char* romExtension : {".gba", ".nds", ".dsi"}) {
                auto rom = it->path();
                rom.replace_extension(romExtension);
                if ((identity = readRomIdentity(rom))) break;
            }
            Found entry;
            const auto stem = it->path().stem().string();
            if (identity) {
                entry.title.platform = identity->platform;
                entry.title.titleId = cartridgeTitleId(identity->gameCode);
                entry.title.name = identity->title.empty() ? stem : identity->title;
                entry.title.publisher = identity->gameCode + "  \xC2\xB7  " + it->path().filename().string();
            } else {
                // Without a ROM the game is unknown; the file name keeps the id stable.
                entry.title.platform = "file";
                entry.title.titleId = std::format("FFFFFFFF{:08X}", crc32(ByteView(reinterpret_cast<const std::uint8_t*>(stem.data()), stem.size())));
                entry.title.name = stem;
                entry.title.publisher = it->path().filename().string();
            }
            entry.save.titleId = entry.title.titleId;
            entry.save.kind = SaveKind::File;
            entry.save.size = size;
            entry.save.path = it->path();
            found.push_back(std::move(entry));
        }
    }
    return found;
}

std::vector<TitleInfo> FileSavePlatform::titles() {
    auto titles = inner_->titles();
    std::set<std::string> known;
    for (const auto& title : titles) known.insert(title.titleId);
    for (auto& entry : scanLocations())
        if (known.insert(entry.title.titleId).second) titles.push_back(std::move(entry.title));
    for (auto& entry : scan())
        if (known.insert(entry.title.titleId).second) titles.push_back(std::move(entry.title));
    return titles;
}

std::vector<SaveInfo> FileSavePlatform::saves() {
    auto saves = inner_->saves();
    for (auto& entry : scanLocations()) saves.push_back(std::move(entry.save));
    for (auto& entry : scan()) saves.push_back(std::move(entry.save));
    return saves;
}

std::expected<std::unique_ptr<SaveMount>, std::string> FileSavePlatform::mount(const SaveInfo& save) {
    if (save.kind != SaveKind::File) return inner_->mount(save);
    std::error_code error;
    if (fs::is_directory(save.path, error))
        return std::make_unique<DirectorySaveMount>(save.path, DirectorySaveMount::CommitHook{}, 0, true);
    if (!fs::is_regular_file(save.path, error)) return std::unexpected("save file not found: " + save.path.string());
    const bool cartridge = std::ranges::any_of(folders_, [&](const fs::path& folder) {
        const auto relative = save.path.lexically_relative(folder);
        return !relative.empty() && !relative.native().starts_with(fs::path("..").native());
    });
    if (!cartridge) return std::make_unique<FileSaveMount>(save.path, pathToUtf8(save.path.filename()));
    return std::make_unique<FileSaveMount>(save.path);
}

std::expected<std::shared_ptr<GameData>, std::string> FileSavePlatform::openGameData(const std::string& titleId) {
    for (const auto& location : locations_) {
        if (saveLocationTitleId(location) != titleId) continue;
        if (auto folder = rpgMakerDataFolder(pathFromUtf8(location.path))) return std::make_shared<DirectoryGameData>(*folder);
    }
    return inner_->openGameData(titleId);
}

}

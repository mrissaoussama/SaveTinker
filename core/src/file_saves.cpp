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
const std::set<std::string> kSaveExtensions = {".sav", ".srm", ".sa1", ".dsv", ".fla", ".eep", ".sra"};

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

// Looks for the ROM a save belongs to: next to it, or one folder up for loaders that keep saves in a
// subfolder (TWiLight Menu++ uses saves/, others use save/ or a per-system folder).
std::optional<RomIdentity> findRomForSave(const fs::path& save) {
    for (const auto& folder : {save.parent_path(), save.parent_path().parent_path()}) {
        if (folder.empty()) continue;
        for (const char* romExtension : {".gba", ".nds", ".dsi"}) {
            auto rom = folder / save.filename();
            rom.replace_extension(romExtension);
            if (auto identity = readRomIdentity(rom)) return identity;
        }
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
    std::error_code error;
    const bool folder = fs::is_directory(path, error);
    fs::path game = folder ? path : path.parent_path();
    while (game.has_parent_path() && game != game.root_path() && isGenericFolder(pathToUtf8(game.filename()))) game = game.parent_path();
    std::string name = game == game.root_path() ? "" : pathToUtf8(game.filename());
    if (!folder) name = name.empty() ? pathToUtf8(path.stem()) : name + " - " + pathToUtf8(path.stem());
    location.name = name.empty() ? pathToUtf8(path) : name;
    if (!folder) {
        if (auto identity = findRomForSave(path)) {
            location.platform = identity->platform;
            location.titleId = cartridgeTitleId(identity->gameCode);
            if (!identity->title.empty()) location.name = identity->title;
        }
    }
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

std::vector<FileSavePlatform::Found> FileSavePlatform::scanLocations() const {
    std::vector<Found> found;
    for (const auto& location : locations_) {
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

            const auto identity = findRomForSave(it->path());
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
    // GBA and DS saves are one raw image that editors know as save.bin, wherever it was found.
    bool cartridge = std::ranges::any_of(folders_, [&](const fs::path& folder) {
        const auto relative = save.path.lexically_relative(folder);
        return !relative.empty() && !relative.native().starts_with(fs::path("..").native());
    });
    for (const auto& location : locations_)
        if (pathFromUtf8(location.path) == save.path && (location.platform == "nds" || location.platform == "gba")) cartridge = true;
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

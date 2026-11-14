#include "savetinker/file_saves.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <set>

#include "savetinker/checksum.hpp"

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

FileSaveMount::FileSaveMount(fs::path path) : path_(std::move(path)) {}

std::vector<SaveFileEntry> FileSaveMount::listFiles() {
    return {{kFileName, fs::file_size(path_)}};
}

Bytes FileSaveMount::read(const std::string& path) {
    if (path != kFileName) throw std::runtime_error("no such file: " + path);
    std::ifstream stream(path_, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot open " + path_.string());
    return Bytes(std::istreambuf_iterator<char>(stream), {});
}

// Written next to the original and renamed over it, so a full SD card cannot leave half a save behind.
void FileSaveMount::write(const std::string& path, ByteView data) {
    if (path != kFileName) throw std::runtime_error("this save only holds " + std::string(kFileName));
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
    for (auto& entry : scan())
        if (known.insert(entry.title.titleId).second) titles.push_back(std::move(entry.title));
    return titles;
}

std::vector<SaveInfo> FileSavePlatform::saves() {
    auto saves = inner_->saves();
    for (auto& entry : scan()) saves.push_back(std::move(entry.save));
    return saves;
}

std::expected<std::unique_ptr<SaveMount>, std::string> FileSavePlatform::mount(const SaveInfo& save) {
    if (save.kind != SaveKind::File) return inner_->mount(save);
    std::error_code error;
    if (!fs::is_regular_file(save.path, error)) return std::unexpected("save file not found: " + save.path.string());
    return std::make_unique<FileSaveMount>(save.path);
}

std::expected<std::shared_ptr<GameData>, std::string> FileSavePlatform::openGameData(const std::string& titleId) {
    return inner_->openGameData(titleId);
}

}

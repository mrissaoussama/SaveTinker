#include "savetinker/game_data.hpp"

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace st {

namespace fs = std::filesystem;

DirectoryGameData::DirectoryGameData(fs::path root) : root_(std::move(root)) {}

std::optional<fs::path> DirectoryGameData::resolve(const std::string& path) const {
    std::string cleaned = path;
    while (cleaned.starts_with('/')) cleaned.erase(0, 1);
    const fs::path relative = fs::path(cleaned).lexically_normal();
    if (relative.is_absolute() || relative.has_root_name() || (!relative.empty() && *relative.begin() == ".."))
        return std::nullopt;
    if (relative.empty() || relative == ".") return root_;
    return root_ / relative;
}

std::optional<Bytes> DirectoryGameData::read(const std::string& path, std::uint64_t maxSize) {
    const auto target = resolve(path);
    if (!target) throw std::invalid_argument("invalid game data path: " + path);
    std::ifstream stream(*target, std::ios::binary | std::ios::ate);
    if (!stream) return std::nullopt;
    const auto size = static_cast<std::uint64_t>(stream.tellg());
    if (size > maxSize) throw std::runtime_error("game file is too large: " + path);
    Bytes content(static_cast<std::size_t>(size));
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(content.data()), static_cast<std::streamsize>(size));
    if (!stream) throw std::runtime_error("cannot read game file: " + path);
    return content;
}

std::vector<GameDataEntry> DirectoryGameData::list(const std::string& directory) {
    const auto target = resolve(directory);
    if (!target) throw std::invalid_argument("invalid game data path: " + directory);
    std::vector<GameDataEntry> entries;
    std::error_code error;
    for (fs::directory_iterator it(*target, error), end; !error && it != end; it.increment(error)) {
        GameDataEntry entry{it->path().filename().string(), 0, it->is_directory()};
        if (!entry.directory) entry.size = it->file_size();
        entries.push_back(std::move(entry));
    }
    std::ranges::sort(entries, {}, &GameDataEntry::name);
    return entries;
}

}

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

namespace {

// Lists a layer that may be unavailable; the files of the layers above it are still worth showing.
std::vector<GameDataEntry> safeList(GameData& layer, const std::string& directory) {
    try {
        return layer.list(directory);
    } catch (const std::exception&) {
        return {};
    }
}

}

LayeredGameData::LayeredGameData(std::vector<std::shared_ptr<GameData>> layers) : layers_(std::move(layers)) {}

std::optional<Bytes> LayeredGameData::read(const std::string& path, std::uint64_t maxSize) {
    for (const auto& layer : layers_)
        if (auto content = layer->read(path, maxSize)) return content;
    return std::nullopt;
}

std::vector<GameDataEntry> LayeredGameData::list(const std::string& directory) {
    std::vector<GameDataEntry> entries;
    for (const auto& layer : layers_)
        // The game itself is the last layer; when it cannot be read, the mod files found so far still count.
        for (auto& entry : layer == layers_.back() && !entries.empty() ? safeList(*layer, directory) : layer->list(directory))
            if (std::ranges::find(entries, entry.name, &GameDataEntry::name) == entries.end()) entries.push_back(std::move(entry));
    std::ranges::sort(entries, {}, &GameDataEntry::name);
    return entries;
}

std::shared_ptr<GameData> withModFolders(std::shared_ptr<GameData> base, const std::vector<fs::path>& folders) {
    std::vector<std::shared_ptr<GameData>> layers;
    for (const auto& folder : folders) {
        std::error_code error;
        if (fs::is_directory(folder, error)) layers.push_back(std::make_shared<DirectoryGameData>(folder));
    }
    if (layers.empty()) return base;
    if (base) layers.push_back(std::move(base));
    return std::make_shared<LayeredGameData>(std::move(layers));
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

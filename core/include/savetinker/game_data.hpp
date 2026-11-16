#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "savetinker/byte_buffer.hpp"

namespace st {

struct GameDataEntry {
    std::string name;
    std::uint64_t size = 0;
    bool directory = false;
};

class GameData {
public:
    virtual ~GameData() = default;
    // Nothing when the file does not exist. Throws when it is larger than `maxSize`.
    virtual std::optional<Bytes> read(const std::string& path, std::uint64_t maxSize) = 0;
    virtual std::vector<GameDataEntry> list(const std::string& directory) = 0;
};

class DirectoryGameData : public GameData {
public:
    explicit DirectoryGameData(std::filesystem::path root);

    std::optional<Bytes> read(const std::string& path, std::uint64_t maxSize) override;
    std::vector<GameDataEntry> list(const std::string& directory) override;

private:
    std::optional<std::filesystem::path> resolve(const std::string& path) const;

    std::filesystem::path root_;
};

// Files from `layers` win over the base game, the way LayeredFS mods replace game files.
class LayeredGameData : public GameData {
public:
    LayeredGameData(std::vector<std::shared_ptr<GameData>> layers);

    std::optional<Bytes> read(const std::string& path, std::uint64_t maxSize) override;
    std::vector<GameDataEntry> list(const std::string& directory) override;

private:
    std::vector<std::shared_ptr<GameData>> layers_;
};

// Stands in for game files that could not be opened: every read reports why.
class UnavailableGameData : public GameData {
public:
    explicit UnavailableGameData(std::string reason) : reason_(std::move(reason)) {}
    std::optional<Bytes> read(const std::string&, std::uint64_t) override { throw std::runtime_error(reason_); }
    std::vector<GameDataEntry> list(const std::string&) override { throw std::runtime_error(reason_); }

private:
    std::string reason_;
};

// Adds the mod folders that exist (for example atmosphere/contents/<id>/romfs) in front of `base`.
std::shared_ptr<GameData> withModFolders(std::shared_ptr<GameData> base, const std::vector<std::filesystem::path>& folders);

using GameDataOpener = std::function<std::expected<std::shared_ptr<GameData>, std::string>()>;

}

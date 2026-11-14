#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
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

using GameDataOpener = std::function<std::expected<std::shared_ptr<GameData>, std::string>()>;

}

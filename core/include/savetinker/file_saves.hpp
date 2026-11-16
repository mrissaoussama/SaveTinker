#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "savetinker/manifest.hpp"
#include "savetinker/platform.hpp"
#include "savetinker/settings.hpp"

namespace st {

struct RomIdentity {
    std::string platform;
    std::string gameCode;
    std::string title;
};

std::optional<RomIdentity> readRomIdentity(const std::filesystem::path& rom);

// Title id for a cartridge game: eight zeros, then the four-letter game code as hex.
std::string cartridgeTitleId(const std::string& gameCode);
std::optional<RomIdentity> findRomForSave(const std::filesystem::path& save);

// Paths are kept as UTF-8 in settings.
std::filesystem::path pathFromUtf8(const std::string& text);
std::string pathToUtf8(const std::filesystem::path& path);

// A PC save named after the folder it sits in. The user links it to a game afterwards.
SaveLocation describeSaveLocation(const std::filesystem::path& path);
std::string saveLocationTitleId(const SaveLocation& location);
// The data folder of an RPG Maker MV/MZ game (the one holding System.json), found from a save.
std::optional<std::filesystem::path> rpgMakerDataFolder(const std::filesystem::path& save);

class FileSaveMount : public SaveMount {
public:
    // `name` is the file name editors see.
    explicit FileSaveMount(std::filesystem::path path, std::string name = kFileName);

    std::vector<SaveFileEntry> listFiles() override;
    Bytes read(const std::string& path) override;
    void write(const std::string& path, ByteView data) override;
    void remove(const std::string& path) override;
    void commit() override {}
    bool singleFile() const override { return true; }

    static constexpr const char* kFileName = "save.bin";

private:
    std::filesystem::path path_;
    std::string name_;
};

class FileSavePlatform : public Platform {
public:
    explicit FileSavePlatform(std::unique_ptr<Platform> inner);

    void setFolders(std::vector<std::filesystem::path> folders);
    void setLocations(std::vector<SaveLocation> locations);
    const std::vector<std::filesystem::path>& folders() const { return folders_; }
    Platform& inner() { return *inner_; }

    std::string gamePlatform() const override { return inner_->gamePlatform(); }
    std::filesystem::path dataRoot() const override { return inner_->dataRoot(); }
    std::vector<UserAccount> users() override { return inner_->users(); }
    std::vector<TitleInfo> titles() override;
    std::vector<SaveInfo> saves() override;
    std::optional<std::string> runningTitleId() override { return inner_->runningTitleId(); }
    std::expected<std::unique_ptr<SaveMount>, std::string> mount(const SaveInfo& save) override;
    std::expected<std::shared_ptr<GameData>, std::string> openGameData(const std::string& titleId) override;
    void afterRestore(const SaveInfo& save) override {
        if (save.kind != SaveKind::File) inner_->afterRestore(save);
    }

private:
    struct Found {
        TitleInfo title;
        SaveInfo save;
    };
    std::vector<Found> scan() const;
    std::vector<Found> scanLocations() const;

    std::unique_ptr<Platform> inner_;
    std::vector<std::filesystem::path> folders_;
    std::vector<SaveLocation> locations_;
};

}

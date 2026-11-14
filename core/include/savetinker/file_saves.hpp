#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "savetinker/platform.hpp"

namespace st {

// Game identity read from a ROM header next to a save file.
struct RomIdentity {
    std::string platform;
    std::string gameCode;
    std::string title;
};

// GBA (.gba) and DS (.nds) headers. Nothing when the file is neither.
std::optional<RomIdentity> readRomIdentity(const std::filesystem::path& rom);

// Title id for a cartridge game: eight zeros, then the four-letter game code as hex.
std::string cartridgeTitleId(const std::string& gameCode);

// One save file on the SD card or disk, exposed to editors as "save.bin".
class FileSaveMount : public SaveMount {
public:
    explicit FileSaveMount(std::filesystem::path path);

    std::vector<SaveFileEntry> listFiles() override;
    Bytes read(const std::string& path) override;
    void write(const std::string& path, ByteView data) override;
    void remove(const std::string& path) override;
    void commit() override {}

    static constexpr const char* kFileName = "save.bin";

private:
    std::filesystem::path path_;
};

// Adds save files from emulators and dumps (GBA, DS) found in folders to another platform's titles and saves.
class FileSavePlatform : public Platform {
public:
    explicit FileSavePlatform(std::unique_ptr<Platform> inner);

    void setFolders(std::vector<std::filesystem::path> folders);
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

private:
    struct Found {
        TitleInfo title;
        SaveInfo save;
    };
    std::vector<Found> scan() const;

    std::unique_ptr<Platform> inner_;
    std::vector<std::filesystem::path> folders_;
};

}

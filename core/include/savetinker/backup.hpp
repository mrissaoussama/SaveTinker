#pragma once

#include <chrono>
#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "savetinker/platform.hpp"

namespace st {

struct BackupFile {
    std::string path;
    std::uint64_t size = 0;
    std::string sha256;
};

struct BackupRecord {
    std::filesystem::path directory;
    std::string titleId;
    std::string titleName;
    std::string slot;
    std::string createdAt;
    std::string reason;
    std::vector<BackupFile> files;

    std::uint64_t totalSize() const;
};

// Layout: <root>/<titleId>/<slot>/<timestamp>/{backup.json, files/...}
class BackupManager {
public:
    using Clock = std::function<std::chrono::system_clock::time_point()>;

    explicit BackupManager(std::filesystem::path root, Clock clock = std::chrono::system_clock::now);

    std::expected<BackupRecord, std::string> create(SaveMount& mount, const SaveInfo& save,
                                                    const std::string& titleName, const std::string& reason);
    std::vector<BackupRecord> list(const std::string& titleId, const std::string& slot = {}) const;
    std::expected<void, std::string> verify(const BackupRecord& record) const;
    std::expected<void, std::string> restore(const BackupRecord& record, SaveMount& mount) const;
    // Replaces the save with the files of any folder: a copied backup, or a save from another device.
    std::expected<void, std::string> importFolder(const std::filesystem::path& folder, SaveMount& mount) const;
    // Copies the backed up files, without SaveTinker's record, to `folder`.
    std::expected<BackupRecord, std::string> exportTo(const BackupRecord& record, const std::filesystem::path& folder) const;
    bool remove(const BackupRecord& record) const;
    std::size_t prune(const std::string& titleId, const std::string& slot, std::size_t keep) const;

private:
    std::optional<BackupRecord> readRecord(const std::filesystem::path& directory) const;
    std::expected<void, std::string> writeOver(const std::vector<std::pair<std::string, std::filesystem::path>>& files,
                                               SaveMount& mount) const;

    std::filesystem::path root_;
    Clock clock_;
};

}

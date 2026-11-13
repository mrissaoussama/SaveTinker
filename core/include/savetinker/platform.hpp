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
#include "savetinker/document.hpp"

namespace st {

struct UserAccount {
    std::string id;
    std::string nickname;
};

enum class SaveKind { Account, Device, Bcat, Cache, System };

std::string_view saveKindName(SaveKind kind);
std::optional<SaveKind> parseSaveKind(std::string_view name);

struct TitleInfo {
    std::string platform;
    std::string titleId;
    std::string name;
    std::string publisher;
    std::string displayVersion;
    std::uint32_t version = 0;
    Bytes icon;
};

struct SaveInfo {
    std::string titleId;
    SaveKind kind = SaveKind::Account;
    std::string userId;
    std::uint64_t size = 0;
    std::uint64_t journalSize = 0;
    std::uint64_t saveDataId = 0;

    std::string slotName() const;
};

struct SaveFileEntry {
    std::string path;
    std::uint64_t size = 0;
};

class SaveMount {
public:
    virtual ~SaveMount() = default;
    virtual std::vector<SaveFileEntry> listFiles() = 0;
    virtual Bytes read(const std::string& path) = 0;
    virtual void write(const std::string& path, ByteView data) = 0;
    virtual void remove(const std::string& path) = 0;
    virtual void commit() = 0;
};

// Save files exposed as a normal directory, which covers the PC backend and Switch devoptab mounts.
class DirectorySaveMount : public SaveMount {
public:
    using CommitHook = std::function<void()>;

    DirectorySaveMount(std::filesystem::path root, CommitHook commitHook = {}, std::uint64_t chunkSize = 0);

    std::vector<SaveFileEntry> listFiles() override;
    Bytes read(const std::string& path) override;
    void write(const std::string& path, ByteView data) override;
    void remove(const std::string& path) override;
    void commit() override;

private:
    std::filesystem::path resolve(const std::string& path) const;

    std::filesystem::path root_;
    CommitHook commitHook_;
    std::uint64_t chunkSize_;
};

class Platform {
public:
    virtual ~Platform() = default;

    virtual std::string gamePlatform() const = 0;
    virtual std::filesystem::path dataRoot() const = 0;

    virtual std::vector<UserAccount> users() = 0;
    virtual std::vector<TitleInfo> titles() = 0;
    virtual std::vector<SaveInfo> saves() = 0;
    virtual std::optional<std::string> runningTitleId() = 0;
    virtual std::expected<std::unique_ptr<SaveMount>, std::string> mount(const SaveInfo& save) = 0;
};

Document loadDocument(SaveMount& mount);
// Writes files that differ from what is on the mount, then commits.
void storeDocument(const Document& document, SaveMount& mount);

}

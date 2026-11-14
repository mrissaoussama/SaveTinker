#include "ctr_platform.hpp"

#include <3ds.h>
#include <format>

#include "savetinker/smdh.hpp"

namespace st {

namespace {

constexpr const char* kSaveDevice = "stsave";
constexpr std::uint32_t kApplicationHigh = 0x00040000;

std::string formatTitleId(std::uint64_t id) {
    return std::format("{:016X}", id);
}

std::string formatVersion(std::uint16_t version) {
    return std::format("{}.{}.{}", (version >> 10) & 0x3F, (version >> 4) & 0x3F, version & 0xF);
}

FS_Path saveArchivePath(std::uint64_t id, std::uint8_t mediaType, std::array<std::uint32_t, 3>& storage) {
    storage = {mediaType, static_cast<std::uint32_t>(id), static_cast<std::uint32_t>(id >> 32)};
    return {PATH_BINARY, sizeof(storage), storage.data()};
}

Bytes readSmdh(std::uint64_t id, std::uint8_t mediaType) {
    static const std::uint32_t filePath[] = {0x0, 0x0, 0x2, 0x6E6F6369, 0x0};
    const std::uint32_t archivePath[] = {static_cast<std::uint32_t>(id), static_cast<std::uint32_t>(id >> 32), mediaType, 0x0};
    const FS_Path archive{PATH_BINARY, sizeof(archivePath), archivePath};
    const FS_Path file{PATH_BINARY, sizeof(filePath), filePath};
    Handle handle = 0;
    if (R_FAILED(FSUSER_OpenFileDirectly(&handle, ARCHIVE_SAVEDATA_AND_CONTENT, archive, file, FS_OPEN_READ, 0))) return {};
    Bytes data(kSmdhSize);
    u32 read = 0;
    const Result rc = FSFILE_Read(handle, &read, 0, data.data(), static_cast<u32>(data.size()));
    FSFILE_Close(handle);
    if (R_FAILED(rc) || read != data.size()) return {};
    return data;
}

class CtrSaveMount : public DirectorySaveMount {
public:
    CtrSaveMount()
        : DirectorySaveMount(std::string(kSaveDevice) + ":/", [] {
              if (const Result rc = archiveCommitSaveData(kSaveDevice); R_FAILED(rc))
                  throw std::runtime_error(std::format("commit failed (0x{:08X})", static_cast<std::uint32_t>(rc)));
          }) {}
    ~CtrSaveMount() override { archiveUnmount(kSaveDevice); }
};

class RomfsGameData : public DirectoryGameData {
public:
    explicit RomfsGameData(std::string device) : DirectoryGameData(device + ":/"), device_(std::move(device)) {}
    ~RomfsGameData() override { romfsUnmount(device_.c_str()); }

private:
    std::string device_;
};

}

CtrPlatform::CtrPlatform() {
    amReady_ = R_SUCCEEDED(amInit());
    cfgReady_ = R_SUCCEEDED(cfguInit());
}

CtrPlatform::~CtrPlatform() {
    if (cfgReady_) cfguExit();
    if (amReady_) amExit();
}

std::vector<UserAccount> CtrPlatform::users() {
    return {};
}

std::vector<CtrPlatform::InstalledTitle> CtrPlatform::scan() {
    std::vector<InstalledTitle> titles;
    if (!amReady_) return titles;
    for (const auto mediaType : {MEDIATYPE_SD, MEDIATYPE_GAME_CARD}) {
        u32 count = 0;
        if (R_FAILED(AM_GetTitleCount(mediaType, &count)) || count == 0) continue;
        std::vector<u64> ids(count);
        u32 read = 0;
        if (R_FAILED(AM_GetTitleList(&read, mediaType, count, ids.data()))) continue;
        ids.resize(read);
        std::vector<AM_TitleInfo> infos(ids.size());
        const bool haveInfo = R_SUCCEEDED(AM_GetTitleInfo(mediaType, static_cast<u32>(ids.size()), ids.data(), infos.data()));
        for (std::size_t i = 0; i < ids.size(); ++i) {
            if ((ids[i] >> 32) != kApplicationHigh) continue;
            titles.push_back({ids[i], static_cast<std::uint8_t>(mediaType), haveInfo ? infos[i].version : std::uint16_t{0}});
        }
    }
    installed_.clear();
    for (const auto& title : titles) installed_[formatTitleId(title.id)] = title;
    return titles;
}

const CtrPlatform::InstalledTitle* CtrPlatform::find(const std::string& titleId) {
    if (installed_.empty()) scan();
    const auto found = installed_.find(titleId);
    return found == installed_.end() ? nullptr : &found->second;
}

std::vector<TitleInfo> CtrPlatform::titles() {
    u8 language = CFG_LANGUAGE_EN;
    if (cfgReady_) CFGU_GetSystemLanguage(&language);
    std::vector<TitleInfo> result;
    for (const auto& title : scan()) {
        TitleInfo info;
        info.platform = "3ds";
        info.titleId = formatTitleId(title.id);
        info.version = title.version;
        info.displayVersion = formatVersion(title.version);
        if (auto smdh = parseSmdh(readSmdh(title.id, title.mediaType), language)) {
            info.name = std::move(smdh->shortTitle);
            info.publisher = std::move(smdh->publisher);
            info.icon = std::move(smdh->iconTga);
        }
        if (info.name.empty()) info.name = info.titleId;
        result.push_back(std::move(info));
    }
    return result;
}

std::vector<SaveInfo> CtrPlatform::saves() {
    std::vector<SaveInfo> result;
    for (const auto& title : scan()) {
        std::array<std::uint32_t, 3> storage{};
        FS_Archive archive = 0;
        if (R_FAILED(FSUSER_OpenArchive(&archive, ARCHIVE_USER_SAVEDATA, saveArchivePath(title.id, title.mediaType, storage))))
            continue;
        FSUSER_CloseArchive(archive);
        SaveInfo save;
        save.titleId = formatTitleId(title.id);
        save.kind = SaveKind::Device;
        result.push_back(std::move(save));
    }
    return result;
}

std::expected<std::unique_ptr<SaveMount>, std::string> CtrPlatform::mount(const SaveInfo& save) {
    const auto* title = find(save.titleId);
    if (!title) return std::unexpected("title is not installed");
    archiveUnmount(kSaveDevice);
    std::array<std::uint32_t, 3> storage{};
    if (const Result rc = archiveMount(ARCHIVE_USER_SAVEDATA, saveArchivePath(title->id, title->mediaType, storage), kSaveDevice);
        R_FAILED(rc))
        return std::unexpected(std::format("cannot open save data (0x{:08X})", static_cast<std::uint32_t>(rc)));
    return std::make_unique<CtrSaveMount>();
}

std::expected<std::shared_ptr<GameData>, std::string> CtrPlatform::openGameData(const std::string& titleId) {
    const auto* title = find(titleId);
    if (!title) return std::unexpected("title is not installed");
    static int mounts = 0;
    const std::string device = "stgame" + std::to_string(mounts++ % 8);
    romfsUnmount(device.c_str());
    if (const Result rc = romfsMountFromTitle(title->id, static_cast<FS_MediaType>(title->mediaType), device.c_str()); R_FAILED(rc))
        return std::unexpected(std::format("cannot open the game files (0x{:08X})", static_cast<std::uint32_t>(rc)));
    return std::make_shared<RomfsGameData>(device);
}

}

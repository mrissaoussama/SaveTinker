#include "switch_platform.hpp"

#include <cstdio>
#include <cstring>
#include <format>
#include <switch.h>

#include "savetinker/manifest.hpp"

namespace st {

namespace {

constexpr const char* kMountName = "stsave";
constexpr std::uint64_t kFallbackChunkSize = 512 * 1024;

std::string formatResult(Result rc) {
    return std::format("0x{:X} ({}-{})", rc, R_MODULE(rc) + 2000, R_DESCRIPTION(rc));
}

std::string formatTitleId(std::uint64_t id) {
    return std::format("{:016X}", id);
}

std::uint64_t parseTitleId(const std::string& text) {
    return std::stoull(text, nullptr, 16);
}

std::string formatUserId(const AccountUid& uid) {
    return std::format("{:016X}{:016X}", uid.uid[1], uid.uid[0]);
}

AccountUid parseUserId(const std::string& text) {
    AccountUid uid{};
    if (text.size() == 32) {
        uid.uid[1] = std::stoull(text.substr(0, 16), nullptr, 16);
        uid.uid[0] = std::stoull(text.substr(16), nullptr, 16);
    }
    return uid;
}

std::optional<SaveKind> kindFromType(std::uint8_t type) {
    switch (type) {
    case FsSaveDataType_Account: return SaveKind::Account;
    case FsSaveDataType_Device: return SaveKind::Device;
    case FsSaveDataType_Bcat: return SaveKind::Bcat;
    case FsSaveDataType_Cache: return SaveKind::Cache;
    default: return std::nullopt;
    }
}

FsSaveDataType typeFromKind(SaveKind kind) {
    switch (kind) {
    case SaveKind::Account: return FsSaveDataType_Account;
    case SaveKind::Device: return FsSaveDataType_Device;
    case SaveKind::Bcat: return FsSaveDataType_Bcat;
    case SaveKind::Cache: return FsSaveDataType_Cache;
    case SaveKind::System: return FsSaveDataType_System;
    }
    return FsSaveDataType_Account;
}

class SwitchSaveMount : public DirectorySaveMount {
public:
    explicit SwitchSaveMount(std::uint64_t chunkSize)
        : DirectorySaveMount(std::string(kMountName) + ":/", [] { fsdevCommitDevice(kMountName); }, chunkSize) {}
    ~SwitchSaveMount() override { fsdevUnmountDevice(kMountName); }
};

}

SwitchPlatform::SwitchPlatform() {
    accountReady_ = R_SUCCEEDED(accountInitialize(AccountServiceType_Administrator));
    nsReady_ = R_SUCCEEDED(nsInitialize());
    pmdmntReady_ = R_SUCCEEDED(pmdmntInitialize());
    pminfoReady_ = R_SUCCEEDED(pminfoInitialize());
}

SwitchPlatform::~SwitchPlatform() {
    if (pminfoReady_) pminfoExit();
    if (pmdmntReady_) pmdmntExit();
    if (nsReady_) nsExit();
    if (accountReady_) accountExit();
}

bool SwitchPlatform::appletMode() const {
    const auto type = appletGetAppletType();
    return type != AppletType_Application && type != AppletType_SystemApplication;
}

std::vector<UserAccount> SwitchPlatform::users() {
    std::vector<UserAccount> accounts;
    if (!accountReady_) return accounts;
    AccountUid uids[ACC_USER_LIST_SIZE]{};
    s32 count = 0;
    if (R_FAILED(accountListAllUsers(uids, ACC_USER_LIST_SIZE, &count))) return accounts;
    for (s32 i = 0; i < count; ++i) {
        UserAccount account{formatUserId(uids[i]), {}};
        AccountProfile profile;
        if (R_SUCCEEDED(accountGetProfile(&profile, uids[i]))) {
            AccountProfileBase base{};
            if (R_SUCCEEDED(accountProfileGet(&profile, nullptr, &base)))
                account.nickname.assign(base.nickname, strnlen(base.nickname, sizeof(base.nickname)));
            accountProfileClose(&profile);
        }
        accounts.push_back(std::move(account));
    }
    return accounts;
}

std::vector<TitleInfo> SwitchPlatform::titles() {
    std::vector<TitleInfo> result;
    if (!nsReady_) return result;
    auto control = std::make_unique<NsApplicationControlData>();
    NsApplicationRecord records[64];
    s32 offset = 0;
    while (true) {
        s32 count = 0;
        if (R_FAILED(nsListApplicationRecord(records, 64, offset, &count)) || count <= 0) break;
        for (s32 i = 0; i < count; ++i) {
            TitleInfo title;
            title.platform = "switch";
            title.titleId = formatTitleId(records[i].application_id);
            u64 controlSize = 0;
            if (R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_Storage, records[i].application_id,
                                                        control.get(), sizeof(NsApplicationControlData), &controlSize))) {
                NacpLanguageEntry* language = nullptr;
                if (R_SUCCEEDED(nacpGetLanguageEntry(&control->nacp, &language)) && language) {
                    title.name.assign(language->name, strnlen(language->name, sizeof(language->name)));
                    title.publisher.assign(language->author, strnlen(language->author, sizeof(language->author)));
                }
                title.displayVersion.assign(control->nacp.display_version,
                                            strnlen(control->nacp.display_version, sizeof(control->nacp.display_version)));
                if (controlSize > sizeof(NacpStruct))
                    title.icon.assign(control->icon, control->icon + (controlSize - sizeof(NacpStruct)));
                journalSizes_[title.titleId] = {control->nacp.user_account_save_data_journal_size,
                                                control->nacp.device_save_data_journal_size};
            }
            result.push_back(std::move(title));
        }
        offset += count;
    }
    return result;
}

std::vector<SaveInfo> SwitchPlatform::saves() {
    std::vector<SaveInfo> result;
    FsSaveDataInfoReader reader;
    if (R_FAILED(fsOpenSaveDataInfoReader(&reader, FsSaveDataSpaceId_User))) return result;
    FsSaveDataInfo infos[32];
    while (true) {
        s64 count = 0;
        if (R_FAILED(fsSaveDataInfoReaderRead(&reader, infos, 32, &count)) || count <= 0) break;
        for (s64 i = 0; i < count; ++i) {
            const auto kind = kindFromType(infos[i].save_data_type);
            if (!kind) continue;
            SaveInfo save;
            save.titleId = formatTitleId(infos[i].application_id);
            save.kind = *kind;
            if (*kind == SaveKind::Account) save.userId = formatUserId(infos[i].uid);
            save.size = infos[i].size;
            save.saveDataId = infos[i].save_data_id;
            if (auto sizes = journalSizes_.find(save.titleId); sizes != journalSizes_.end())
                save.journalSize = *kind == SaveKind::Account ? sizes->second.account : sizes->second.device;
            result.push_back(std::move(save));
        }
    }
    fsSaveDataInfoReaderClose(&reader);
    return result;
}

std::optional<std::string> SwitchPlatform::runningTitleId() {
    if (!pmdmntReady_) return std::nullopt;
    u64 processId = 0;
    if (R_FAILED(pmdmntGetApplicationProcessId(&processId))) return std::nullopt;
    u64 programId = 0;
    if (pminfoReady_ && R_SUCCEEDED(pminfoGetProgramId(&programId, processId))) return formatTitleId(programId);
    return std::string{};
}

std::expected<std::unique_ptr<SaveMount>, std::string> SwitchPlatform::mount(const SaveInfo& save) {
    if (auto running = runningTitleId(); running && (running->empty() || *running == save.titleId))
        return std::unexpected("the game is still running; close it from the HOME menu first");

    FsSaveDataAttribute attribute{};
    attribute.application_id = parseTitleId(save.titleId);
    attribute.save_data_type = typeFromKind(save.kind);
    if (save.kind == SaveKind::Account) attribute.uid = parseUserId(save.userId);

    FsFileSystem fileSystem;
    const Result rc = fsOpenSaveDataFileSystem(&fileSystem, FsSaveDataSpaceId_User, &attribute);
    if (R_FAILED(rc)) return std::unexpected("cannot open save data: " + formatResult(rc));
    if (fsdevMountDevice(kMountName, fileSystem) == -1) return std::unexpected("cannot mount save data");

    const std::uint64_t chunkSize = save.journalSize > 0 ? save.journalSize / 2 : kFallbackChunkSize;
    return std::make_unique<SwitchSaveMount>(chunkSize);
}

}

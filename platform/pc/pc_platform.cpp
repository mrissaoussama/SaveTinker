#include "pc_platform.hpp"

#include <fstream>
#include <nlohmann/json.hpp>

#include "savetinker/manifest.hpp"

namespace st {

namespace fs = std::filesystem;

namespace {

nlohmann::json readJsonFile(const fs::path& path) {
    std::ifstream stream(path);
    if (!stream) return nlohmann::json::array();
    auto value = nlohmann::json::parse(stream, nullptr, false);
    return value.is_discarded() ? nlohmann::json::array() : value;
}

std::uint64_t directorySize(const fs::path& path) {
    std::uint64_t total = 0;
    for (const auto& entry : fs::recursive_directory_iterator(path))
        if (entry.is_regular_file()) total += entry.file_size();
    return total;
}

}

PcPlatform::PcPlatform(fs::path root, std::string gamePlatform)
    : root_(std::move(root)), gamePlatform_(std::move(gamePlatform)) {}

std::vector<UserAccount> PcPlatform::users() {
    std::vector<UserAccount> accounts;
    for (const auto& item : readJsonFile(root_ / "users.json"))
        if (item.is_object()) accounts.push_back({item.value("id", ""), item.value("nickname", "")});
    return accounts;
}

std::vector<TitleInfo> PcPlatform::titles() {
    std::vector<TitleInfo> result;
    for (const auto& item : readJsonFile(root_ / "titles.json")) {
        if (!item.is_object()) continue;
        TitleInfo title;
        title.platform = item.value("platform", gamePlatform_);
        title.titleId = normalizeTitleId(item.value("title_id", ""));
        title.name = item.value("name", "");
        title.publisher = item.value("publisher", "");
        title.displayVersion = item.value("version", "");
        if (auto addOns = item.find("dlc"); addOns != item.end() && addOns->is_array())
            for (const auto& addOn : *addOns)
                if (addOn.is_string()) title.addOns.push_back(normalizeTitleId(addOn.get<std::string>()));
        result.push_back(std::move(title));
    }
    return result;
}

std::vector<SaveInfo> PcPlatform::saves() {
    std::vector<SaveInfo> result;
    const fs::path savesRoot = root_ / "saves";
    std::error_code error;
    if (!fs::is_directory(savesRoot, error)) return result;
    for (const auto& titleEntry : fs::directory_iterator(savesRoot)) {
        if (!titleEntry.is_directory()) continue;
        const auto titleId = normalizeTitleId(titleEntry.path().filename().string());
        if (!isValidTitleId(titleId)) continue;
        for (const auto& slotEntry : fs::directory_iterator(titleEntry.path())) {
            if (!slotEntry.is_directory()) continue;
            const auto slot = slotEntry.path().filename().string();
            SaveInfo save;
            save.titleId = titleId;
            if (slot.starts_with("account-")) {
                save.kind = SaveKind::Account;
                save.userId = slot.substr(8);
            } else if (auto kind = parseSaveKind(slot)) {
                save.kind = *kind;
            } else {
                continue;
            }
            save.size = directorySize(slotEntry.path());
            result.push_back(std::move(save));
        }
    }
    return result;
}

std::optional<std::string> PcPlatform::runningTitleId() {
    std::ifstream stream(root_ / "running.txt");
    std::string titleId;
    if (stream >> titleId && isValidTitleId(titleId)) return normalizeTitleId(titleId);
    return std::nullopt;
}

fs::path PcPlatform::slotPath(const SaveInfo& save) const {
    return root_ / "saves" / save.titleId / save.slotName();
}

std::expected<std::unique_ptr<SaveMount>, std::string> PcPlatform::mount(const SaveInfo& save) {
    if (auto running = runningTitleId(); running && *running == save.titleId)
        return std::unexpected("the game is running; close it first");
    const auto path = slotPath(save);
    std::error_code error;
    if (!fs::is_directory(path, error)) return std::unexpected("save not found: " + path.string());
    return std::make_unique<DirectorySaveMount>(path, DirectorySaveMount::CommitHook{}, 0, true);
}

std::expected<std::shared_ptr<GameData>, std::string> PcPlatform::openGameData(const std::string& titleId) {
    const auto path = root_ / "romfs" / titleId;
    std::error_code error;
    if (!fs::is_directory(path, error)) return std::unexpected("no extracted game files in " + path.generic_string());
    return std::make_shared<DirectoryGameData>(path);
}

}

#include "savetinker/settings.hpp"

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>

namespace st {

Settings loadSettings(const std::filesystem::path& path) {
    Settings settings;
    std::ifstream stream(path);
    if (!stream) return settings;
    const auto root = nlohmann::json::parse(stream, nullptr, false);
    if (!root.is_object()) return settings;

    settings.autoBackup = root.value("auto_backup", settings.autoBackup);
    settings.backupsToKeep = std::clamp(root.value("backups_to_keep", settings.backupsToKeep), 1, 100);
    settings.uiScale = std::clamp(root.value("ui_scale", settings.uiScale), 0.75f, 2.0f);
    settings.language = root.value("language", settings.language);
    settings.checkUpdatesOnStart = root.value("check_updates_on_start", settings.checkUpdatesOnStart);
    settings.skippedVersion = root.value("skipped_version", settings.skippedVersion);
    if (auto folders = root.find("save_folders"); folders != root.end() && folders->is_array())
        for (const auto& folder : *folders)
            if (folder.is_string()) settings.saveFolders.push_back(folder.get<std::string>());
    if (auto locations = root.find("save_locations"); locations != root.end() && locations->is_array()) {
        for (const auto& item : *locations) {
            if (!item.is_object() || !item.contains("path")) continue;
            settings.saveLocations.push_back(
                {item.value("path", ""), item.value("name", ""), item.value("platform", "pc"), item.value("title_id", "")});
        }
    }
    if (auto repos = root.find("repos"); repos != root.end() && repos->is_array()) {
        for (const auto& repo : *repos) {
            if (!repo.is_object() || !repo.contains("url")) continue;
            settings.repos.push_back({repo.value("url", ""), repo.value("enabled", true)});
        }
    }
    return settings;
}

bool saveSettings(const std::filesystem::path& path, const Settings& settings) {
    nlohmann::json repos = nlohmann::json::array();
    for (const auto& repo : settings.repos) repos.push_back({{"url", repo.url}, {"enabled", repo.enabled}});
    nlohmann::json locations = nlohmann::json::array();
    for (const auto& location : settings.saveLocations)
        locations.push_back({{"path", location.path}, {"name", location.name}, {"platform", location.platform}, {"title_id", location.titleId}});
    const nlohmann::json root = {
        {"auto_backup", settings.autoBackup},
        {"backups_to_keep", settings.backupsToKeep},
        {"ui_scale", settings.uiScale},
        {"language", settings.language},
        {"check_updates_on_start", settings.checkUpdatesOnStart},
        {"skipped_version", settings.skippedVersion},
        {"repos", repos},
        {"save_folders", settings.saveFolders},
        {"save_locations", locations},
    };
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    const auto temporary = path.string() + ".tmp";
    {
        std::ofstream stream(temporary, std::ios::trunc);
        stream << root.dump(2);
        if (!stream) return false;
    }
    std::filesystem::rename(temporary, path, error);
    return !error;
}

}

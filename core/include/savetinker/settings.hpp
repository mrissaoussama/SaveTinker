#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace st {

struct RepoEntry {
    std::string url;
    bool enabled = true;
};

struct Settings {
    bool autoBackup = true;
    int backupsToKeep = 10;
    float uiScale = 1.0f;
    std::string language = "auto";
    bool checkUpdatesOnStart = true;
    std::string skippedVersion;
    std::vector<RepoEntry> repos;
    // Folders searched for loose save files (GBA, DS). Empty means the platform defaults.
    std::vector<std::string> saveFolders;
};

Settings loadSettings(const std::filesystem::path& path);
bool saveSettings(const std::filesystem::path& path, const Settings& settings);

}

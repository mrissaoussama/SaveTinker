#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "savetinker/manifest.hpp"

namespace st {

struct RepoEditorEntry {
    std::string id;
    std::string name;
    std::string description;
    std::string version;
    int apiVersion = kApiVersion;
    std::vector<std::string> authors;
    std::string source;
    std::vector<GameTarget> games;
    std::string downloadUrl;
    std::string sha256;
    std::uint64_t size = 0;
    std::string changelog;
    std::string updated;

    const GameTarget* findGame(std::string_view platform, std::string_view titleId) const;
    bool needsNewerApp() const { return apiVersion > kApiVersion; }
};

struct RepoIndex {
    std::string name;
    std::string maintainer;
    std::string homepage;
    std::string description;
    int apiVersion = kApiVersion;
    std::vector<std::string> platforms;
    std::vector<RepoEditorEntry> editors;
};

std::expected<RepoIndex, std::string> parseRepoIndex(std::string_view json, std::string_view indexUrl = {});
std::string resolveUrl(std::string_view baseUrl, std::string_view reference);

struct RepoSource {
    std::string url;
    bool enabled = true;
    RepoIndex index;
};

struct EditorMatch {
    const RepoSource* repo = nullptr;
    const RepoEditorEntry* editor = nullptr;
    bool versionSupported = false;
};

struct InstalledEditor {
    std::string id;
    std::string version;
    std::string repoUrl;
};

struct EditorUpdate {
    const RepoSource* repo = nullptr;
    const RepoEditorEntry* editor = nullptr;
    std::string installedVersion;
};

class RepoCatalog {
public:
    void setRepo(RepoSource source);
    bool removeRepo(std::string_view url);
    const std::vector<RepoSource>& repos() const { return repos_; }

    std::vector<EditorMatch> findEditors(std::string_view platform, std::string_view titleId,
                                         std::string_view gameVersion = {}) const;
    std::vector<EditorUpdate> findUpdates(const std::vector<InstalledEditor>& installed) const;

private:
    std::vector<RepoSource> repos_;
};

}

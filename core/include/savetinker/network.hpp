#pragma once

#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "savetinker/byte_buffer.hpp"
#include "savetinker/repository.hpp"
#include "savetinker/settings.hpp"

namespace st {

struct HttpResponse {
    long status = 0;
    Bytes body;
};

using ProgressCallback = std::function<void(std::uint64_t received, std::uint64_t total)>;

class HttpClient {
public:
    virtual ~HttpClient() = default;
    virtual std::expected<HttpResponse, std::string> get(const std::string& url, std::uint64_t maxBytes,
                                                         const ProgressCallback& progress = {}) = 0;
};

// GET that fails on non-2xx statuses.
std::expected<Bytes, std::string> download(HttpClient& client, const std::string& url, std::uint64_t maxBytes,
                                           const ProgressCallback& progress = {});

struct RepoStatus {
    std::string url;
    bool fromCache = false;
    std::string error;
    std::size_t editorCount = 0;
};

// Fetches repo indexes, keeps a copy of each so browsing works offline.
class RepoManager {
public:
    RepoManager(HttpClient& client, std::filesystem::path cacheDir);

    std::vector<RepoStatus> refresh(const std::vector<RepoEntry>& repos, RepoCatalog& catalog);
    std::vector<RepoStatus> loadCached(const std::vector<RepoEntry>& repos, RepoCatalog& catalog) const;

    // Downloads an editor package and checks its size and SHA-256 when the repo lists them.
    std::expected<Bytes, std::string> downloadEditor(const RepoEditorEntry& editor, const ProgressCallback& progress = {});

private:
    std::filesystem::path cachePath(const std::string& url) const;

    HttpClient& client_;
    std::filesystem::path cacheDir_;
};

struct ReleaseInfo {
    std::string version;
    std::string notes;
    std::string pageUrl;
    std::string assetUrl;
    std::string checksumUrl;
};

// Reads a GitHub "latest release" response. Returns nothing when `currentVersion` is up to date.
std::expected<std::optional<ReleaseInfo>, std::string> parseLatestRelease(std::string_view json, std::string_view currentVersion,
                                                                          std::string_view assetName);

class AppUpdater {
public:
    AppUpdater(HttpClient& client, std::string repoSlug, std::string currentVersion, std::string assetName);

    bool configured() const { return !repoSlug_.empty(); }
    std::expected<std::optional<ReleaseInfo>, std::string> check();
    // Downloads the release asset next to `target`, verifies it and replaces `target`.
    std::expected<void, std::string> install(const ReleaseInfo& release, const std::filesystem::path& target,
                                             const ProgressCallback& progress = {});

private:
    HttpClient& client_;
    std::string repoSlug_;
    std::string currentVersion_;
    std::string assetName_;
};

}

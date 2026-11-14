#include "savetinker/network.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>

#include "savetinker/checksum.hpp"
#include "savetinker/version.hpp"

namespace st {

namespace fs = std::filesystem;

namespace {

constexpr std::uint64_t kMaxIndexSize = 8u << 20;
constexpr std::uint64_t kMaxEditorSize = 32u << 20;
constexpr std::uint64_t kMaxAppSize = 64u << 20;

std::string_view asText(const Bytes& bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

std::optional<std::string> readTextFile(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return std::nullopt;
    return std::string(std::istreambuf_iterator<char>(stream), {});
}

void writeBinaryFile(const fs::path& path, ByteView data) {
    fs::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!stream) throw std::runtime_error("cannot write " + path.string());
}

}

std::expected<Bytes, std::string> download(HttpClient& client, const std::string& url, std::uint64_t maxBytes,
                                           const ProgressCallback& progress) {
    auto response = client.get(url, maxBytes, progress);
    if (!response) return std::unexpected(response.error());
    if (response->status < 200 || response->status >= 300)
        return std::unexpected(std::format("server answered {} for {}", response->status, url));
    return std::move(response->body);
}

RepoManager::RepoManager(HttpClient& client, fs::path cacheDir) : client_(client), cacheDir_(std::move(cacheDir)) {}

fs::path RepoManager::cachePath(const std::string& url) const {
    const auto digest = sha256(ByteView(reinterpret_cast<const std::uint8_t*>(url.data()), url.size()));
    return cacheDir_ / (toHex(ByteView(digest.data(), 12)) + ".json");
}

std::vector<RepoStatus> RepoManager::refresh(const std::vector<RepoEntry>& repos, RepoCatalog& catalog) {
    std::vector<RepoStatus> statuses;
    for (const auto& repo : repos) {
        RepoStatus status;
        status.url = repo.url;
        auto body = download(client_, repo.url, kMaxIndexSize);
        std::optional<RepoIndex> index;
        if (body) {
            auto parsed = parseRepoIndex(asText(*body), repo.url);
            if (parsed) {
                index = std::move(*parsed);
                try {
                    writeBinaryFile(cachePath(repo.url), *body);
                } catch (const std::exception&) {
                }
            } else {
                status.error = parsed.error();
            }
        } else {
            status.error = body.error();
        }
        if (!index) {
            if (auto cached = readTextFile(cachePath(repo.url))) {
                if (auto parsed = parseRepoIndex(*cached, repo.url)) {
                    index = std::move(*parsed);
                    status.fromCache = true;
                }
            }
        }
        if (index) {
            status.editorCount = index->editors.size();
            catalog.setRepo({repo.url, repo.enabled, std::move(*index)});
        } else {
            catalog.removeRepo(repo.url);
        }
        statuses.push_back(std::move(status));
    }
    return statuses;
}

std::vector<RepoStatus> RepoManager::loadCached(const std::vector<RepoEntry>& repos, RepoCatalog& catalog) const {
    std::vector<RepoStatus> statuses;
    for (const auto& repo : repos) {
        RepoStatus status;
        status.url = repo.url;
        status.fromCache = true;
        auto cached = readTextFile(cachePath(repo.url));
        std::expected<RepoIndex, std::string> parsed = std::unexpected(std::string("not downloaded yet"));
        if (cached) parsed = parseRepoIndex(*cached, repo.url);
        if (parsed) {
            status.editorCount = parsed->editors.size();
            catalog.setRepo({repo.url, repo.enabled, std::move(*parsed)});
        } else {
            status.error = parsed.error();
        }
        statuses.push_back(std::move(status));
    }
    return statuses;
}

std::expected<Bytes, std::string> RepoManager::downloadEditor(const RepoEditorEntry& editor, const ProgressCallback& progress) {
    auto body = download(client_, editor.downloadUrl, kMaxEditorSize, progress);
    if (!body) return body;
    if (editor.size && body->size() != editor.size)
        return std::unexpected(std::format("size mismatch: expected {} bytes, got {}", editor.size, body->size()));
    if (!editor.sha256.empty() && toHex(sha256(*body)) != editor.sha256)
        return std::unexpected("checksum mismatch, the download may be corrupt or tampered with");
    return body;
}

std::expected<std::optional<ReleaseInfo>, std::string> parseLatestRelease(std::string_view json, std::string_view currentVersion,
                                                                          std::string_view assetName) {
    const auto root = nlohmann::json::parse(json, nullptr, false);
    if (!root.is_object()) return std::unexpected("invalid release information");
    ReleaseInfo release;
    release.version = root.value("tag_name", "");
    release.notes = root.value("body", "");
    release.pageUrl = root.value("html_url", "");
    const auto latest = Version::parse(release.version);
    const auto current = Version::parse(currentVersion);
    if (!latest) return std::unexpected("release has no usable version tag");
    if (current && *latest <= *current) return std::optional<ReleaseInfo>{};
    if (root.value("draft", false) || root.value("prerelease", false)) return std::optional<ReleaseInfo>{};

    if (auto assets = root.find("assets"); assets != root.end() && assets->is_array()) {
        const std::string checksumName = std::string(assetName) + ".sha256";
        for (const auto& asset : *assets) {
            const auto name = asset.value("name", "");
            if (name == assetName) release.assetUrl = asset.value("browser_download_url", "");
            else if (name == checksumName) release.checksumUrl = asset.value("browser_download_url", "");
        }
    }
    if (release.assetUrl.empty()) return std::unexpected("release " + release.version + " has no " + std::string(assetName));
    if (release.version.starts_with('v') || release.version.starts_with('V')) release.version.erase(0, 1);
    return std::optional<ReleaseInfo>{std::move(release)};
}

AppUpdater::AppUpdater(HttpClient& client, std::string repoSlug, std::string currentVersion, std::string assetName)
    : client_(client), repoSlug_(std::move(repoSlug)), currentVersion_(std::move(currentVersion)), assetName_(std::move(assetName)) {}

std::expected<std::optional<ReleaseInfo>, std::string> AppUpdater::check() {
    if (!configured()) return std::unexpected("no update source configured for this build");
    auto body = download(client_, "https://api.github.com/repos/" + repoSlug_ + "/releases/latest", kMaxIndexSize);
    if (!body) return std::unexpected(body.error());
    return parseLatestRelease(asText(*body), currentVersion_, assetName_);
}

std::expected<void, std::string> AppUpdater::install(const ReleaseInfo& release, const fs::path& target,
                                                     const ProgressCallback& progress) {
    std::string expectedHash;
    if (!release.checksumUrl.empty()) {
        auto checksum = download(client_, release.checksumUrl, 4096);
        if (!checksum) return std::unexpected("cannot fetch checksum: " + checksum.error());
        expectedHash = std::string(asText(*checksum).substr(0, 64));
        std::ranges::transform(expectedHash, expectedHash.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    }
    auto body = download(client_, release.assetUrl, kMaxAppSize, progress);
    if (!body) return std::unexpected(body.error());
    if (!expectedHash.empty() && toHex(sha256(*body)) != expectedHash)
        return std::unexpected("checksum mismatch, update not installed");

    const fs::path staged = target.string() + ".new";
    std::error_code error;
    try {
        writeBinaryFile(staged, *body);
    } catch (const std::exception& failure) {
        return std::unexpected(failure.what());
    }
    fs::rename(staged, target, error);
    if (error) {
        fs::remove(staged, error);
        return std::unexpected("cannot replace the app: " + error.message());
    }
    return {};
}

}

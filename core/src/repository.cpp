#include "savetinker/repository.hpp"

#include <algorithm>

#include "game_target_json.hpp"
#include "json_fields.hpp"
#include "savetinker/checksum.hpp"
#include "savetinker/version.hpp"

namespace st {

using namespace json_fields;

const GameTarget* RepoEditorEntry::findGame(std::string_view platform, std::string_view titleId) const {
    const auto wanted = normalizeTitleId(titleId);
    for (const auto& game : games)
        if (game.platform == platform && game.titleId == wanted) return &game;
    return nullptr;
}

std::string resolveUrl(std::string_view baseUrl, std::string_view reference) {
    if (reference.find("://") != std::string_view::npos || baseUrl.empty()) return std::string(reference);
    if (reference.starts_with('/')) {
        const auto scheme = baseUrl.find("://");
        const auto hostEnd = scheme == std::string_view::npos ? std::string_view::npos : baseUrl.find('/', scheme + 3);
        return std::string(baseUrl.substr(0, hostEnd)) + std::string(reference);
    }
    const auto lastSlash = baseUrl.rfind('/');
    std::string directory(lastSlash == std::string_view::npos ? baseUrl : baseUrl.substr(0, lastSlash + 1));
    if (!directory.ends_with('/')) directory += '/';
    while (reference.starts_with("./")) reference.remove_prefix(2);
    return directory + std::string(reference);
}

namespace {

RepoEditorEntry parseEditorEntry(const nlohmann::json& object, std::string_view indexUrl) {
    if (!object.is_object()) throw FieldError("each editor must be an object");
    RepoEditorEntry entry;
    entry.id = requiredString(object, "id");
    if (!isValidEditorId(entry.id)) throw FieldError("invalid editor id \"" + entry.id + "\"");
    entry.name = requiredString(object, "name");
    entry.description = optionalString(object, "description");
    entry.version = requiredString(object, "version");
    if (!Version::parse(entry.version)) throw FieldError("invalid version for " + entry.id);
    entry.authors = stringList(object, "authors");
    entry.source = optionalString(object, "source");
    entry.games = parseGameTargets(object);
    entry.downloadUrl = resolveUrl(indexUrl, requiredString(object, "download"));
    entry.sha256 = optionalString(object, "sha256");
    if (!entry.sha256.empty()) {
        try {
            if (fromHex(entry.sha256).size() != 32) throw FieldError("");
        } catch (...) {
            throw FieldError("invalid sha256 for " + entry.id);
        }
        std::ranges::transform(entry.sha256, entry.sha256.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    }
    if (auto size = object.find("size"); size != object.end() && size->is_number_unsigned())
        entry.size = size->get<std::uint64_t>();
    entry.changelog = optionalString(object, "changelog");
    entry.updated = optionalString(object, "updated");
    return entry;
}

}

std::expected<RepoIndex, std::string> parseRepoIndex(std::string_view json, std::string_view indexUrl) {
    try {
        const auto root = nlohmann::json::parse(json);
        if (!root.is_object()) return std::unexpected("repo index must be a JSON object");
        RepoIndex index;
        index.name = requiredString(root, "name");
        index.maintainer = optionalString(root, "maintainer");
        index.homepage = optionalString(root, "homepage");
        index.description = optionalString(root, "description");
        index.apiVersion = optionalInt(root, "api_version", kApiVersion);
        index.platforms = stringList(root, "platforms");

        if (auto editors = root.find("editors"); editors != root.end()) {
            if (!editors->is_array()) return std::unexpected("\"editors\" must be a list");
            for (const auto& item : *editors) index.editors.push_back(parseEditorEntry(item, indexUrl));
        }
        for (const auto& editor : index.editors)
            for (const auto& game : editor.games)
                if (std::ranges::find(index.platforms, game.platform) == index.platforms.end())
                    index.platforms.push_back(game.platform);
        return index;
    } catch (const nlohmann::json::exception& error) {
        return std::unexpected(std::string("invalid JSON: ") + error.what());
    } catch (const FieldError& error) {
        return std::unexpected(error.what());
    }
}

void RepoCatalog::setRepo(RepoSource source) {
    auto existing = std::ranges::find(repos_, source.url, &RepoSource::url);
    if (existing != repos_.end()) *existing = std::move(source);
    else repos_.push_back(std::move(source));
}

bool RepoCatalog::removeRepo(std::string_view url) {
    return std::erase_if(repos_, [&](const RepoSource& repo) { return repo.url == url; }) > 0;
}

std::vector<EditorMatch> RepoCatalog::findEditors(std::string_view platform, std::string_view titleId,
                                                  std::string_view gameVersion) const {
    std::vector<EditorMatch> matches;
    for (const auto& repo : repos_) {
        if (!repo.enabled) continue;
        for (const auto& editor : repo.index.editors) {
            const auto* game = editor.findGame(platform, titleId);
            if (!game) continue;
            const bool supported = gameVersion.empty() || game->supportsVersion(gameVersion);
            matches.push_back({&repo, &editor, supported});
        }
    }
    std::ranges::stable_sort(matches, [](const EditorMatch& a, const EditorMatch& b) {
        return a.versionSupported > b.versionSupported;
    });
    return matches;
}

std::vector<EditorUpdate> RepoCatalog::findUpdates(const std::vector<InstalledEditor>& installed) const {
    std::vector<EditorUpdate> updates;
    for (const auto& local : installed) {
        const auto localVersion = Version::parse(local.version);
        if (!localVersion) continue;
        for (const auto& repo : repos_) {
            if (!repo.enabled || repo.url != local.repoUrl) continue;
            for (const auto& editor : repo.index.editors) {
                if (editor.id != local.id) continue;
                const auto remoteVersion = Version::parse(editor.version);
                if (remoteVersion && *remoteVersion > *localVersion)
                    updates.push_back({&repo, &editor, local.version});
            }
        }
    }
    return updates;
}

}

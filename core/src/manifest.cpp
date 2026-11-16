#include "savetinker/manifest.hpp"

#include <algorithm>
#include <cctype>
#include <format>

#include "json_fields.hpp"
#include "game_target_json.hpp"
#include "savetinker/version.hpp"

namespace st {

using namespace json_fields;

std::string platformDisplayName(std::string_view platformId) {
    if (platformId == platforms::Switch) return "Nintendo Switch";
    if (platformId == platforms::Switch2) return "Nintendo Switch 2";
    if (platformId == platforms::N3ds) return "Nintendo 3DS";
    if (platformId == platforms::Nds) return "Nintendo DS";
    if (platformId == platforms::Gba) return "Game Boy Advance";
    if (platformId == "file") return "Save file";
    if (platformId == "pc") return "PC";
    if (platformId == kAnyTitle) return "Any console";
    return std::string(platformId);
}

std::string_view gameTargetPlatform(std::string_view platformId) {
    return platformId == platforms::Switch2 ? platforms::Switch : platformId;
}

bool isValidTitleId(std::string_view titleId) {
    return titleId.size() == 16 &&
           std::ranges::all_of(titleId, [](unsigned char c) { return std::isxdigit(c) != 0; });
}

std::string normalizeTitleId(std::string_view titleId) {
    std::string out(titleId);
    std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return out;
}

bool GameTarget::supportsVersion(std::string_view gameVersion) const {
    if (versions.empty()) return true;
    return std::ranges::any_of(versions, [&](const std::string& pattern) { return versionMatches(pattern, gameVersion); });
}

bool GameTarget::supportsSaveVersion(std::string_view saveVersion) const {
    if (saveVersions.empty()) return true;
    return std::ranges::any_of(saveVersions, [&](const std::string& pattern) { return versionMatches(pattern, saveVersion); });
}

bool GameTarget::matches(std::string_view wantedPlatform, std::string_view wantedTitle) const {
    // A Switch 2 plays Switch games, so editors for "switch" also open saves found on a Switch 2.
    const bool compatible = platform == wantedPlatform || (platform == platforms::Switch && wantedPlatform == platforms::Switch2);
    if (platform != kAnyTitle && !compatible) return false;
    return generic() || titleId == normalizeTitleId(wantedTitle);
}

const GameTarget* findGameTarget(const std::vector<GameTarget>& games, std::string_view platform, std::string_view titleId) {
    const GameTarget* fallback = nullptr;
    for (const auto& game : games) {
        if (!game.matches(platform, titleId)) continue;
        if (!game.generic()) return &game;
        if (!fallback) fallback = &game;
    }
    return fallback;
}

const GameTarget* EditorManifest::findGame(std::string_view platform, std::string_view titleId) const {
    return findGameTarget(games, platform, titleId);
}

bool EditorManifest::generic() const {
    return std::ranges::any_of(games, &GameTarget::generic);
}

std::vector<std::string> EditorManifest::platforms() const {
    std::vector<std::string> ids;
    for (const auto& game : games)
        if (std::ranges::find(ids, game.platform) == ids.end()) ids.push_back(game.platform);
    return ids;
}

GameTarget parseGameTarget(const nlohmann::json& object) {
    if (!object.is_object()) throw FieldError("each game must be an object");
    GameTarget game;
    game.platform = requiredString(object, "platform");
    const auto titleId = requiredString(object, "title_id");
    if (titleId != kAnyTitle && !isValidTitleId(titleId))
        throw FieldError("invalid title_id \"" + titleId + "\" (expected 16 hex digits or \"*\")");
    game.titleId = normalizeTitleId(titleId);
    if (game.platform == kAnyTitle && !game.generic()) throw FieldError("platform \"*\" needs title_id \"*\"");
    game.name = optionalString(object, "name");
    game.versions = stringList(object, "versions");
    game.saveVersions = stringList(object, "save_versions");
    return game;
}

std::vector<Dependency> parseDependencies(const nlohmann::json& parent) {
    std::vector<Dependency> dependencies;
    auto found = parent.find("dependencies");
    if (found == parent.end()) return dependencies;
    if (!found->is_array()) throw FieldError("\"dependencies\" must be a list");
    for (const auto& item : *found) {
        Dependency dependency;
        if (item.is_string()) {
            dependency.id = item.get<std::string>();
        } else if (item.is_object()) {
            dependency.id = requiredString(item, "id");
            dependency.version = optionalString(item, "version", "*");
        } else {
            throw FieldError("each dependency must be an id or {\"id\": ..., \"version\": ...}");
        }
        if (!isValidEditorId(dependency.id)) throw FieldError("invalid dependency id \"" + dependency.id + "\"");
        dependencies.push_back(std::move(dependency));
    }
    return dependencies;
}

std::string parseKind(const nlohmann::json& parent) {
    const auto kind = optionalString(parent, "kind", std::string(kKindEditor));
    if (kind != kKindEditor && kind != kKindLibrary) throw FieldError("\"kind\" must be \"editor\" or \"library\"");
    return kind;
}

std::vector<GameTarget> parseGameTargets(const nlohmann::json& parent, bool required) {
    auto found = parent.find("games");
    if (!required && (found == parent.end() || (found->is_array() && found->empty()))) return {};
    if (found == parent.end() || !found->is_array() || found->empty())
        throw FieldError("\"games\" must list at least one game");
    std::vector<GameTarget> games;
    for (const auto& item : *found) games.push_back(parseGameTarget(item));
    return games;
}

bool isValidEditorId(std::string_view id) {
    return !id.empty() && std::ranges::all_of(id, [](unsigned char c) {
        return std::islower(c) || std::isdigit(c) || c == '.' || c == '-' || c == '_';
    });
}

std::expected<EditorManifest, std::string> parseManifest(std::string_view json) {
    try {
        const auto root = nlohmann::json::parse(json);
        if (!root.is_object()) return std::unexpected("manifest must be a JSON object");

        EditorManifest manifest;
        manifest.id = requiredString(root, "id");
        if (!isValidEditorId(manifest.id))
            return std::unexpected("id may only contain lowercase letters, digits, '.', '-' and '_'");
        manifest.name = requiredString(root, "name");
        manifest.description = optionalString(root, "description");
        manifest.version = requiredString(root, "version");
        if (!Version::parse(manifest.version)) return std::unexpected("invalid version \"" + manifest.version + "\"");
        manifest.apiVersion = optionalInt(root, "api_version", kApiVersion);
        if (manifest.apiVersion > kApiVersion)
            return std::unexpected("editor needs API version " + std::to_string(manifest.apiVersion) +
                                   ", this build supports " + std::to_string(kApiVersion));
        manifest.authors = stringList(root, "authors");
        manifest.source = optionalString(root, "source");
        manifest.updateUrl = optionalString(root, "update_url");
        manifest.entry = optionalString(root, "entry", "main.lua");
        manifest.icon = optionalString(root, "icon");
        if (!manifest.icon.empty() && (manifest.icon.starts_with('/') || manifest.icon.find("..") != std::string::npos))
            return std::unexpected("\"icon\" must be a path inside the editor folder");
        if (auto gameData = root.find("game_data"); gameData != root.end()) {
            if (!gameData->is_boolean()) return std::unexpected("\"game_data\" must be true or false");
            manifest.gameData = gameData->get<bool>();
        }
        if (auto full = root.find("full_memory"); full != root.end()) {
            if (!full->is_boolean()) return std::unexpected("\"full_memory\" must be true or false");
            manifest.fullMemory = full->get<bool>();
        }
        manifest.kind = parseKind(root);
        manifest.dependencies = parseDependencies(root);
        manifest.games = parseGameTargets(root, !manifest.library());
        return manifest;
    } catch (const nlohmann::json::exception& error) {
        return std::unexpected(std::string("invalid JSON: ") + error.what());
    } catch (const FieldError& error) {
        return std::unexpected(error.what());
    }
}

}

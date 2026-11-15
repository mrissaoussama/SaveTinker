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
    if (platformId == platforms::N3ds) return "Nintendo 3DS";
    if (platformId == platforms::Nds) return "Nintendo DS";
    if (platformId == platforms::Gba) return "Game Boy Advance";
    if (platformId == "file") return "Save file";
    if (platformId == "pc") return "PC";
    if (platformId == kAnyTitle) return "Any console";
    return std::string(platformId);
}

std::string steamTitleId(std::uint64_t appId) {
    return std::format("{:016X}", appId);
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

bool GameTarget::matches(std::string_view wantedPlatform, std::string_view wantedTitle) const {
    if (platform != kAnyTitle && platform != wantedPlatform) return false;
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
    if (auto steam = object.find("steam_app_id"); steam != object.end()) {
        if (!steam->is_number_unsigned() || steam->get<std::uint64_t>() == 0) throw FieldError("steam_app_id must be a positive number");
        if (game.platform != platforms::Pc) throw FieldError("steam_app_id needs platform \"pc\"");
        if (object.contains("title_id")) throw FieldError("use either title_id or steam_app_id");
        game.titleId = steamTitleId(steam->get<std::uint64_t>());
        game.name = optionalString(object, "name");
        game.versions = stringList(object, "versions");
        game.savePaths = stringList(object, "save_paths");
        return game;
    }
    const auto titleId = requiredString(object, "title_id");
    if (titleId != kAnyTitle && !isValidTitleId(titleId))
        throw FieldError("invalid title_id \"" + titleId + "\" (expected 16 hex digits or \"*\")");
    game.titleId = normalizeTitleId(titleId);
    if (game.platform == kAnyTitle && !game.generic()) throw FieldError("platform \"*\" needs title_id \"*\"");
    game.name = optionalString(object, "name");
    game.versions = stringList(object, "versions");
    game.savePaths = stringList(object, "save_paths");
    if (!game.savePaths.empty() && (game.platform != platforms::Pc || game.generic()))
        throw FieldError("save_paths needs platform \"pc\" and a game id");
    return game;
}

std::vector<GameTarget> parseGameTargets(const nlohmann::json& parent) {
    auto found = parent.find("games");
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
        manifest.games = parseGameTargets(root);
        return manifest;
    } catch (const nlohmann::json::exception& error) {
        return std::unexpected(std::string("invalid JSON: ") + error.what());
    } catch (const FieldError& error) {
        return std::unexpected(error.what());
    }
}

}

#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace st {

inline constexpr int kApiVersion = 1;
inline constexpr std::string_view kAnyTitle = "*";

namespace platforms {
inline constexpr std::string_view Switch = "switch";
inline constexpr std::string_view Switch2 = "switch2";
inline constexpr std::string_view N3ds = "3ds";
inline constexpr std::string_view Nds = "nds";
inline constexpr std::string_view Gba = "gba";
inline constexpr std::string_view Pc = "pc";
}

std::string platformDisplayName(std::string_view platformId);
// The id a game is published under: Switch 2 plays Switch games, so its titles are written as "switch".
std::string_view gameTargetPlatform(std::string_view platformId);
bool isValidTitleId(std::string_view titleId);
std::string normalizeTitleId(std::string_view titleId);

struct GameTarget {
    std::string platform;
    std::string titleId;
    std::string name;
    std::vector<std::string> versions;
    // Patterns for the save format version that the editor's save_version(save) reports.
    std::vector<std::string> saveVersions;

    bool supportsVersion(std::string_view gameVersion) const;
    bool supportsSaveVersion(std::string_view saveVersion) const;
    bool generic() const { return titleId == kAnyTitle; }
    bool matches(std::string_view platform, std::string_view titleId) const;
};

// Another package that must be installed: usually a library of shared Lua modules.
struct Dependency {
    std::string id;
    std::string version = "*";
};

inline constexpr std::string_view kKindEditor = "editor";
inline constexpr std::string_view kKindLibrary = "library";

const GameTarget* findGameTarget(const std::vector<GameTarget>& games, std::string_view platform, std::string_view titleId);

struct EditorManifest {
    std::string id;
    std::string name;
    std::string description;
    std::string version;
    int apiVersion = kApiVersion;
    std::vector<std::string> authors;
    std::string source;
    std::string updateUrl;
    std::string entry = "main.lua";
    std::string icon;
    bool gameData = false;
    // Asks for the most memory the console can give; on Switch that means not running from the album.
    bool fullMemory = false;
    // "editor", or "library" for a package of Lua modules that editors list in their dependencies.
    std::string kind = std::string(kKindEditor);
    std::vector<Dependency> dependencies;
    std::vector<GameTarget> games;

    bool library() const { return kind == kKindLibrary; }
    const GameTarget* findGame(std::string_view platform, std::string_view titleId) const;
    bool generic() const;
    std::vector<std::string> platforms() const;
};

std::expected<EditorManifest, std::string> parseManifest(std::string_view json);

}

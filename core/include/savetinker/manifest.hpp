#pragma once

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace st {

inline constexpr int kApiVersion = 1;

namespace platforms {
inline constexpr std::string_view Switch = "switch";
inline constexpr std::string_view N3ds = "3ds";
}

std::string platformDisplayName(std::string_view platformId);
bool isValidTitleId(std::string_view titleId);
std::string normalizeTitleId(std::string_view titleId);

struct GameTarget {
    std::string platform;
    std::string titleId;
    std::string name;
    std::vector<std::string> versions;

    bool supportsVersion(std::string_view gameVersion) const;
};

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
    std::vector<GameTarget> games;

    const GameTarget* findGame(std::string_view platform, std::string_view titleId) const;
    std::vector<std::string> platforms() const;
};

std::expected<EditorManifest, std::string> parseManifest(std::string_view json);

}

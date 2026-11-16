#pragma once

#include <nlohmann/json.hpp>
#include <vector>

#include "savetinker/manifest.hpp"

namespace st {

GameTarget parseGameTarget(const nlohmann::json& object);
std::vector<GameTarget> parseGameTargets(const nlohmann::json& parent, bool required = true);
std::vector<Dependency> parseDependencies(const nlohmann::json& parent);
std::string parseKind(const nlohmann::json& parent);
bool isValidEditorId(std::string_view id);

}

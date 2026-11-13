#pragma once

#include <nlohmann/json.hpp>
#include <vector>

#include "savetinker/manifest.hpp"

namespace st {

GameTarget parseGameTarget(const nlohmann::json& object);
std::vector<GameTarget> parseGameTargets(const nlohmann::json& parent);
bool isValidEditorId(std::string_view id);

}

#pragma once

#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>

namespace st::json_fields {

struct FieldError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

inline std::string requiredString(const nlohmann::json& object, const char* key) {
    auto found = object.find(key);
    if (found == object.end() || !found->is_string() || found->get<std::string>().empty())
        throw FieldError(std::string("missing or empty \"") + key + "\"");
    return found->get<std::string>();
}

inline std::string optionalString(const nlohmann::json& object, const char* key, std::string fallback = {}) {
    auto found = object.find(key);
    if (found == object.end() || found->is_null()) return fallback;
    if (!found->is_string()) throw FieldError(std::string("\"") + key + "\" must be a string");
    return found->get<std::string>();
}

inline std::vector<std::string> stringList(const nlohmann::json& object, const char* key) {
    std::vector<std::string> values;
    auto found = object.find(key);
    if (found == object.end() || found->is_null()) return values;
    if (found->is_string()) return {found->get<std::string>()};
    if (!found->is_array()) throw FieldError(std::string("\"") + key + "\" must be a list of strings");
    for (const auto& item : *found) {
        if (!item.is_string()) throw FieldError(std::string("\"") + key + "\" must be a list of strings");
        values.push_back(item.get<std::string>());
    }
    return values;
}

inline int optionalInt(const nlohmann::json& object, const char* key, int fallback) {
    auto found = object.find(key);
    if (found == object.end() || found->is_null()) return fallback;
    if (!found->is_number_integer()) throw FieldError(std::string("\"") + key + "\" must be an integer");
    return found->get<int>();
}

}

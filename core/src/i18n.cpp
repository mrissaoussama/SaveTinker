#include "savetinker/i18n.hpp"

#include <nlohmann/json.hpp>

namespace st {

int countPlaceholders(std::string_view text) {
    int count = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '{') {
            if (i + 1 < text.size() && text[i + 1] == '{') {
                ++i;
                continue;
            }
            const auto close = text.find('}', i);
            if (close == std::string_view::npos) return -1;
            ++count;
            i = close;
        } else if (text[i] == '}') {
            if (i + 1 < text.size() && text[i + 1] == '}') {
                ++i;
                continue;
            }
            return -1;
        }
    }
    return count;
}

bool Translator::load(std::string_view json, std::string* error, std::vector<std::string>* rejected) {
    const auto root = nlohmann::json::parse(json, nullptr, false);
    if (!root.is_object()) {
        if (error) *error = "translation file must be a JSON object";
        return false;
    }
    for (const auto& [english, value] : root.items()) {
        if (!value.is_string()) {
            if (rejected) rejected->push_back(english);
            continue;
        }
        auto translated = value.get<std::string>();
        if (translated.empty()) continue;
        if (countPlaceholders(english) != countPlaceholders(translated)) {
            if (rejected) rejected->push_back(english);
            continue;
        }
        entries_[english] = std::move(translated);
    }
    return true;
}

std::string_view Translator::get(std::string_view text) const {
    const auto found = entries_.find(text);
    return found == entries_.end() ? text : std::string_view(found->second);
}

}

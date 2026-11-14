#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "savetinker/i18n.hpp"

namespace app {

st::Translator& translator();

inline std::string tr(std::string_view text) {
    return std::string(translator().get(text));
}

// Marks text for scripts/update_translations.py where tr() is applied later.
constexpr const char* tr_noop(const char* text) {
    return text;
}

// For ImGui calls that need a C string. Valid until translations are reloaded.
inline const char* trc(const char* text) {
    return translator().get(text).data();
}

template <typename... Args>
std::string trf(std::string_view text, const Args&... args) {
    return translator().format(text, args...);
}

// Built-in translation first, then <data>/lang/<language>.json for fixes and new languages.
void loadTranslations(const std::string& language, const std::filesystem::path& dataRoot);
// Language codes with a built-in translation, English included.
std::vector<std::string> builtinLanguages();

}

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

constexpr const char* tr_noop(const char* text) {
    return text;
}

inline const char* trc(const char* text) {
    return translator().get(text).data();
}

template <typename... Args>
std::string trf(std::string_view text, const Args&... args) {
    return translator().format(text, args...);
}

void loadTranslations(const std::string& language, const std::filesystem::path& dataRoot);
std::vector<std::string> builtinLanguages();

}

#include "i18n.hpp"

#include <algorithm>
#include <fstream>

#include "lang_data.hpp"

namespace app {

st::Translator& translator() {
    static st::Translator instance;
    return instance;
}

void loadTranslations(const std::string& language, const std::filesystem::path& dataRoot) {
    auto& active = translator();
    active.clear();
    if (language == "en") return;
    for (const auto& [code, json] : kBuiltinLanguages)
        if (code == language) active.load(json);
    std::ifstream stream(dataRoot / "lang" / (language + ".json"), std::ios::binary);
    if (stream) active.load(std::string(std::istreambuf_iterator<char>(stream), {}));
}

std::vector<std::string> builtinLanguages() {
    std::vector<std::string> codes{"en"};
    for (const auto& [code, json] : kBuiltinLanguages) codes.emplace_back(code);
    return codes;
}

}

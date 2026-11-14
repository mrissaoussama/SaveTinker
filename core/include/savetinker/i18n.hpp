#pragma once

#include <format>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace st {

// Maps English UI text to another language. Files are flat JSON objects: {"Back": "Retour"}.
class Translator {
public:
    // Adds the entries of a translation file, replacing existing ones. Entries whose {} placeholders do not
    // match the English text are skipped and reported.
    bool load(std::string_view json, std::string* error = nullptr, std::vector<std::string>* rejected = nullptr);
    void clear() { entries_.clear(); }
    std::size_t size() const { return entries_.size(); }

    // The translation, or `text` itself when there is none. The returned view stays valid until the next load.
    std::string_view get(std::string_view text) const;

    template <typename... Args>
    std::string format(std::string_view text, const Args&... args) const {
        try {
            return std::vformat(get(text), std::make_format_args(args...));
        } catch (const std::format_error&) {
            return std::vformat(text, std::make_format_args(args...));
        }
    }

private:
    std::map<std::string, std::string, std::less<>> entries_;
};

// Number of {} placeholders, or -1 when braces are unbalanced.
int countPlaceholders(std::string_view text);

}

#include "savetinker/version.hpp"

#include <charconv>

namespace st {

namespace {

std::string_view trim(std::string_view text) {
    while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
    while (!text.empty() && text.back() == ' ') text.remove_suffix(1);
    return text;
}

}

std::optional<Version> Version::parse(std::string_view text) {
    text = trim(text);
    if (!text.empty() && (text.front() == 'v' || text.front() == 'V')) text.remove_prefix(1);
    if (text.empty()) return std::nullopt;

    Version version;
    if (auto dash = text.find('-'); dash != std::string_view::npos) {
        version.preRelease = std::string(text.substr(dash + 1));
        text = text.substr(0, dash);
        if (version.preRelease.empty()) return std::nullopt;
    }

    while (true) {
        const auto dot = text.find('.');
        const auto piece = text.substr(0, dot);
        int number = 0;
        auto [end, error] = std::from_chars(piece.data(), piece.data() + piece.size(), number);
        if (error != std::errc{} || end != piece.data() + piece.size() || piece.empty()) return std::nullopt;
        version.parts.push_back(number);
        if (dot == std::string_view::npos) break;
        text = text.substr(dot + 1);
    }
    return version;
}

std::string Version::toString() const {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) out += '.';
        out += std::to_string(parts[i]);
    }
    if (!preRelease.empty()) out += "-" + preRelease;
    return out;
}

std::strong_ordering Version::operator<=>(const Version& other) const {
    const std::size_t count = std::max(parts.size(), other.parts.size());
    for (std::size_t i = 0; i < count; ++i) {
        const int mine = i < parts.size() ? parts[i] : 0;
        const int theirs = i < other.parts.size() ? other.parts[i] : 0;
        if (auto order = mine <=> theirs; order != 0) return order;
    }
    if (preRelease.empty() != other.preRelease.empty())
        return preRelease.empty() ? std::strong_ordering::greater : std::strong_ordering::less;
    return preRelease <=> other.preRelease;
}

bool versionMatches(std::string_view pattern, std::string_view versionText) {
    pattern = trim(pattern);
    if (pattern == "*") return true;
    const auto version = Version::parse(versionText);
    if (!version) return trim(versionText) == pattern;

    if (auto separator = pattern.find(" - "); separator != std::string_view::npos) {
        const auto low = Version::parse(pattern.substr(0, separator));
        const auto high = Version::parse(pattern.substr(separator + 3));
        return low && high && *version >= *low && *version <= *high;
    }

    struct Operator { std::string_view token; bool (*test)(std::strong_ordering); };
    static constexpr Operator operators[] = {
        {">=", [](std::strong_ordering o) { return o >= 0; }},
        {"<=", [](std::strong_ordering o) { return o <= 0; }},
        {">", [](std::strong_ordering o) { return o > 0; }},
        {"<", [](std::strong_ordering o) { return o < 0; }},
    };
    for (const auto& op : operators) {
        if (pattern.starts_with(op.token)) {
            const auto bound = Version::parse(pattern.substr(op.token.size()));
            return bound && op.test(*version <=> *bound);
        }
    }
    const auto exact = Version::parse(pattern);
    return exact && *version == *exact;
}

}

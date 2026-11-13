#pragma once

#include <compare>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace st {

// Dotted numeric version with optional pre-release tag: "1.2.3", "v2.0", "0.3.0-beta.1".
struct Version {
    std::vector<int> parts;
    std::string preRelease;

    static std::optional<Version> parse(std::string_view text);
    std::string toString() const;

    std::strong_ordering operator<=>(const Version& other) const;
    bool operator==(const Version& other) const { return (*this <=> other) == 0; }
};

// Pattern forms: "*", "1.2.3", ">=1.2", ">1.2", "<=1.2", "<2", "1.0.0 - 1.2.0".
bool versionMatches(std::string_view pattern, std::string_view version);

}

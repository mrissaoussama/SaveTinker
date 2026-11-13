#include <doctest.h>

#include "savetinker/version.hpp"

using st::Version;
using st::versionMatches;

TEST_CASE("parses dotted versions") {
    CHECK(Version::parse("1.2.3")->parts == std::vector<int>{1, 2, 3});
    CHECK(Version::parse("v2")->parts == std::vector<int>{2});
    CHECK(Version::parse("0.3.0-beta.1")->preRelease == "beta.1");
    CHECK_FALSE(Version::parse(""));
    CHECK_FALSE(Version::parse("1..2"));
    CHECK_FALSE(Version::parse("1.x"));
    CHECK_FALSE(Version::parse("1.0-"));
}

TEST_CASE("orders versions") {
    CHECK(*Version::parse("1.10") > *Version::parse("1.9"));
    CHECK(*Version::parse("1.0") == *Version::parse("1.0.0"));
    CHECK(*Version::parse("1.0.0-beta") < *Version::parse("1.0.0"));
    CHECK(*Version::parse("1.0.0-alpha") < *Version::parse("1.0.0-beta"));
}

TEST_CASE("matches version patterns") {
    CHECK(versionMatches("*", "anything"));
    CHECK(versionMatches("1.2.0", "1.2"));
    CHECK(versionMatches(">=1.2", "1.3.1"));
    CHECK_FALSE(versionMatches(">=1.2", "1.1.9"));
    CHECK(versionMatches("<2", "1.99"));
    CHECK_FALSE(versionMatches("<2", "2.0.0"));
    CHECK(versionMatches("1.0.0 - 1.2.0", "1.1.5"));
    CHECK_FALSE(versionMatches("1.0.0 - 1.2.0", "1.2.1"));
    CHECK(versionMatches("Ver.A", "Ver.A"));
}

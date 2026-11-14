#include <doctest.h>

#include "savetinker/manifest.hpp"

namespace {

constexpr const char* kValidManifest = R"({
  "id": "example.my-game",
  "name": "My Game Editor",
  "description": "Gold and items",
  "version": "1.0.0",
  "authors": ["someone"],
  "source": "https://example.com/repo",
  "games": [
    {"platform": "switch", "title_id": "0100abcd00000000", "name": "My Game", "versions": [">=1.0.0"]},
    {"platform": "3ds", "title_id": "0004000000123400"}
  ]
})";

}

TEST_CASE("parses a full manifest") {
    const auto manifest = st::parseManifest(kValidManifest);
    REQUIRE(manifest.has_value());
    CHECK(manifest->id == "example.my-game");
    CHECK(manifest->entry == "main.lua");
    CHECK(manifest->games.size() == 2);
    CHECK(manifest->platforms() == std::vector<std::string>{"switch", "3ds"});

    const auto* game = manifest->findGame("switch", "0100ABCD00000000");
    REQUIRE(game != nullptr);
    CHECK(game->titleId == "0100ABCD00000000");
    CHECK(game->supportsVersion("1.0.2"));
    CHECK_FALSE(game->supportsVersion("0.9"));
    CHECK(manifest->findGame("3ds", "0004000000123400")->supportsVersion("9.9"));
    CHECK(manifest->findGame("3ds", "0100ABCD00000000") == nullptr);
}

TEST_CASE("rejects broken manifests with a reason") {
    CHECK(st::parseManifest("not json").error().starts_with("invalid JSON"));
    CHECK(st::parseManifest(R"({"id":"x","name":"n","version":"1.0","games":[]})").error().find("games") != std::string::npos);
    CHECK(st::parseManifest(R"({"id":"Bad Id","name":"n","version":"1.0","games":[{"platform":"switch","title_id":"0100ABCD00000000"}]})")
              .error().find("id may only") != std::string::npos);
    CHECK(st::parseManifest(R"({"id":"x","name":"n","version":"1.0","games":[{"platform":"switch","title_id":"123"}]})")
              .error().find("title_id") != std::string::npos);
    CHECK(st::parseManifest(R"({"id":"x","name":"n","version":"1.0","api_version":99,"games":[{"platform":"switch","title_id":"0100ABCD00000000"}]})")
              .error().find("API version") != std::string::npos);
}

TEST_CASE("platform names") {
    CHECK(st::platformDisplayName("switch") == "Nintendo Switch");
    CHECK(st::platformDisplayName("3ds") == "Nintendo 3DS");
    CHECK(st::platformDisplayName("wiiu") == "wiiu");
}

TEST_CASE("generic editors match every title, exact targets win") {
    const auto manifest = st::parseManifest(R"({"id":"x.unity","name":"Unity saves","version":"1.0.0","icon":"icon.png","games":[
        {"platform":"switch","title_id":"*","name":"Unity games"},
        {"platform":"switch","title_id":"0100ABCD00000000","versions":["1.0"]}]})");
    REQUIRE(manifest.has_value());
    CHECK(manifest->generic());
    CHECK(manifest->icon == "icon.png");
    CHECK(manifest->findGame("switch", "0100FFFF00000000")->generic());
    CHECK_FALSE(manifest->findGame("switch", "0100abcd00000000")->generic());
    CHECK(manifest->findGame("3ds", "0004000000123400") == nullptr);

    const auto anywhere = st::parseManifest(R"({"id":"x.any","name":"n","version":"1.0","games":[{"platform":"*","title_id":"*"}]})");
    REQUIRE(anywhere.has_value());
    CHECK(anywhere->findGame("3ds", "0004000000123400") != nullptr);
    CHECK(st::parseManifest(R"({"id":"x","name":"n","version":"1.0","games":[{"platform":"*","title_id":"0100ABCD00000000"}]})")
              .error().find("platform") != std::string::npos);
    CHECK(st::parseManifest(R"({"id":"x","name":"n","version":"1.0","icon":"../up.png","games":[{"platform":"*","title_id":"*"}]})")
              .error().find("icon") != std::string::npos);
}

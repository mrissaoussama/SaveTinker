#include <doctest.h>

#include "savetinker/repository.hpp"

namespace {

constexpr const char* kRepoJson = R"({
  "name": "Community Editors",
  "maintainer": "someone",
  "editors": [
    {
      "id": "example.my-game",
      "name": "My Game Editor",
      "version": "1.2.0",
      "games": [
        {"platform": "switch", "title_id": "0100ABCD00000000", "versions": ["1.0.0", "1.1.0"]},
        {"platform": "3ds", "title_id": "0004000000123400"}
      ],
      "download": "editors/my-game-1.2.0.zip",
      "sha256": "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855",
      "size": 1234
    },
    {
      "id": "example.other",
      "name": "Other",
      "version": "0.1",
      "games": [{"platform": "switch", "title_id": "0100ABCD00000000", "versions": ["2.0.0"]}],
      "download": "https://cdn.example.com/other.zip"
    }
  ]
})";

st::RepoSource makeSource(const std::string& url) {
    auto index = st::parseRepoIndex(kRepoJson, url);
    REQUIRE(index.has_value());
    return st::RepoSource{url, true, *index};
}

}

TEST_CASE("parses repo index and collects platforms") {
    const auto index = st::parseRepoIndex(kRepoJson, "https://example.com/repo/index.json");
    REQUIRE(index.has_value());
    CHECK(index->name == "Community Editors");
    CHECK(index->editors.size() == 2);
    CHECK(index->platforms == std::vector<std::string>{"switch", "3ds"});
    CHECK(index->editors[0].downloadUrl == "https://example.com/repo/editors/my-game-1.2.0.zip");
    CHECK(index->editors[0].sha256 == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(index->editors[0].size == 1234);
    CHECK(index->editors[1].downloadUrl == "https://cdn.example.com/other.zip");
}

TEST_CASE("resolves relative urls") {
    CHECK(st::resolveUrl("https://a.com/x/index.json", "./b.zip") == "https://a.com/x/b.zip");
    CHECK(st::resolveUrl("https://a.com/x/index.json", "/root.zip") == "https://a.com/root.zip");
    CHECK(st::resolveUrl("https://a.com/x/", "b.zip") == "https://a.com/x/b.zip");
    CHECK(st::resolveUrl("", "b.zip") == "b.zip");
}

TEST_CASE("rejects bad editor entries") {
    CHECK_FALSE(st::parseRepoIndex(R"({"editors":[]})").has_value());
    const auto badHash = st::parseRepoIndex(R"({"name":"r","editors":[{"id":"a","name":"A","version":"1","download":"a.zip","sha256":"xyz",
        "games":[{"platform":"switch","title_id":"0100ABCD00000000"}]}]})");
    REQUIRE_FALSE(badHash.has_value());
    CHECK(badHash.error().find("sha256") != std::string::npos);
}

TEST_CASE("finds editors across several repos") {
    st::RepoCatalog catalog;
    catalog.setRepo(makeSource("https://one.example/index.json"));
    catalog.setRepo(makeSource("https://two.example/index.json"));

    auto matches = catalog.findEditors("switch", "0100abcd00000000", "2.0.0");
    REQUIRE(matches.size() == 4);
    CHECK(matches[0].versionSupported);
    CHECK(matches[0].editor->id == "example.other");
    CHECK_FALSE(matches[3].versionSupported);

    CHECK(catalog.findEditors("3ds", "0004000000123400").size() == 2);
    CHECK(catalog.findEditors("switch", "0100000000000000").empty());

    catalog.setRepo({"https://two.example/index.json", false, {}});
    CHECK(catalog.findEditors("3ds", "0004000000123400").size() == 1);
    CHECK(catalog.removeRepo("https://one.example/index.json"));
    CHECK(catalog.findEditors("3ds", "0004000000123400").empty());
}

TEST_CASE("reports updates only from the repo an editor came from") {
    st::RepoCatalog catalog;
    catalog.setRepo(makeSource("https://one.example/index.json"));
    const auto updates = catalog.findUpdates({
        {"example.my-game", "1.1.0", "https://one.example/index.json"},
        {"example.other", "0.1", "https://one.example/index.json"},
        {"example.my-game", "1.0.0", "https://elsewhere.example/index.json"},
    });
    REQUIRE(updates.size() == 1);
    CHECK(updates[0].editor->version == "1.2.0");
    CHECK(updates[0].installedVersion == "1.1.0");
}

#include <doctest.h>

#include "savetinker/code_inspect.hpp"
#include "savetinker/game_data.hpp"
#include "savetinker/manifest.hpp"
#include "savetinker/script_host.hpp"
#include "test_support.hpp"

namespace {

st::EditorManifest manifestWithGameData(bool gameData) {
    auto manifest = *st::parseManifest(R"({"id": "a", "name": "A", "version": "1.0.0",
        "games": [{"platform": "switch", "title_id": "0100000000001000"}]})");
    manifest.gameData = gameData;
    return manifest;
}

st::ModuleLoader noModules() {
    return [](std::string_view) -> std::optional<std::string> { return "function build() end"; };
}

}

TEST_CASE("directory game data reads and lists files inside its root only") {
    test::TempDir dir;
    test::writeFile(dir.path() / "romfs" / "items.txt", "Potion\nEther\n");
    test::writeFile(dir.path() / "romfs" / "data" / "a.bin", "xy");
    test::writeFile(dir.path() / "secret.txt", "no");
    st::DirectoryGameData data(dir.path() / "romfs");

    CHECK(data.read("items.txt", 100) == test::bytesOf("Potion\nEther\n"));
    CHECK(data.read("/data/a.bin", 100) == test::bytesOf("xy"));
    CHECK_FALSE(data.read("missing.txt", 100).has_value());
    CHECK_THROWS(data.read("items.txt", 4));
    CHECK_THROWS(data.read("../secret.txt", 100));

    const auto entries = data.list("");
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].name == "data");
    CHECK(entries[0].directory);
    CHECK(entries[1].name == "items.txt");
    CHECK(entries[1].size == 13);
}

TEST_CASE("manifest game_data flag") {
    auto manifest = st::parseManifest(R"({"id": "a", "name": "A", "version": "1.0.0", "game_data": true,
        "games": [{"platform": "switch", "title_id": "0100000000001000"}]})");
    REQUIRE(manifest.has_value());
    CHECK(manifest->gameData);
    CHECK_FALSE(st::parseManifest(R"({"id": "a", "name": "A", "version": "1.0.0", "game_data": "yes",
        "games": [{"platform": "switch", "title_id": "0100000000001000"}]})").has_value());
}

TEST_CASE("scripts read game files only when the manifest asks for them") {
    test::TempDir dir;
    test::writeFile(dir.path() / "names.txt", "Link");
    int opened = 0;
    auto opener = [&]() -> std::expected<std::shared_ptr<st::GameData>, std::string> {
        ++opened;
        return std::make_shared<st::DirectoryGameData>(dir.path());
    };

    st::Document document;
    SUBCASE("allowed") {
        st::ScriptHost host(document, {"switch", "0100000000001000", "", "", manifestWithGameData(true)}, noModules());
        host.setGameDataOpener(opener);
        CHECK(host.evaluate("return st.game_data.read('names.txt')").value() == "Link");
        CHECK(host.evaluate("return st.game_data.read('nope.txt', true)").value() == "nil");
        CHECK(host.evaluate("local l = st.game_data.list() return l[1].name .. l[1].size").value() == "names.txt4");
        CHECK(opened == 1);
        auto missing = host.evaluate("return st.game_data.read('nope.txt')");
        REQUIRE_FALSE(missing.has_value());
        CHECK(missing.error().find("not found") != std::string::npos);
    }
    SUBCASE("not declared") {
        st::ScriptHost host(document, {"switch", "0100000000001000", "", "", manifestWithGameData(false)}, noModules());
        host.setGameDataOpener(opener);
        auto result = host.evaluate("return st.game_data.read('names.txt')");
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().find("game_data") != std::string::npos);
        CHECK(opened == 0);
    }
    SUBCASE("platform without game files") {
        st::ScriptHost host(document, {"switch", "0100000000001000", "", "", manifestWithGameData(true)}, noModules());
        host.setGameDataOpener([]() -> std::expected<std::shared_ptr<st::GameData>, std::string> {
            return std::unexpected("not supported");
        });
        auto result = host.evaluate("return st.game_data.read('names.txt')");
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().find("not supported") != std::string::npos);
    }
}

TEST_CASE("code summary lists game file reads") {
    const auto findings = st::summarizeScript("local names = st.game_data.read('text/items.msbt')\n");
    REQUIRE(findings.size() == 1);
    CHECK(findings[0].kind == st::CodeFinding::Kind::ReadsGameData);
    CHECK(findings[0].detail == "text/items.msbt");
}

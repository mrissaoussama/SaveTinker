#include <doctest.h>

#include "pc_platform.hpp"
#include "savetinker/manifest.hpp"
#include "savetinker/platform.hpp"
#include "savetinker/script_host.hpp"
#include "test_support.hpp"

namespace {

st::ScriptContext gameContext() {
    st::ScriptContext context{"3ds", "0004000000030700", "Game", "1.2.0"};
    context.addOns = {"0004008C00030701", "0004008C00030702"};
    context.saveKind = "extdata";
    context.editor = *st::parseManifest(R"({"id": "a", "name": "A", "version": "1.0.0",
        "games": [{"platform": "3ds", "title_id": "0004000000030700"}]})");
    return context;
}

}

TEST_CASE("scripts see installed DLC, the save kind and the game version") {
    st::Document document;
    st::ScriptHost host(document, gameContext(), [](std::string_view) -> std::optional<std::string> { return ""; });
    CHECK(host.evaluate("return #st.game.dlc").value() == "2");
    CHECK(host.evaluate("return st.game.has_dlc('0004008C00030702')").value() == "true");
    CHECK(host.evaluate("return st.game.has_dlc('30701')").value() == "true");
    CHECK(host.evaluate("return st.game.has_dlc('0004008C00099999')").value() == "false");
    CHECK(host.evaluate("return st.game.save_kind").value() == "extdata");
    CHECK(host.evaluate("return st.game.version_is('>=1.1.0')").value() == "true");
    CHECK(host.evaluate("return st.game.version_is('1.0.0 - 1.1.9')").value() == "false");
    CHECK(host.evaluate("return st.version_matches('*', '9.9.9')").value() == "true");
}

TEST_CASE("detect can explain why it rejects a save") {
    st::Document document;
    st::ScriptHost host(document, gameContext(), [](std::string_view) -> std::optional<std::string> {
        return "function detect(save) if st.game.version_is('<2.0.0') then return false, 'Needs the 2.0 update' end return true end";
    });
    REQUIRE(host.loadEntry().has_value());
    CHECK_FALSE(host.detect().value());
    CHECK(host.detectReason() == "Needs the 2.0 update");
}

TEST_CASE("new save kinds round trip through their names") {
    CHECK(st::parseSaveKind("extdata") == st::SaveKind::ExtData);
    CHECK(st::parseSaveKind("ds-card") == st::SaveKind::DsCard);
    CHECK(st::saveKindName(st::SaveKind::ExtData) == "extdata");
    CHECK(st::platformDisplayName("nds") == "Nintendo DS");
}

TEST_CASE("the desktop console reads DLC lists and extra data folders") {
    test::TempDir dir;
    test::writeFile(dir.path() / "titles.json",
                    R"([{"title_id": "0004000000030700", "name": "Kart", "version": "1.2", "dlc": ["0004008c00030701"]}])");
    test::writeFile(dir.path() / "saves" / "0004000000030700" / "extdata" / "data.bin", "x");
    st::PcPlatform platform(dir.path(), "3ds");
    const auto titles = platform.titles();
    REQUIRE(titles.size() == 1);
    CHECK(titles[0].addOns == std::vector<std::string>{"0004008C00030701"});
    const auto saves = platform.saves();
    REQUIRE(saves.size() == 1);
    CHECK(saves[0].kind == st::SaveKind::ExtData);
}

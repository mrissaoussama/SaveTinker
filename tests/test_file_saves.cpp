#include <doctest.h>

#include "pc_platform.hpp"
#include "savetinker/file_saves.hpp"
#include "test_support.hpp"

namespace {

std::string gbaRom(std::string_view title, std::string_view code) {
    std::string rom(0xC0, '\0');
    rom.replace(0xA0, title.size(), title);
    rom.replace(0xAC, code.size(), code);
    rom[0xB2] = static_cast<char>(0x96);
    return rom;
}

std::string ndsRom(std::string_view title, std::string_view code) {
    std::string rom(0x200, '\0');
    rom.replace(0, title.size(), title);
    rom.replace(0x0C, code.size(), code);
    return rom;
}

}

TEST_CASE("ROM headers identify GBA and DS games") {
    test::TempDir dir;
    test::writeFile(dir.path() / "emerald.gba", gbaRom("POKEMON EMER", "BPEE"));
    test::writeFile(dir.path() / "pearl.nds", ndsRom("POKEMON PL", "CPUE"));
    test::writeFile(dir.path() / "junk.gba", std::string(0xC0, 'x'));

    auto gba = st::readRomIdentity(dir.path() / "emerald.gba");
    REQUIRE(gba.has_value());
    CHECK(gba->platform == "gba");
    CHECK(gba->gameCode == "BPEE");
    CHECK(gba->title == "POKEMON EMER");
    auto nds = st::readRomIdentity(dir.path() / "pearl.nds");
    REQUIRE(nds.has_value());
    CHECK(nds->platform == "nds");
    CHECK(nds->gameCode == "CPUE");
    CHECK_FALSE(st::readRomIdentity(dir.path() / "junk.gba").has_value());
    CHECK(st::cartridgeTitleId("BPEE") == "0000000042504545");
}

TEST_CASE("loose save files appear as titles and open as save.bin") {
    test::TempDir console;
    test::writeFile(console.path() / "titles.json", "[]");
    test::writeFile(console.path() / "gba" / "emerald.gba", gbaRom("POKEMON EMER", "BPEE"));
    test::writeFile(console.path() / "gba" / "emerald.sav", std::string(128, '\x01'));
    test::writeFile(console.path() / "gba" / "homebrew.sav", "data");
    test::writeFile(console.path() / "gba" / "notes.txt", "ignored");

    st::FileSavePlatform platform(std::make_unique<st::PcPlatform>(console.path()));
    platform.setFolders({console.path() / "gba"});

    const auto titles = platform.titles();
    REQUIRE(titles.size() == 2);
    const auto emerald = std::ranges::find(titles, "0000000042504545", &st::TitleInfo::titleId);
    REQUIRE(emerald != titles.end());
    CHECK(emerald->platform == "gba");
    CHECK(emerald->name == "POKEMON EMER");
    const auto unknown = std::ranges::find(titles, "file", &st::TitleInfo::platform);
    REQUIRE(unknown != titles.end());
    CHECK(unknown->name == "homebrew");

    const auto saves = platform.saves();
    REQUIRE(saves.size() == 2);
    const auto save = std::ranges::find(saves, "0000000042504545", &st::SaveInfo::titleId);
    REQUIRE(save != saves.end());
    CHECK(save->kind == st::SaveKind::File);
    CHECK(save->slotName() == "file-emerald");

    auto mount = platform.mount(*save);
    REQUIRE(mount.has_value());
    auto document = st::loadDocument(**mount);
    CHECK(document.fileNames() == std::vector<std::string>{"save.bin"});
    document.write("save.bin", 0, st::Bytes{0xAA});
    st::storeDocument(document, **mount);
    CHECK(test::readFile(console.path() / "gba" / "emerald.sav")[0] == '\xAA');
    CHECK_FALSE(std::filesystem::exists(console.path() / "gba" / "emerald.sav.savetinker-tmp"));
}

TEST_CASE("desktop saves are replaced atomically") {
    test::TempDir console;
    test::writeFile(console.path() / "saves" / "0100000000001000" / "device" / "save.dat", "old");
    st::PcPlatform platform(console.path());
    auto mount = platform.mount(platform.saves().front());
    REQUIRE(mount.has_value());
    (*mount)->write("save.dat", test::viewOf("new contents"));
    CHECK(test::readFile(console.path() / "saves" / "0100000000001000" / "device" / "save.dat") == "new contents");
    CHECK_FALSE(std::filesystem::exists(console.path() / "saves" / "0100000000001000" / "device" / "save.dat.savetinker-tmp"));
}

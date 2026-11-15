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

TEST_CASE("added save locations guess names and title ids from the path") {
    test::TempDir dir;
    test::writeFile(dir.path() / "Sandfall" / "Saved" / "SaveGames" / "76561197960271872" / "EXPEDITION_0.sav", "GVAS");
    test::writeFile(dir.path() / "nand" / "user" / "save" / "0000000000000000" / "0123456789ABCDEF0123456789ABCDEF" /
                        "01003fb00c5a8000" / "save.dat", "x");
    test::writeFile(dir.path() / "sdmc" / "title" / "00040000" / "00030700" / "data" / "00000001" / "system1.dat", "x");
    test::writeFile(dir.path() / "MyRpg" / "www" / "data" / "System.json", "{}");
    test::writeFile(dir.path() / "MyRpg" / "www" / "save" / "file1.rpgsave", "x");

    const auto unreal = st::describeSaveLocation(dir.path() / "Sandfall" / "Saved" / "SaveGames" / "76561197960271872" / "EXPEDITION_0.sav");
    CHECK(unreal.name == "Sandfall - EXPEDITION_0");
    CHECK(unreal.platform == "pc");
    CHECK(unreal.titleId.empty());
    CHECK(st::saveLocationTitleId(unreal).starts_with("FFFFFFFE"));

    const auto yuzu = st::describeSaveLocation(dir.path() / "nand" / "user" / "save" / "0000000000000000" /
                                               "0123456789ABCDEF0123456789ABCDEF" / "01003fb00c5a8000");
    CHECK(yuzu.platform == "switch");
    CHECK(yuzu.titleId == "01003FB00C5A8000");

    const auto azahar = st::describeSaveLocation(dir.path() / "sdmc" / "title" / "00040000" / "00030700" / "data" / "00000001");
    CHECK(azahar.platform == "3ds");
    CHECK(azahar.titleId == "0004000000030700");

    const auto steam = st::describeSaveLocation(dir.path() / "Steam" / "userdata" / "153776498" / "1903340" / "remote");
    CHECK(steam.platform == "pc");
    CHECK(steam.titleId == "00000000001D0AEC");
    CHECK(steam.name == "Steam app 1903340");

    const auto rpg = st::describeSaveLocation(dir.path() / "MyRpg" / "www" / "save");
    CHECK(rpg.name == "MyRpg");
    CHECK(st::rpgMakerDataFolder(dir.path() / "MyRpg" / "www" / "save" / "file1.rpgsave") == dir.path() / "MyRpg" / "www" / "data");
}

TEST_CASE("added save files keep their names and folders open as they are") {
    test::TempDir console;
    test::writeFile(console.path() / "titles.json", "[]");
    test::writeFile(console.path() / "game" / "save" / "file1.rpgsave", "one");
    test::writeFile(console.path() / "game" / "save" / "global.rpgsave", "two");
    test::writeFile(console.path() / "game" / "data" / "System.json", "{}");
    test::writeFile(console.path() / "other" / "Slot1.sav", "GVAS");

    st::FileSavePlatform platform(std::make_unique<st::PcPlatform>(console.path()));
    auto folder = st::describeSaveLocation(console.path() / "game" / "save");
    auto file = st::describeSaveLocation(console.path() / "other" / "Slot1.sav");
    platform.setLocations({folder, file});

    const auto titles = platform.titles();
    REQUIRE(titles.size() == 2);
    CHECK(titles[0].platform == "pc");
    const auto saves = platform.saves();
    REQUIRE(saves.size() == 2);

    auto folderMount = platform.mount(saves[0]);
    REQUIRE(folderMount.has_value());
    CHECK((*folderMount)->listFiles().size() == 2);
    auto fileMount = platform.mount(saves[1]);
    REQUIRE(fileMount.has_value());
    REQUIRE((*fileMount)->listFiles().size() == 1);
    CHECK((*fileMount)->listFiles()[0].path == "Slot1.sav");
    CHECK(platform.openGameData(saves[0].titleId).has_value());
}

TEST_CASE("editors find PC saves from their save paths") {
    test::TempDir root;
    test::writeFile(root.path() / "Game" / "Saved" / "SaveGames" / "111" / "Slot1.sav", "GVAS");
    test::writeFile(root.path() / "Game" / "Saved" / "SaveGames" / "111" / "Slot2.sav", "GVAS");
    test::writeFile(root.path() / "Game" / "Saved" / "SaveGames" / "111" / "Settings.sav", "GVAS");
    test::writeFile(root.path() / "Store" / "wgs" / "Slot1.sav", "GVAS");
    test::writeFile(root.path() / "titles.json", "[]");

    const auto base = st::pathToUtf8(root.path());
    CHECK(st::expandSavePath(base + "/Game/Saved/SaveGames/*/slot?.sav").size() == 2);
    CHECK(st::expandSavePath(base + "/Missing/*.sav").empty());
    CHECK(st::expandSavePath("%NOT_A_FOLDER%/x").empty());

    st::GameTarget game{"pc", st::steamTitleId(1903340), "Clair Obscur", {},
                        {base + "/Game/Saved/SaveGames/*/Slot*.sav", base + "/Store/*/Slot*.sav"}};
    st::FileSavePlatform platform(std::make_unique<st::PcPlatform>(root.path()));
    platform.setKnownGames({game});
    const auto titles = platform.titles();
    REQUIRE(titles.size() == 1);
    CHECK(titles[0].name == "Clair Obscur");
    const auto saves = platform.saves();
    REQUIRE(saves.size() == 3);
    auto mount = platform.mount(saves[0]);
    REQUIRE(mount.has_value());
    CHECK((*mount)->listFiles()[0].path == "Slot1.sav");
}

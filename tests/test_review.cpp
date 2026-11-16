#include <doctest.h>

#include <algorithm>

#include "savetinker/authoring.hpp"
#include "savetinker/backup.hpp"
#include "savetinker/editor_session.hpp"
#include "savetinker/editor_store.hpp"
#include "savetinker/file_saves.hpp"
#include "savetinker/game_data.hpp"
#include "savetinker/manifest.hpp"
#include "savetinker/repository.hpp"
#include "test_support.hpp"

using st::Bytes;
using st::UiNode;
using st::UiValue;

namespace {

struct Session {
    st::Document document;
    std::unique_ptr<st::ScriptHost> host;
    std::unique_ptr<st::EditorSession> session;

    explicit Session(const std::string& code, st::ModuleLoader extra = {}) {
        document.addFile("save.dat", Bytes{10, 20, 30, 40, 0, 0, 0, 0});
        st::ScriptContext context;
        context.platform = "switch";
        host = std::make_unique<st::ScriptHost>(document, context, [code, extra](std::string_view name) -> std::optional<std::string> {
            if (name == "main") return code;
            if (extra) return extra(name);
            return std::nullopt;
        });
        REQUIRE(host->loadEntry().has_value());
        session = std::make_unique<st::EditorSession>(*host);
        REQUIRE(session->rebuild().has_value());
    }

    const UiNode& node(const std::string& label) const {
        for (const auto& n : session->currentPage().nodes)
            if (n.label == label) return n;
        FAIL("no node " << label);
        throw std::logic_error("unreachable");
    }
};

constexpr const char* kFields = R"(
function build(ui, save)
    local file = save:file("save.dat")
    ui:int{ label = "Gold", bind = file:field(0, "u8") }
    ui:dropdown{ label = "Class", bind = file:field(1, "u8"), options = { [20] = "Knight", [21] = "Mage" } }
    ui:toggle{ label = "Hard", bind = file:flag(4, 0) }
end
)";

}

TEST_CASE("documents load big files only when read") {
    st::Document document;
    int loads = 0;
    document.addLazyFile("big.bin", 4, [&] {
        ++loads;
        return Bytes{1, 2, 3, 4};
    });
    CHECK(document.hasFile("big.bin"));
    CHECK(document.fileSize("big.bin") == 4);
    CHECK(loads == 0);
    CHECK(document.file("big.bin").read<std::uint8_t>(2) == 3);
    CHECK(document.file("big.bin").size() == 4);
    CHECK(loads == 1);
    CHECK(document.touchedFiles().empty());
}

TEST_CASE("original content is shown on request and only edited files are touched") {
    st::Document document;
    document.addFile("a", Bytes{1, 2});
    document.addFile("b", Bytes{3, 4});
    const auto before = document.revision("a");
    document.write("a", 0, test::viewOf("\x09"));
    CHECK(document.revision("a") > before);
    CHECK(document.touchedFiles() == std::vector<std::string>{"a"});
    const auto seen = document.withOriginals([&] { return document.file("a").read<std::uint8_t>(0); });
    CHECK(seen == 1);
    CHECK(document.file("a").read<std::uint8_t>(0) == 9);
    document.undo();
    CHECK(document.file("a").read<std::uint8_t>(0) == 1);
    // Still listed: it may differ from what an earlier write put on disk.
    CHECK(document.touchedFiles() == std::vector<std::string>{"a"});
}

TEST_CASE("edited fields are listed with their loaded and current values, and can be reset") {
    Session s(kFields);
    REQUIRE(s.session->setValue(s.node("Gold"), UiValue{std::int64_t{99}}).has_value());
    REQUIRE(s.session->setValue(s.node("Class"), UiValue{std::int64_t{21}}).has_value());
    REQUIRE(s.session->setValue(s.node("Hard"), UiValue{true}).has_value());
    CHECK(s.session->changed(s.node("Gold")));

    auto changes = s.session->fieldChanges();
    REQUIRE(changes.size() == 3);
    CHECK(changes[0].label == "Gold");
    CHECK(changes[0].before == "10");
    CHECK(changes[0].after == "99");
    CHECK(changes[1].before == "Knight");
    CHECK(changes[1].after == "Mage");
    CHECK(changes[2].before == "Off");
    CHECK(changes[2].after == "On");

    REQUIRE(s.session->reset(s.node("Class")).has_value());
    CHECK(s.document.file("save.dat").read<std::uint8_t>(1) == 20);
    CHECK(!s.session->changed(s.node("Class")));
    CHECK(s.session->fieldChanges().size() == 2);

    // Undoing the reset brings Mage back; undoing Hard drops it from the list.
    REQUIRE(s.session->undo().has_value());
    REQUIRE(s.session->undo().has_value());
    changes = s.session->fieldChanges();
    REQUIRE(changes.size() == 2);
    CHECK(changes[0].label == "Gold");
    CHECK(changes[1].after == "Mage");
}

TEST_CASE("save_version reports the save format") {
    Session s("function save_version(save) return tostring(save:file('save.dat'):u8(0)) end\nfunction build(ui) end");
    auto version = s.host->saveVersion();
    REQUIRE(version.has_value());
    REQUIRE(version->has_value());
    CHECK(**version == "10");
    st::GameTarget game;
    game.saveVersions = {">=10"};
    CHECK(game.supportsSaveVersion("10"));
    CHECK(!game.supportsSaveVersion("9"));
}

TEST_CASE("manifests describe libraries, dependencies and save versions") {
    auto library = st::parseManifest(R"({"id": "me.lib", "name": "Lib", "version": "1.0.0", "kind": "library"})");
    REQUIRE(library.has_value());
    CHECK(library->library());
    CHECK(library->games.empty());

    auto editor = st::parseManifest(R"({"id": "me.ed", "name": "Ed", "version": "1.0.0", "full_memory": true,
        "dependencies": ["me.lib", {"id": "me.other", "version": ">=2.0.0"}],
        "games": [{"platform": "switch2", "title_id": "0100000000000001", "save_versions": ["3"]}]})");
    REQUIRE(editor.has_value());
    CHECK(editor->fullMemory);
    REQUIRE(editor->dependencies.size() == 2);
    CHECK(editor->dependencies[1].version == ">=2.0.0");
    CHECK(editor->games[0].saveVersions == std::vector<std::string>{"3"});
    CHECK(!st::parseManifest(R"({"id": "me.x", "name": "X", "version": "1.0.0", "games": []})").has_value());
    CHECK(!st::parseManifest(R"({"id": "me.x", "name": "X", "version": "1.0.0", "kind": "plugin", "games": []})").has_value());
}

TEST_CASE("switch editors also open saves on a Switch 2") {
    st::GameTarget target{"switch", "0100000000000001", "", {}, {}};
    CHECK(target.matches("switch2", "0100000000000001"));
    st::GameTarget only2{"switch2", "0100000000000001", "", {}, {}};
    CHECK(!only2.matches("switch", "0100000000000001"));
}

TEST_CASE("installed dependencies are found, missing ones reported, and their modules required") {
    test::TempDir dir;
    test::writeFile(dir.path() / "me.lib/manifest.json", R"({"id": "me.lib", "name": "Lib", "version": "1.2.0", "kind": "library",
        "dependencies": ["me.base"]})");
    test::writeFile(dir.path() / "me.lib/util/math.lua", "return { double = function(x) return x * 2 end }");
    test::writeFile(dir.path() / "me.ed/manifest.json", R"({"id": "me.ed", "name": "Ed", "version": "1.0.0",
        "dependencies": [{"id": "me.lib", "version": ">=1.0.0"}], "games": [{"platform": "*", "title_id": "*"}]})");
    st::EditorStore store(dir.path());
    const auto editor = store.find("me.ed");
    REQUIRE(editor.has_value());
    const auto missing = store.missingDependencies(editor->manifest);
    REQUIRE(missing.size() == 1);
    CHECK(missing[0].id == "me.base");
    CHECK(store.dependents("me.lib") == std::vector<std::string>{"Ed"});

    const auto folders = store.dependencyFolders(editor->manifest);
    REQUIRE(folders.size() == 1);
    auto loader = st::packageModuleLoader(editor->directory, folders);
    CHECK(loader("util.math").has_value());

    Session s("local m = require('util.math')\nfunction build(ui) ui:label{ label = 'v', value = m.double(21) } end", loader);
    CHECK(s.node("v").display == "42");
}

TEST_CASE("the catalog finds the newest package that fits a version pattern") {
    st::RepoCatalog catalog;
    auto index = st::parseRepoIndex(R"({"name": "R", "editors": [
        {"id": "me.lib", "name": "Lib", "version": "1.0.0", "kind": "library", "download": "a.zip"},
        {"id": "me.lib", "name": "Lib", "version": "2.1.0", "kind": "library", "download": "b.zip"}]})", "https://x/repo.json");
    REQUIRE(index.has_value());
    catalog.setRepo({"https://x/repo.json", true, *index});
    CHECK(catalog.findPackage("me.lib").editor->version == "2.1.0");
    CHECK(catalog.findPackage("me.lib", "<2.0.0").editor->version == "1.0.0");
    CHECK(catalog.findPackage("me.none").editor == nullptr);
}

TEST_CASE("a save is imported from a plain folder, writing files before removing extras") {
    test::TempDir dir;
    test::writeFile(dir.path() / "save/keep.bin", "old");
    test::writeFile(dir.path() / "save/extra.bin", "gone");
    test::writeFile(dir.path() / "import/keep.bin", "new");
    test::writeFile(dir.path() / "import/sub/added.bin", "added");
    st::DirectorySaveMount mount(dir.path() / "save");
    st::BackupManager backups(dir.path() / "backups");
    REQUIRE(backups.importFolder(dir.path() / "import", mount).has_value());
    CHECK(test::readFile(dir.path() / "save/keep.bin") == "new");
    CHECK(test::readFile(dir.path() / "save/sub/added.bin") == "added");
    CHECK(!std::filesystem::exists(dir.path() / "save/extra.bin"));

    st::SaveInfo info;
    info.titleId = "0100000000000001";
    auto record = backups.create(mount, info, "Game", "test");
    REQUIRE(record.has_value());
    REQUIRE(backups.exportTo(*record, dir.path() / "export").has_value());
    CHECK(test::readFile(dir.path() / "export/keep.bin") == "new");
}

TEST_CASE("roms are found next to a save or one folder up") {
    test::TempDir dir;
    std::string header(0xC0, '\0');
    header.replace(0, 12, "MYGAMETITLE ");
    header.replace(0x0C, 4, "ABCE");
    test::writeFile(dir.path() / "roms/nds/Game.nds", header);
    test::writeFile(dir.path() / "roms/nds/saves/Game.sav", "save");
    const auto identity = st::findRomForSave(dir.path() / "roms/nds/saves/Game.sav");
    REQUIRE(identity.has_value());
    CHECK(identity->gameCode == "ABCE");
    const auto location = st::describeSaveLocation(dir.path() / "roms/nds/saves/Game.sav");
    CHECK(location.platform == "nds");
    CHECK(location.titleId == st::cartridgeTitleId("ABCE"));
}

TEST_CASE("zip, memfile views and cartridge helpers work from Lua") {
    Session s("function build(ui) end");
    auto zip = s.host->evaluate(R"(
        local data = st.zip.write({ { name = "log", data = "abc" }, { name = "sub/x", data = "yz" } })
        local back = st.zip.read(data)
        return #back .. back[1].name .. back[1].data .. back[2].data)");
    REQUIRE(zip.has_value());
    CHECK(*zip == "2logabcyz");

    auto view = s.host->evaluate(R"(
        local memfile = require("st.memfile")
        local file = save_for_test or nil
        local m = memfile.new(string.pack("<I4I2", 7, 9))
        m:set(4, "u16", 300)
        m:splice(0, 4, "")
        return m:size() .. ":" .. m:u16(0))");
    REQUIRE(view.has_value());
    CHECK(*view == "2:300");
}

TEST_CASE("mod folders are read before the game files, even when the game files cannot be opened") {
    test::TempDir dir;
    test::writeFile(dir.path() / "game/data/a.txt", "game a");
    test::writeFile(dir.path() / "game/data/b.txt", "game b");
    test::writeFile(dir.path() / "mods/data/a.txt", "mod a");
    auto layered = st::withModFolders(std::make_shared<st::DirectoryGameData>(dir.path() / "game"), {dir.path() / "mods", dir.path() / "missing"});
    CHECK(test::readFile(dir.path() / "mods/data/a.txt") == "mod a");
    auto a = layered->read("data/a.txt", 1024);
    auto b = layered->read("data/b.txt", 1024);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(std::string(a->begin(), a->end()) == "mod a");
    CHECK(std::string(b->begin(), b->end()) == "game b");
    CHECK(layered->list("data").size() == 2);

    auto broken = st::withModFolders(std::make_shared<st::UnavailableGameData>("no keys"), {dir.path() / "mods"});
    auto modded = broken->read("data/a.txt", 1024);
    REQUIRE(modded.has_value());
    CHECK_THROWS_WITH(broken->read("data/b.txt", 1024), "no keys");
}

TEST_CASE("a lazy file whose first read fails can be read again") {
    st::Document document;
    int loads = 0;
    document.addLazyFile("big.bin", 2, [&] {
        if (++loads == 1) throw std::runtime_error("busy");
        return Bytes{7, 8};
    });
    CHECK_THROWS(document.file("big.bin"));
    CHECK(document.hasFile("big.bin"));
    CHECK(document.file("big.bin").read<std::uint8_t>(1) == 8);
}

TEST_CASE("the loaded value survives an edit that changes the file size") {
    // Gold sits a fixed distance from the end, so inserting a byte at the start moves it.
    Session s(R"(
function build(ui, save)
    local file = save:file("save.dat")
    ui:int{ label = "Gold", bind = file:field(file:size() - 5, "u8") }
    ui:button{ label = "Grow", run = function() file:splice(0, 0, "\7") end }
end
)");
    REQUIRE(s.session->press(s.node("Grow")).has_value());
    CHECK(s.node("Gold").value == UiValue{std::int64_t{40}});
    REQUIRE(s.session->setValue(s.node("Gold"), UiValue{std::int64_t{99}}).has_value());
    auto changes = s.session->fieldChanges();
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].before == "40");
    REQUIRE(s.session->reset(s.node("Gold")).has_value());
    CHECK(s.document.file("save.dat").read<std::uint8_t>(4) == 40);
}

TEST_CASE("fields that share a widget id are tracked separately") {
    Session s(R"(
function build(ui, save)
    local file = save:file("save.dat")
    local slot = ui:state("slot", 0)
    ui:int{ label = "Slot", bind = slot }
    ui:int{ label = "HP", bind = file:field(slot:get(), "u8") }
end
)");
    REQUIRE(s.session->setValue(s.node("HP"), UiValue{std::int64_t{99}}).has_value());
    REQUIRE(s.session->setValue(s.node("Slot"), UiValue{std::int64_t{1}}).has_value());
    CHECK(!s.session->changed(s.node("HP")));
    REQUIRE(s.session->setValue(s.node("HP"), UiValue{std::int64_t{55}}).has_value());
    CHECK(s.session->changed(s.node("HP")));
    std::vector<std::string> hp;
    for (const auto& change : s.session->fieldChanges())
        if (change.label == "HP") hp.push_back(change.before + ">" + change.after);
    CHECK(hp == std::vector<std::string>{"10>99", "20>55"});
}

TEST_CASE("a one-file save takes the single file of a folder whatever its name") {
    test::TempDir dir;
    test::writeFile(dir.path() / "game.sav", "old");
    test::writeFile(dir.path() / "one/Pokemon Emerald.sav", "new");
    test::writeFile(dir.path() / "two/a.sav", "a");
    test::writeFile(dir.path() / "two/b.sav", "b");
    st::FileSaveMount mount(dir.path() / "game.sav");
    st::BackupManager backups(dir.path() / "backups");
    CHECK(!backups.importFolder(dir.path() / "two", mount).has_value());
    CHECK(test::readFile(dir.path() / "game.sav") == "old");
    REQUIRE(backups.importFolder(dir.path() / "one", mount).has_value());
    CHECK(test::readFile(dir.path() / "game.sav") == "new");
}

TEST_CASE("libraries are listed apart from editors in repo.json") {
    st::RepoIndex index;
    index.name = "Mine";
    st::RepoEditorEntry library;
    library.id = "me.lib";
    library.name = "Lib";
    library.version = "1.0.0";
    library.kind = std::string(st::kKindLibrary);
    library.downloadUrl = "lib.zip";
    index.editors.push_back(library);
    const auto json = st::repoIndexToJson(index);
    CHECK(json.find("\"editors\": []") != std::string::npos);
    CHECK(json.find("\"libraries\": [") != std::string::npos);
    auto parsed = st::parseRepoIndex(json, "https://host/repo.json");
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->editors.size() == 1);
    CHECK(parsed->editors[0].library());
}

TEST_CASE("Switch 2 titles are published as Switch") {
    CHECK(st::gameTargetPlatform("switch2") == "switch");
    CHECK(st::gameTargetPlatform("3ds") == "3ds");
}

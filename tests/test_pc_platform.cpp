#include <doctest.h>

#include "pc_platform.hpp"
#include "test_support.hpp"

namespace {

void makeConsole(const std::filesystem::path& root) {
    test::writeFile(root / "titles.json", R"([{"title_id":"0100abcd00000000","name":"My Game","version":"1.0.2"}])");
    test::writeFile(root / "users.json", R"([{"id":"AAAA","nickname":"Player"}])");
    test::writeFile(root / "saves/0100ABCD00000000/account-AAAA/save.dat", "hello");
    test::writeFile(root / "saves/0100ABCD00000000/account-AAAA/slots/1.bin", "slot");
    test::writeFile(root / "saves/0100ABCD00000000/device/settings.ini", "x=1");
    test::writeFile(root / "saves/0100ABCD00000000/garbage/file", "ignored");
}

}

TEST_CASE("pc platform lists titles, users and saves") {
    test::TempDir dir;
    makeConsole(dir.path());
    st::PcPlatform platform(dir.path());

    const auto titles = platform.titles();
    REQUIRE(titles.size() == 1);
    CHECK(titles[0].titleId == "0100ABCD00000000");
    CHECK(titles[0].displayVersion == "1.0.2");
    CHECK(platform.users().at(0).nickname == "Player");

    const auto saves = platform.saves();
    REQUIRE(saves.size() == 2);
    const auto account = std::ranges::find(saves, st::SaveKind::Account, &st::SaveInfo::kind);
    REQUIRE(account != saves.end());
    CHECK(account->userId == "AAAA");
    CHECK(account->size == 9);
}

TEST_CASE("mounting refuses while the game runs") {
    test::TempDir dir;
    makeConsole(dir.path());
    st::PcPlatform platform(dir.path());
    st::SaveInfo save{"0100ABCD00000000", st::SaveKind::Device, ""};
    CHECK(platform.mount(save).has_value());
    test::writeFile(dir.path() / "running.txt", "0100abcd00000000");
    CHECK(platform.runningTitleId() == "0100ABCD00000000");
    CHECK_FALSE(platform.mount(save).has_value());
}

TEST_CASE("documents load from and store to a mount") {
    test::TempDir dir;
    makeConsole(dir.path());
    st::PcPlatform platform(dir.path());
    auto mount = platform.mount({"0100ABCD00000000", st::SaveKind::Account, "AAAA"});
    REQUIRE(mount.has_value());

    auto document = st::loadDocument(**mount);
    CHECK(document.fileNames() == std::vector<std::string>{"save.dat", "slots/1.bin"});
    document.write("save.dat", 0, test::viewOf("J"));
    st::storeDocument(document, **mount);
    CHECK(test::readFile(dir.path() / "saves/0100ABCD00000000/account-AAAA/save.dat") == "Jello");
}

TEST_CASE("directory mount blocks path escapes") {
    test::TempDir dir;
    st::DirectorySaveMount mount(dir.path());
    CHECK_THROWS(mount.write("../outside.txt", test::viewOf("x")));
    CHECK_THROWS(mount.read("/etc/passwd"));
}

TEST_CASE("chunked writes commit between chunks") {
    test::TempDir dir;
    int commits = 0;
    st::DirectorySaveMount mount(dir.path(), [&] { ++commits; }, 4);
    mount.write("big.bin", test::viewOf("0123456789"));
    CHECK(commits == 2);
    CHECK(test::readFile(dir.path() / "big.bin") == "0123456789");
    mount.commit();
    CHECK(commits == 3);
}

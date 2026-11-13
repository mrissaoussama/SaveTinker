#include <doctest.h>

#include "savetinker/backup.hpp"
#include "test_support.hpp"

namespace {

struct Fixture {
    test::TempDir dir;
    std::filesystem::path saveDir = dir.path() / "save";
    st::DirectorySaveMount mount{saveDir};
    st::SaveInfo save{"0100ABCD00000000", st::SaveKind::Account, "AAAA"};
    std::chrono::system_clock::time_point now = std::chrono::sys_days{std::chrono::year{2026} / 11 / 14} + std::chrono::hours{9};
    st::BackupManager manager{dir.path() / "backups", [this] { return now; }};

    Fixture() {
        test::writeFile(saveDir / "save.dat", "original");
        test::writeFile(saveDir / "sub/extra.bin", "extra");
    }
};

}

TEST_CASE("creates a backup with metadata and hashes") {
    Fixture f;
    const auto record = f.manager.create(f.mount, f.save, "My Game", "before edit");
    REQUIRE(record.has_value());
    CHECK(record->createdAt == "2026-11-14T09:00:00Z");
    CHECK(record->slot == "account-AAAA");
    CHECK(record->files.size() == 2);
    CHECK(record->totalSize() == 13);
    CHECK(record->directory.filename() == "20261114-090000");
    CHECK(f.manager.verify(*record).has_value());

    const auto listed = f.manager.list("0100ABCD00000000");
    REQUIRE(listed.size() == 1);
    CHECK(listed[0].titleName == "My Game");
    CHECK(listed[0].reason == "before edit");
}

TEST_CASE("same-second backups get distinct folders") {
    Fixture f;
    const auto first = f.manager.create(f.mount, f.save, "G", "a");
    const auto second = f.manager.create(f.mount, f.save, "G", "b");
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(first->directory != second->directory);
    CHECK(f.manager.list("0100ABCD00000000", "account-AAAA").size() == 2);
}

TEST_CASE("restore puts files back and removes new ones") {
    Fixture f;
    const auto record = f.manager.create(f.mount, f.save, "G", "auto");
    REQUIRE(record.has_value());
    test::writeFile(f.saveDir / "save.dat", "changed!");
    test::writeFile(f.saveDir / "new.bin", "new");

    REQUIRE(f.manager.restore(*record, f.mount).has_value());
    CHECK(test::readFile(f.saveDir / "save.dat") == "original");
    CHECK_FALSE(std::filesystem::exists(f.saveDir / "new.bin"));
}

TEST_CASE("restore refuses a tampered backup") {
    Fixture f;
    const auto record = f.manager.create(f.mount, f.save, "G", "auto");
    REQUIRE(record.has_value());
    test::writeFile(record->directory / "files/save.dat", "corrupt");
    const auto result = f.manager.restore(*record, f.mount);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().find("checksum") != std::string::npos);
    CHECK(test::readFile(f.saveDir / "save.dat") == "original");
}

TEST_CASE("prune keeps the newest backups") {
    Fixture f;
    for (int i = 0; i < 4; ++i) {
        REQUIRE(f.manager.create(f.mount, f.save, "G", "auto").has_value());
        f.now += std::chrono::minutes{1};
    }
    CHECK(f.manager.prune("0100ABCD00000000", "account-AAAA", 2) == 2);
    const auto remaining = f.manager.list("0100ABCD00000000");
    REQUIRE(remaining.size() == 2);
    CHECK(remaining[0].directory.filename() == "20261114-090300");
    CHECK(remaining[1].directory.filename() == "20261114-090200");
}

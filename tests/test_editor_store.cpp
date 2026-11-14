#include <doctest.h>

#include "savetinker/editor_store.hpp"
#include "test_support.hpp"

using st::PackageFile;

namespace {

const std::string kManifest = R"({"id":"author.game","name":"Game","version":"1.2.0",
  "games":[{"platform":"switch","title_id":"0100ABCD00000000"},{"platform":"3ds","title_id":"0004000000001000"}]})";

std::vector<PackageFile> editorFiles(const std::string& prefix = "") {
    return {
        {prefix + "manifest.json", test::bytesOf(kManifest)},
        {prefix + "main.lua", test::bytesOf("function detect() return true end")},
        {prefix + "labels/items.json", test::bytesOf("{}")},
    };
}

}

TEST_CASE("zip round trip") {
    const auto zip = st::writeZip(editorFiles());
    const auto files = st::readZip(zip);
    REQUIRE(files.has_value());
    REQUIRE(files->size() == 3);
    CHECK((*files)[0].path == "labels/items.json");
    CHECK((*files)[1].text() == "function detect() return true end");
}

TEST_CASE("packages are validated") {
    auto package = st::openPackage(st::writeZip(editorFiles("game-editor-main/")));
    REQUIRE(package.has_value());
    CHECK(package->manifest.id == "author.game");
    CHECK(package->find("main.lua") != nullptr);

    CHECK(st::openPackage(test::bytesOf("not a zip")).error().find("zip") != std::string::npos);
    CHECK(st::openPackage(st::writeZip({{"main.lua", test::bytesOf("x")}})).error().find("manifest") != std::string::npos);
    CHECK(st::openPackage(st::writeZip({{"manifest.json", test::bytesOf(kManifest)}})).error().find("entry") != std::string::npos);
    CHECK(st::openPackage(st::writeZip({{"../evil.lua", test::bytesOf("x")}})).error().find("unsafe") != std::string::npos);

    st::PackageLimits tight;
    tight.maxTotalSize = 10;
    CHECK(st::openPackage(st::writeZip(editorFiles()), tight).error().find("too large") != std::string::npos);
}

TEST_CASE("install, list, load and remove editors") {
    test::TempDir dir;
    st::EditorStore store(dir.path() / "editors");
    CHECK(store.list().empty());

    auto package = st::openPackage(st::writeZip(editorFiles()));
    REQUIRE(package.has_value());
    auto installed = store.install(*package, "https://repo.example/index.json", "abc");
    REQUIRE(installed.has_value());
    CHECK(installed->repoUrl == "https://repo.example/index.json");
    CHECK(installed->packageSha256 == "abc");
    CHECK_FALSE(installed->installedAt.empty());

    CHECK(store.list().size() == 1);
    CHECK(store.forGame("3ds", "0004000000001000").size() == 1);
    CHECK(store.forGame("switch", "0100000000000000").empty());
    CHECK(store.installedVersions().at(0).version == "1.2.0");

    auto loaded = store.load("author.game");
    REQUIRE(loaded.has_value());
    CHECK(loaded->files.size() == 3);
    CHECK(loaded->find(".savetinker.json") == nullptr);

    REQUIRE(store.install(*package, "https://other.example/index.json", "def").has_value());
    CHECK(store.list().size() == 1);
    CHECK(store.find("author.game")->repoUrl == "https://other.example/index.json");

    REQUIRE(store.remove("author.game").has_value());
    CHECK(store.list().empty());
    CHECK_FALSE(store.remove("author.game").has_value());
}

TEST_CASE("broken editors are reported separately") {
    test::TempDir dir;
    test::writeFile(dir.path() / "broken/manifest.json", "{");
    test::writeFile(dir.path() / "_lib/util.lua", "return {}");
    st::EditorStore store(dir.path());
    std::vector<st::BrokenEditor> broken;
    CHECK(store.list(&broken).empty());
    REQUIRE(broken.size() == 1);
    CHECK(broken[0].directory.filename() == "broken");
}

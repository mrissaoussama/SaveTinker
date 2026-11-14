#include <doctest.h>

#include <map>

#include "savetinker/checksum.hpp"
#include "savetinker/network.hpp"
#include "savetinker/qr.hpp"
#include "savetinker/settings.hpp"
#include "test_support.hpp"

namespace {

class FakeHttpClient : public st::HttpClient {
public:
    std::map<std::string, st::HttpResponse> responses;
    std::vector<std::string> requested;

    std::expected<st::HttpResponse, std::string> get(const std::string& url, std::uint64_t maxBytes,
                                                     const st::ProgressCallback& progress) override {
        requested.push_back(url);
        auto found = responses.find(url);
        if (found == responses.end()) return std::unexpected("could not connect");
        if (found->second.body.size() > maxBytes) return std::unexpected("download is larger than allowed");
        if (progress) progress(found->second.body.size(), found->second.body.size());
        return found->second;
    }

    void serve(const std::string& url, std::string_view body, long status = 200) {
        responses[url] = {status, test::bytesOf(body)};
    }
};

std::string repoJson(const std::string& sha = "", std::size_t size = 0) {
    std::string entry = R"({"id":"a.game","name":"Game","version":"1.0","download":"game.zip",
        "games":[{"platform":"switch","title_id":"0100ABCD00000000"}])";
    if (!sha.empty()) entry += R"(,"sha256":")" + sha + "\"";
    if (size) entry += R"(,"size":)" + std::to_string(size);
    return R"({"name":"Repo","editors":[)" + entry + "}]}";
}

}

TEST_CASE("repo refresh downloads, caches and falls back offline") {
    test::TempDir dir;
    FakeHttpClient client;
    client.serve("https://a.example/repo.json", repoJson());
    st::RepoManager manager(client, dir.path());
    st::RepoCatalog catalog;

    auto statuses = manager.refresh({{"https://a.example/repo.json"}, {"https://down.example/repo.json"}}, catalog);
    REQUIRE(statuses.size() == 2);
    CHECK(statuses[0].error.empty());
    CHECK(statuses[0].editorCount == 1);
    CHECK_FALSE(statuses[1].error.empty());
    CHECK(catalog.repos().size() == 1);
    CHECK(catalog.repos()[0].index.editors[0].downloadUrl == "https://a.example/game.zip");

    client.responses.clear();
    st::RepoCatalog offline;
    statuses = manager.refresh({{"https://a.example/repo.json"}}, offline);
    CHECK(statuses[0].fromCache);
    CHECK(offline.repos().size() == 1);

    st::RepoCatalog cachedOnly;
    CHECK(manager.loadCached({{"https://a.example/repo.json"}}, cachedOnly)[0].editorCount == 1);
}

TEST_CASE("bad statuses and bad json are errors") {
    test::TempDir dir;
    FakeHttpClient client;
    client.serve("https://a.example/404.json", "missing", 404);
    client.serve("https://a.example/bad.json", "{");
    st::RepoManager manager(client, dir.path());
    st::RepoCatalog catalog;
    const auto statuses = manager.refresh({{"https://a.example/404.json"}, {"https://a.example/bad.json"}}, catalog);
    CHECK(statuses[0].error.find("404") != std::string::npos);
    CHECK(statuses[1].error.find("JSON") != std::string::npos);
    CHECK(catalog.repos().empty());
}

TEST_CASE("editor downloads are verified") {
    test::TempDir dir;
    FakeHttpClient client;
    const std::string zip = "zip-bytes";
    client.serve("https://a.example/game.zip", zip);
    const auto goodHash = st::toHex(st::sha256(test::viewOf(zip)));
    st::RepoManager manager(client, dir.path());

    auto index = st::parseRepoIndex(repoJson(goodHash, zip.size()), "https://a.example/repo.json");
    REQUIRE(index.has_value());
    CHECK(manager.downloadEditor(index->editors[0]).has_value());

    index = st::parseRepoIndex(repoJson(std::string(64, '0')), "https://a.example/repo.json");
    CHECK(manager.downloadEditor(index->editors[0]).error().find("checksum") != std::string::npos);

    index = st::parseRepoIndex(repoJson("", 3), "https://a.example/repo.json");
    CHECK(manager.downloadEditor(index->editors[0]).error().find("size") != std::string::npos);
}

TEST_CASE("release parsing compares versions and finds assets") {
    const std::string release = R"({"tag_name":"v0.3.0","body":"Fixes","html_url":"https://x/releases/v0.3.0",
        "assets":[{"name":"SaveTinker.nro","browser_download_url":"https://x/SaveTinker.nro"},
                  {"name":"SaveTinker.nro.sha256","browser_download_url":"https://x/SaveTinker.nro.sha256"}]})";
    auto newer = st::parseLatestRelease(release, "0.2.0", "SaveTinker.nro");
    REQUIRE(newer.has_value());
    REQUIRE(newer->has_value());
    CHECK((*newer)->version == "0.3.0");
    CHECK((*newer)->checksumUrl == "https://x/SaveTinker.nro.sha256");

    auto same = st::parseLatestRelease(release, "0.3.0", "SaveTinker.nro");
    REQUIRE(same.has_value());
    CHECK_FALSE(same->has_value());

    CHECK_FALSE(st::parseLatestRelease(release, "0.1.0", "Other.nro").has_value());
    CHECK_FALSE(st::parseLatestRelease("[]", "0.1.0", "SaveTinker.nro").has_value());
}

TEST_CASE("updater replaces the app only when the checksum matches") {
    test::TempDir dir;
    FakeHttpClient client;
    const std::string binary = "new nro";
    client.serve("https://x/SaveTinker.nro", binary);
    client.serve("https://x/SaveTinker.nro.sha256", st::toHex(st::sha256(test::viewOf(binary))) + "  SaveTinker.nro\n");
    client.serve("https://x/bad.sha256", std::string(64, 'a'));
    const auto target = dir.path() / "SaveTinker.nro";
    test::writeFile(target, "old nro");

    st::AppUpdater updater(client, "owner/repo", "0.1.0", "SaveTinker.nro");
    CHECK(updater.configured());
    st::ReleaseInfo release{"0.2.0", "", "", "https://x/SaveTinker.nro", "https://x/bad.sha256"};
    CHECK_FALSE(updater.install(release, target).has_value());
    CHECK(test::readFile(target) == "old nro");

    release.checksumUrl = "https://x/SaveTinker.nro.sha256";
    REQUIRE(updater.install(release, target).has_value());
    CHECK(test::readFile(target) == "new nro");

    st::AppUpdater unconfigured(client, "", "0.1.0", "SaveTinker.nro");
    CHECK_FALSE(unconfigured.check().has_value());
}

TEST_CASE("settings round trip with clamping") {
    test::TempDir dir;
    const auto path = dir.path() / "settings.json";
    CHECK(st::loadSettings(path).autoBackup);

    st::Settings settings;
    settings.backupsToKeep = 3;
    settings.repos = {{"https://a.example/repo.json", false}};
    REQUIRE(st::saveSettings(path, settings));
    REQUIRE(st::saveSettings(path, settings));
    const auto loaded = st::loadSettings(path);
    CHECK(loaded.backupsToKeep == 3);
    REQUIRE(loaded.repos.size() == 1);
    CHECK_FALSE(loaded.repos[0].enabled);

    test::writeFile(path, R"({"backups_to_keep": 5000, "ui_scale": 9})");
    CHECK(st::loadSettings(path).backupsToKeep == 100);
    CHECK(st::loadSettings(path).uiScale == 2.0f);
}

TEST_CASE("qr codes encode urls") {
    const auto code = st::makeQrCode("https://example.com/repo.json");
    REQUIRE(code.has_value());
    CHECK(code->size >= 21);
    CHECK(code->dark(0, 0));
}

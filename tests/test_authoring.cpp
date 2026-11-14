#include <doctest.h>

#include <algorithm>

#include "savetinker/authoring.hpp"
#include "savetinker/checksum.hpp"
#include "savetinker/editor_session.hpp"
#include "savetinker/script_host.hpp"
#include "test_support.hpp"

namespace {

const std::filesystem::path kExamples = SAVETINKER_EXAMPLES_DIR;

constexpr std::string_view kManifest = R"({
  "id": "test.editor", "name": "Test", "version": "1.2.0", "description": "d", "source": "https://example.com",
  "games": [{"platform": "switch", "title_id": "0100000000001000"}]
})";

bool hasIssue(const std::vector<st::ValidationIssue>& issues, std::string_view file, std::string_view text) {
    return std::ranges::any_of(issues, [&](const st::ValidationIssue& issue) {
        return issue.file == file && issue.message.find(text) != std::string::npos;
    });
}

}

TEST_CASE("editor folders are read without hidden files") {
    test::TempDir dir;
    test::writeFile(dir.path() / "manifest.json", kManifest);
    test::writeFile(dir.path() / "main.lua", "function build(ui, save) end");
    test::writeFile(dir.path() / "lib" / "util.lua", "return {}");
    test::writeFile(dir.path() / ".git" / "HEAD", "ref");
    test::writeFile(dir.path() / ".savetinker.json", "{}");

    auto package = st::readEditorDirectory(dir.path());
    REQUIRE_MESSAGE(package.has_value(), (package ? "" : package.error()));
    CHECK(package->manifest.id == "test.editor");
    std::vector<std::string> paths;
    for (const auto& file : package->files) paths.push_back(file.path);
    CHECK(paths == std::vector<std::string>{"lib/util.lua", "main.lua", "manifest.json"});
}

TEST_CASE("editor folder without a valid manifest is rejected") {
    test::TempDir dir;
    CHECK_FALSE(st::readEditorDirectory(dir.path()).has_value());
    test::writeFile(dir.path() / "manifest.json", "{}");
    auto package = st::readEditorDirectory(dir.path());
    REQUIRE_FALSE(package.has_value());
    CHECK(package.error().starts_with("manifest.json:"));
}

TEST_CASE("validation reports syntax errors, bad JSON and missing entry") {
    st::EditorPackage package;
    package.manifest = *st::parseManifest(R"({"id": "a", "name": "A", "version": "1.0.0", "entry": "start.lua",
        "games": [{"platform": "wiiu", "title_id": "0100000000001000"}]})");
    package.files = {{"helpers.lua", test::bytesOf("local x = = 1")}, {"data/items.json", test::bytesOf("{oops")}};

    const auto issues = st::validatePackage(package);
    CHECK(hasIssue(issues, "start.lua", "missing"));
    CHECK(hasIssue(issues, "helpers.lua", "helpers.lua:1:"));
    CHECK(hasIssue(issues, "data/items.json", "invalid JSON"));
    CHECK(hasIssue(issues, "manifest.json", "unknown platform"));
    CHECK(hasIssue(issues, "manifest.json", "source"));
}

TEST_CASE("the sample editor validates without errors") {
    auto package = st::readEditorDirectory(kExamples / "editors" / "sample-game");
    REQUIRE(package.has_value());
    for (const auto& issue : st::validatePackage(*package))
        CHECK_MESSAGE(issue.severity == st::ValidationIssue::Severity::Warning, (issue.file + ": " + issue.message));
}

TEST_CASE("packed editors round trip through the repo index") {
    auto package = st::readEditorDirectory(kExamples / "editors" / "sample-game");
    REQUIRE(package.has_value());
    const auto packed = st::packEditor(*package);
    CHECK(packed.fileName == "examples.sample-game-1.0.0.zip");
    CHECK(packed.entry.downloadUrl == "editors/examples.sample-game-1.0.0.zip");
    CHECK(packed.entry.size == packed.zip.size());
    CHECK(packed.entry.sha256 == st::toHex(st::sha256(packed.zip)));

    auto reopened = st::openPackage(packed.zip);
    REQUIRE(reopened.has_value());
    CHECK(reopened->files.size() == package->files.size());

    st::RepoIndex index;
    index.name = "Mine";
    index.maintainer = "me";
    index.editors.push_back(packed.entry);
    const auto json = st::repoIndexToJson(index);
    CHECK(json.find("\"download\": \"editors/examples.sample-game-1.0.0.zip\"") != std::string::npos);

    auto parsed = st::parseRepoIndex(json, "https://host/repo/repo.json");
    REQUIRE_MESSAGE(parsed.has_value(), (parsed ? "" : parsed.error()));
    REQUIRE(parsed->editors.size() == 1);
    const auto& editor = parsed->editors.front();
    CHECK(editor.downloadUrl == "https://host/repo/editors/examples.sample-game-1.0.0.zip");
    CHECK(editor.sha256 == packed.entry.sha256);
    CHECK(editor.games.size() == 2);
    CHECK(editor.games[0].versions == std::vector<std::string>{">=1.0.0"});
}

TEST_CASE("manifests survive a JSON round trip") {
    auto manifest = *st::parseManifest(kManifest);
    manifest.authors = {"a", "b"};
    manifest.entry = "src/main.lua";
    manifest.games.front().versions = {"1.0.0"};
    auto parsed = st::parseManifest(st::manifestToJson(manifest));
    REQUIRE(parsed.has_value());
    CHECK(parsed->authors == manifest.authors);
    CHECK(parsed->entry == "src/main.lua");
    CHECK(parsed->source == "https://example.com");
    CHECK(parsed->games.front().versions == std::vector<std::string>{"1.0.0"});
}

TEST_CASE("UI dumps include nested pages, sections and popups") {
    st::Document document;
    document.addFile("save.dat", st::Bytes{5, 0, 0, 0, 0b101});
    st::ScriptContext context;
    context.editor = *st::parseManifest(kManifest);
    st::ScriptHost host(document, context, [](std::string_view) -> std::optional<std::string> {
        return R"(
function build(ui, save)
    local f = save:file("save.dat")
    ui:note("Top note")
    ui:page("Player", function(page)
        page:int{ label = "Gold", bind = f:field(0, "u32"), actions = { { label = "Max", run = function() end } } }
        page:section("Flags", function(s)
            s:toggle{ label = "Hard", bind = f:flag(4, 0) }
            s:multiselect{ label = "Elements", bind = f:field(4, "u8"), bits = { [0] = "Fire", [1] = "Ice", [2] = "Wind" } }
        end)
        page:page("Details", function(d)
            d:label{ label = "Raw", value = 255, format = "hex" }
            d:popup({ label = "More" }, function(p) p:dropdown{ label = "Mode", bind = f:field(0, "u32"), options = { [5] = "Five" } } end)
        end)
    end)
    ui:page("Other", function(page) page:button{ label = "Reset", run = function() end } end)
end
)";
    });
    REQUIRE(host.loadEntry().has_value());
    st::EditorSession session(host);

    auto dump = st::dumpUi(session);
    REQUIRE_MESSAGE(dump.has_value(), (dump ? "" : dump.error()));
    CHECK(*dump == "note: Top note\n"
                   "[Player]\n"
                   "  Gold = 5 [Max]\n"
                   "  Flags:\n"
                   "    Hard = on\n"
                   "    Elements = Fire, Wind\n"
                   "  > Details\n"
                   "    Raw: 0xFF\n"
                   "    More (popup):\n"
                   "      Mode = Five\n"
                   "[Other]\n"
                   "  [Reset]\n");
}

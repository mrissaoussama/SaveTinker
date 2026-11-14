#include <doctest.h>

#include "savetinker/authoring.hpp"
#include "savetinker/editor_session.hpp"
#include "savetinker/editor_template.hpp"
#include "savetinker/script_host.hpp"
#include "test_support.hpp"

namespace {

st::EditorTemplate sampleTemplate() {
    st::EditorTemplate draft;
    draft.manifest = *st::parseManifest(R"({"id": "me.game", "name": "Game", "version": "0.1.0",
        "games": [{"platform": "switch", "title_id": "0100000000001000"}]})");
    draft.detectFile = "save.dat";
    draft.magic = test::bytesOf("SMPL");
    draft.fields = {
        {"save.dat", 0x08, "u32", "Gold", "Player", false, 0, 0, false},
        {"save.dat", 0x0C, "bit", "Hard \"mode\"", "Player", false, 2, 0, false},
        {"save.dat", 0x10, "utf16", "Name", "", false, 0, 4, false},
        {"data/1.bin", 0x00, "u16", "Id", "Other", true, 0, 0, true},
        {"save.dat", 0x20, "f32", "Speed", "Player", false, 0, 0, false},
    };
    return draft;
}

st::Bytes sampleSave() {
    st::ByteBuffer buffer(st::Bytes(0x24));
    buffer.writeBytes(0, test::viewOf("SMPL"));
    buffer.write<std::uint32_t>(0x08, 1234);
    buffer.write<std::uint8_t>(0x0C, 0b100);
    buffer.writeUtf16(0x10, "Ann", 4);
    buffer.writeFloat(0x20, 1.5f);
    return buffer.bytes();
}

}

TEST_CASE("template fields report their size and problems") {
    CHECK(st::templateFieldSize({"f", 0, "u64"}) == 8);
    CHECK(st::templateFieldSize({"f", 0, "utf16", "x", "", false, 0, 5}) == 10);
    CHECK(st::validateTemplateField({"f", 0, "u24", "x"}).find("unknown type") != std::string::npos);
    CHECK(st::validateTemplateField({"f", 0, "bit", "x", "", false, 9}) == "bit must be 0-7");
    CHECK(st::validateTemplateField({"f", 0, "utf8", "x"}) == "text fields need a length");
    CHECK(st::validateTemplateField({"f", 0, "u8", "x"}).empty());
}

TEST_CASE("generated editors compile, detect and edit the save") {
    const auto draft = sampleTemplate();
    const auto files = st::generateEditorFiles(draft);

    st::EditorPackage package;
    package.files = files;
    package.manifest = *st::parseManifest(package.find("manifest.json")->text());
    CHECK(package.manifest.id == "me.game");
    for (const auto& issue : st::validatePackage(package))
        CHECK_MESSAGE(issue.severity == st::ValidationIssue::Severity::Warning, issue.message);

    const std::string script(package.find("main.lua")->text());
    CHECK(script.find("local data_1_bin = save:file(\"data/1.bin\")") != std::string::npos);

    st::Document document;
    document.addFile("save.dat", sampleSave());
    document.addFile("data/1.bin", st::Bytes{0x12, 0x34});
    st::ScriptHost host(document, {"switch", "0100000000001000", "", "", package.manifest},
                        [&](std::string_view name) -> std::optional<std::string> {
                            if (name == "main") return script;
                            return std::nullopt;
                        });
    REQUIRE(host.loadEntry().has_value());
    CHECK(host.detect().value());

    st::EditorSession session(host);
    auto dump = st::dumpUi(session);
    REQUIRE_MESSAGE(dump.has_value(), (dump ? "" : dump.error()));
    CHECK(*dump == "[Player]\n"
                   "  Gold = 1234\n"
                   "  Hard \"mode\" = on\n"
                   "  Speed = 1.5\n"
                   "[General]\n"
                   "  Name = \"Ann\"\n"
                   "[Other]\n"
                   "  Id = 0x1234\n");

    REQUIRE(session.openTopLevel(session.topLevelLinks().front().pageId).has_value());
    const auto gold = session.currentPage().nodes.front();
    REQUIRE(session.setValue(gold, std::int64_t{99}).has_value());
    CHECK(document.file("save.dat").read<std::uint32_t>(0x08) == 99);
}

TEST_CASE("generated detect rejects other saves") {
    st::Document document;
    document.addFile("save.dat", st::Bytes(8));
    const auto script = st::generateEditorScript(sampleTemplate());
    st::ScriptHost host(document, {}, [&](std::string_view) -> std::optional<std::string> { return script; });
    REQUIRE(host.loadEntry().has_value());
    CHECK_FALSE(host.detect().value());
}

TEST_CASE("templates survive a JSON round trip") {
    const auto draft = sampleTemplate();
    auto parsed = st::templateFromJson(st::templateToJson(draft));
    REQUIRE_MESSAGE(parsed.has_value(), (parsed ? "" : parsed.error()));
    CHECK(parsed->manifest.id == "me.game");
    CHECK(parsed->magic == draft.magic);
    CHECK(parsed->detectFile == "save.dat");
    REQUIRE(parsed->fields.size() == draft.fields.size());
    CHECK(parsed->fields[1].bit == 2);
    CHECK(parsed->fields[2].length == 4);
    CHECK(parsed->fields[3].bigEndian);
    CHECK(parsed->fields[3].hex);
    CHECK(st::generateEditorScript(*parsed) == st::generateEditorScript(draft));
}

TEST_CASE("invalid template fields are rejected") {
    auto parsed = st::templateFromJson(R"({"manifest": {"id": "a", "name": "A", "version": "1.0.0",
        "games": [{"platform": "switch", "title_id": "0100000000001000"}]},
        "fields": [{"file": "f", "offset": 0, "type": "bit", "label": "Flag", "bit": 12}]})");
    REQUIRE_FALSE(parsed.has_value());
    CHECK(parsed.error() == "Flag: bit must be 0-7");
}

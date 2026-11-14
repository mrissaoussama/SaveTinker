#include <doctest.h>

#include <algorithm>
#include <fstream>
#include <sstream>

#include "savetinker/checksum.hpp"
#include "savetinker/editor_session.hpp"
#include "savetinker/manifest.hpp"
#include "savetinker/script_host.hpp"

#ifndef SAVETINKER_EXAMPLES_DIR
#error "SAVETINKER_EXAMPLES_DIR must be defined"
#endif

namespace {

const std::filesystem::path kEditorDir = std::filesystem::path(SAVETINKER_EXAMPLES_DIR) / "editors" / "sample-game";

st::EditorManifest loadManifest() {
    std::ifstream stream(kEditorDir / "manifest.json");
    std::stringstream text;
    text << stream.rdbuf();
    auto manifest = st::parseManifest(text.str());
    REQUIRE_MESSAGE(manifest.has_value(), (manifest ? "" : manifest.error()));
    return *manifest;
}

st::Bytes sampleSave() {
    st::ByteBuffer buffer(st::Bytes(0x30));
    buffer.writeBytes(0, st::ByteView(reinterpret_cast<const std::uint8_t*>("SMPL"), 4));
    buffer.write<std::uint32_t>(0x08, 1500);
    buffer.write<std::uint8_t>(0x0C, 12);
    buffer.writeUtf16(0x10, "Link", 16);
    return buffer.bytes();
}

}

TEST_CASE("sample editor manifest targets two platforms") {
    const auto manifest = loadManifest();
    CHECK(manifest.platforms() == std::vector<std::string>{"switch", "3ds"});
    CHECK(manifest.findGame("switch", "0100000000001000")->supportsVersion("1.0.0"));
}

TEST_CASE("sample editor detects its save and fixes the checksum") {
    st::Document document;
    document.addFile("save.dat", sampleSave());
    st::ScriptContext context{"switch", "0100000000001000", "Sample Game", "1.0.0", loadManifest()};
    st::ScriptHost host(document, context, st::directoryModuleLoader(kEditorDir));

    REQUIRE(host.loadEntry().has_value());
    CHECK(host.detect().value());

    REQUIRE(host.run("st.save:file('save.dat'):set(0x08, 'u32', 9999)").has_value());
    REQUIRE(host.beforeSave().has_value());

    const auto& file = document.file("save.dat");
    CHECK(file.read<std::uint32_t>(0x08) == 9999);
    CHECK(file.read<std::uint32_t>(0x04) == st::crc32(file.readBytes(0x08, file.size() - 0x08)));
}

TEST_CASE("sample editor rejects other saves") {
    st::Document document;
    document.addFile("save.dat", st::Bytes(16));
    st::ScriptContext context{"switch", "0100000000001000", "Sample Game", "1.0.0", loadManifest()};
    st::ScriptHost host(document, context, st::directoryModuleLoader(kEditorDir));
    REQUIRE(host.loadEntry().has_value());
    CHECK_FALSE(host.detect().value());
}

TEST_CASE("sample editor builds its pages against the demo save") {
    const auto savePath = std::filesystem::path(SAVETINKER_EXAMPLES_DIR) / "console" / "saves" / "0100000000001000" /
                          "account-00000000000000010000000000000001" / "save.dat";
    std::ifstream stream(savePath, std::ios::binary);
    REQUIRE(stream);
    st::Document document;
    document.addFile("save.dat", st::Bytes(std::istreambuf_iterator<char>(stream), {}));

    st::ScriptContext context{"switch", "0100000000001000", "Sample Game", "1.0.0", loadManifest(), "fr"};
    st::ScriptHost host(document, context, st::directoryModuleLoader(kEditorDir));
    host.setDataLoader(st::directoryDataLoader(kEditorDir));
    REQUIRE(host.loadEntry().has_value());
    REQUIRE(host.detect().value());

    st::EditorSession session(host);
    auto built = session.rebuild();
    REQUIRE_MESSAGE(built.has_value(), (built ? "" : built.error()));
    REQUIRE(session.topLevelLinks().size() == 3);
    CHECK(session.currentPage().title == "Player");

    const auto& nodes = session.currentPage().nodes;
    const auto name = std::ranges::find(nodes, std::string("Name"), &st::UiNode::label);
    REQUIRE(name != nodes.end());
    CHECK(name->value == st::UiValue(std::string("Link")));

    const auto classNode = *std::ranges::find(nodes, std::string("Class"), &st::UiNode::label);
    REQUIRE(session.setValue(classNode, std::int64_t{1}).has_value());
    CHECK(document.file("save.dat").read<std::uint8_t>(0x48) == 80);

    REQUIRE(session.openTopLevel(session.topLevelLinks()[1].pageId).has_value());
    const auto& bag = session.currentPage().nodes.at(1);
    REQUIRE(bag.kind == st::WidgetKind::Section);
    CHECK(bag.children.at(0).options.at(1).label == "Potion");
    CHECK(bag.children.at(2).options.at(3).label == "Épée de bronze");

    REQUIRE(session.openTopLevel(session.topLevelLinks()[2].pageId).has_value());
    const auto before = session.currentPage().nodes.size();
    const auto expansion = *std::ranges::find(session.currentPage().nodes, std::string("Expansion unlocked"), &st::UiNode::label);
    REQUIRE(session.setValue(expansion, true).has_value());
    CHECK(session.currentPage().nodes.size() == before + 3);
}

#include <doctest.h>

#include <fstream>
#include <sstream>

#include "savetinker/checksum.hpp"
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

#include <doctest.h>

#include <map>

#include "savetinker/checksum.hpp"
#include "savetinker/script_host.hpp"
#include "test_support.hpp"

using st::Bytes;
using st::Document;
using st::ScriptHost;

namespace {

st::ScriptContext makeContext() {
    st::ScriptContext context;
    context.platform = "switch";
    context.titleId = "0100ABCD00000000";
    context.gameName = "My Game";
    context.gameVersion = "1.0.2";
    context.editor.id = "example.my-game";
    context.editor.name = "My Game Editor";
    context.editor.version = "1.0.0";
    return context;
}

st::ModuleLoader mapLoader(std::map<std::string, std::string> modules) {
    return [modules = std::move(modules)](std::string_view name) -> std::optional<std::string> {
        auto found = modules.find(std::string(name));
        if (found == modules.end()) return std::nullopt;
        return found->second;
    };
}

struct Fixture {
    Document document;
    std::unique_ptr<ScriptHost> host;

    explicit Fixture(std::map<std::string, std::string> modules = {}, st::ScriptLimits limits = {}) {
        document.addFile("save.dat", Bytes{0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 'A', 'B', 0, 0});
        host = std::make_unique<ScriptHost>(document, makeContext(), mapLoader(std::move(modules)), limits);
    }

    std::string eval(std::string_view code) {
        auto result = host->evaluate(code);
        REQUIRE_MESSAGE(result.has_value(), (result ? "" : result.error()));
        return *result;
    }

    std::string errorOf(std::string_view code) {
        auto result = host->run(code);
        REQUIRE_FALSE(result.has_value());
        return result.error();
    }
};

}

TEST_CASE("context tables describe platform, game and editor") {
    Fixture f;
    CHECK(f.eval("return st.platform.id") == "switch");
    CHECK(f.eval("return st.platform.name") == "Nintendo Switch");
    CHECK(f.eval("return st.game.title_id .. ' ' .. st.game.version") == "0100ABCD00000000 1.0.2");
    CHECK(f.eval("return st.editor.id") == "example.my-game");
    CHECK(f.eval("return st.api_version") == "1");
}

TEST_CASE("sandbox hides dangerous functions") {
    Fixture f;
    CHECK(f.eval("return tostring(io)") == "nil");
    CHECK(f.eval("return tostring(os)") == "nil");
    CHECK(f.eval("return tostring(debug)") == "nil");
    CHECK(f.eval("return tostring(load)") == "nil");
    CHECK(f.eval("return tostring(dofile)") == "nil");
    CHECK(f.eval("return tostring(string.dump)") == "nil");
    CHECK(f.eval("return tostring(package)") == "nil");
}

TEST_CASE("file reads typed values") {
    Fixture f;
    CHECK(f.eval("return st.save:file('save.dat'):u32(0)") == "16");
    CHECK(f.eval("return st.save:file('save.dat'):u32(0, 'big')") == "268435456");
    CHECK(f.eval("return st.save:file('save.dat'):get(0, 'u8')") == "16");
    CHECK(f.eval("return st.save:file('save.dat'):str(8, 4)") == "AB");
    CHECK(f.eval("return st.save:file('save.dat'):size()") == "12");
    CHECK(f.eval("return st.save:file('save.dat'):find('B')") == "9");
    CHECK(f.eval("return #st.save:files()") == "1");
    CHECK(f.eval("return st.save:has('nope')") == "false");
}

TEST_CASE("file writes go through the document and can be undone") {
    Fixture f;
    f.eval("local f = st.save:file('save.dat'); f:set(4, 'u16', 500); f:setBit(0, 0, true)");
    CHECK(f.document.file("save.dat").read<std::uint16_t>(4) == 500);
    CHECK(f.document.file("save.dat").read<std::uint8_t>(0) == 0x11);
    CHECK(f.document.history().size() == 2);
    f.document.undo();
    f.document.undo();
    CHECK(f.document.file("save.dat").read<std::uint16_t>(4) == 0);
}

TEST_CASE("default endian applies to reads and writes") {
    Fixture f;
    f.eval("local f = st.save:file('save.dat'):endian('big'); f:set(0, 'u32', 1)");
    CHECK(f.document.file("save.dat").read<std::uint32_t>(0, st::Endian::Big) == 1);
    CHECK(f.eval("local f = st.save:file('save.dat'); f:endian('big'); return f:u32(0)") == "1");
}

TEST_CASE("floats and strings") {
    Fixture f;
    f.eval("local f = st.save:file('save.dat'); f:set(0, 'f32', 2.5); f:setStr(8, 4, 'Hi')");
    CHECK(f.eval("return st.save:file('save.dat'):f32(0)") == "2.5");
    CHECK(f.eval("return st.save:file('save.dat'):str(8, 4)") == "Hi");
    f.eval("st.save:file('save.dat'):setStr(8, 2, 'Z', 'utf16')");
    CHECK(f.eval("return st.save:file('save.dat'):str(8, 2, 'utf16')") == "Z");
}

TEST_CASE("bad writes raise script errors") {
    Fixture f;
    CHECK(f.errorOf("st.save:file('save.dat'):set(0, 'u8', 300)").find("out of range for u8") != std::string::npos);
    CHECK(f.errorOf("st.save:file('save.dat'):set(100, 'u32', 1)").find("out of range") != std::string::npos);
    CHECK(f.errorOf("st.save:file('save.dat'):set(0, 'u7', 1)").find("unknown field type") != std::string::npos);
    CHECK(f.errorOf("st.save:file('missing')").find("no file named") != std::string::npos);
    CHECK(f.errorOf("st.save:file('save.dat'):setStr(8, 2, 'toolong')").find("longer") != std::string::npos);
}

TEST_CASE("transactions group writes and roll back on error") {
    Fixture f;
    f.eval("st.transaction('Max all', function() local f = st.save:file('save.dat'); for i = 0, 3 do f:set(i, 'u8', 255) end end)");
    CHECK(f.document.history().size() == 1);
    CHECK(f.document.undoLabel() == "Max all");

    const auto error = f.errorOf("st.transaction('Broken', function() st.save:file('save.dat'):set(4, 'u8', 9); error('boom') end)");
    CHECK(error.find("boom") != std::string::npos);
    CHECK(f.document.file("save.dat").read<std::uint8_t>(4) == 0);
    CHECK(f.document.history().size() == 1);
}

TEST_CASE("hash and hex helpers") {
    Fixture f;
    CHECK(f.eval("return st.hash.crc32('123456789')") == std::to_string(0xCBF43926u));
    CHECK(f.eval("return st.hex(st.hash.sha256('abc'))") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(f.eval("return st.hex(st.unhex('00FF'))") == "00ff");
    CHECK(f.eval("return string.unpack('<I2', st.unhex('3412'))") == "4660");
}

TEST_CASE("json round trip") {
    Fixture f;
    CHECK(f.eval(R"(local v = st.json.decode('{"gold":10,"items":[1,2,3],"name":"x"}') return v.gold + #v.items)") == "13");
    CHECK(f.eval(R"(return st.json.encode({1, 2, {a = true}}))") == R"([1,2,{"a":true}])");
    CHECK(f.eval(R"(return st.json.decode('null') == st.json.null)") == "true");
    CHECK(f.eval(R"(return st.json.encode({a = st.json.null}))") == R"({"a":null})");
    CHECK(f.errorOf("st.json.decode('{bad')").find("invalid JSON") != std::string::npos);
    CHECK(f.errorOf("st.json.encode({[true] = 1})").find("string keys") != std::string::npos);
}

TEST_CASE("require loads modules once") {
    Fixture f({{"lib.counter", "count = (count or 0) + 1; return { value = 42 }"}});
    CHECK(f.eval("return require('lib.counter').value + require('lib.counter').value") == "84");
    CHECK(f.eval("return count") == "1");
    CHECK(f.errorOf("require('missing')").find("not found") != std::string::npos);
    CHECK(f.errorOf("require('../escape')").find("invalid module name") != std::string::npos);
}

TEST_CASE("entry script defines detect and before_save") {
    Fixture f({{"main", R"(
        function detect(save) return save:file('save.dat'):u8(0) == 0x10 end
        function before_save(save)
            local f = save:file('save.dat')
            f:set(4, 'u32', st.hash.crc32(f:bytes(8, 4)))
        end
    )"}});
    REQUIRE(f.host->loadEntry().has_value());
    CHECK(f.host->detect().value());
    REQUIRE(f.host->beforeSave().has_value());
    CHECK(f.document.file("save.dat").read<std::uint32_t>(4) == st::crc32(f.document.file("save.dat").readBytes(8, 4)));
    CHECK(f.document.undoLabel() == "Update checksums");
}

TEST_CASE("detect defaults to true and reports errors") {
    Fixture plain;
    CHECK(plain.host->detect().value());

    Fixture broken({{"main", "function detect(save) error('cannot read') end"}});
    REQUIRE(broken.host->loadEntry().has_value());
    const auto result = broken.host->detect();
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().find("cannot read") != std::string::npos);
}

TEST_CASE("missing entry and syntax errors are reported") {
    Fixture none;
    CHECK(none.host->loadEntry().error().find("not found") != std::string::npos);
    Fixture bad({{"main", "function ("}});
    CHECK_FALSE(bad.host->loadEntry().has_value());
}

TEST_CASE("print and warn go to the log") {
    Fixture f;
    f.eval("print('gold', 10); st.warn('careful')");
    REQUIRE(f.host->log().size() == 2);
    CHECK(f.host->log()[0].message == "gold\t10");
    CHECK(f.host->log()[1].level == st::LogEntry::Level::Warning);
}

TEST_CASE("runaway scripts hit the instruction limit") {
    Fixture f({}, st::ScriptLimits{64u << 20, 100'000});
    CHECK(f.errorOf("while true do end").find("instruction limit") != std::string::npos);
    CHECK(f.eval("return 1 + 1") == "2");
}

TEST_CASE("memory hogs hit the memory limit") {
    Fixture f({}, st::ScriptLimits{2u << 20, 500'000'000});
    CHECK(f.errorOf("local t = {} for i = 1, 1e7 do t[i] = ('x'):rep(100) .. i end").find("memory") != std::string::npos);
    CHECK(f.eval("return 'still alive'") == "still alive");
}

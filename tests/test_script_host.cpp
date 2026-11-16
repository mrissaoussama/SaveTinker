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

TEST_CASE("scripts can compress and encrypt") {
    Fixture f;
    CHECK(f.eval("return st.zlib.decompress(st.zlib.compress('abcabcabc'))") == "abcabcabc");
    CHECK(f.eval("return st.gzip.decompress(st.gzip.compress('hi', 9))") == "hi");
    CHECK(f.eval("return st.deflate.decompress(st.deflate.compress(''))") == "");
    CHECK(f.eval("return st.zstd.decompress(st.zstd.compress(string.rep('z', 100)))") == std::string(100, 'z'));
    CHECK(f.eval("return st.hex(st.crypto.aes_encrypt('ecb', st.unhex('000102030405060708090a0b0c0d0e0f'), "
                 "st.unhex('00112233445566778899aabbccddeeff')))") == "69c4e0d86a7b0430d8cdb78070b4c55a");
    CHECK(f.eval("local k, iv = string.rep('k', 16), string.rep('i', 16) "
                 "return st.crypto.aes_decrypt('cbc', k, st.crypto.aes_encrypt('cbc', k, string.rep('p', 32), iv), iv)") ==
          std::string(32, 'p'));
    CHECK(f.eval("return #st.crypto.aes_cmac(string.rep('k', 16), 'data')") == "16");
    CHECK(f.eval("return st.crypto.xor(st.crypto.xor('secret', 'key'), 'key')") == "secret");
    CHECK(f.eval("return #st.hash.hmac_sha256('key', 'data')") == "32");
    CHECK(f.errorOf("st.crypto.aes_encrypt('ecb', 'short', 'x')").find("16, 24 or 32") != std::string::npos);
}

TEST_CASE("flags and bits reach past the first byte") {
    Fixture f;
    f.eval("local file = st.save:file('save.dat') file:setBit(0, 13, true) file:flag(0, 8):set(true) return ''");
    CHECK(f.document.file("save.dat").read<std::uint8_t>(1) == 0x21);
    CHECK(f.eval("return tostring(st.save:file('save.dat'):bit(0, 13))") == "true");
    CHECK(f.errorOf("st.save:file('save.dat'):bit(0, -1)").find("negative") != std::string::npos);
}

TEST_CASE("structs lay out records and bind their fields") {
    Fixture f;
    CHECK(f.eval(R"(
        local Pair = st.struct{ {"a", "u8"}, {"b", "u8"} }
        local Hero = st.struct{ {"level", "u8"}, {"_", "pad", 1}, {"hp", "u16"}, {"pair", Pair}, {"stats", "u8", count = 2},
                                {"name", "utf8", 2, at = 8} }
        local file = st.save:file("save.dat")
        local heroes = Hero:array(file, 0, 1)
        heroes[1].level:set(7)
        heroes[1].hp:set(0x0102)
        heroes[1].pair.b:set(9)
        heroes[1].stats[2]:set(5)
        return table.concat({ Hero.size, Pair.size, file:u8(0), file:u16(2), file:u8(5), file:u8(7), heroes[1].name:get(),
                              table.concat(Hero:names(), ",") }, " ")
    )") == "10 2 7 258 9 5 AB level,hp,pair,stats,name");
    CHECK(f.eval("return st.struct({ {'x', 'u16'} }, { endian = 'big' }):at(st.save:file('save.dat'), 8).x:get()") == "16706");
    CHECK(f.errorOf("st.struct{ {'x', 'u24'} }").find("unknown field type") != std::string::npos);
}

TEST_CASE("st.gvas reads Unreal saves and keeps sizes right when strings change") {
    Fixture f;
    f.eval(R"(
        local function fs(s) return string.pack("<i4", #s + 1) .. s .. "\0" end
        local function prop(name, kind, header, body) return fs(name) .. fs(kind) .. string.pack("<i4i4", #body, 0) .. header .. body end
        local stats = prop("Level", "IntProperty", "\0", string.pack("<i4", 7)) .. prop("Title", "StrProperty", "\0", fs("Hero")) .. fs("None")
        local data = "GVAS" .. string.pack("<i4i4I2I2I2I4", 2, 522, 4, 27, 2, 0) .. fs("++UE4+Release-4.27") .. string.pack("<i4i4", 3, 0)
            .. fs("/Script/Game.MySave")
            .. prop("Gold", "IntProperty", "\0", string.pack("<i4", 250))
            .. prop("Name", "StrProperty", "\0", fs("Rin"))
            .. fs("Alive") .. fs("BoolProperty") .. string.pack("<i4i4", 0, 0) .. "\1\0"
            .. prop("Pos", "StructProperty", fs("Vector") .. string.rep("\0", 17), string.pack("<fff", 1, 2, 3))
            .. prop("Stats", "StructProperty", fs("PlayerStats") .. string.rep("\0", 17), stats)
            .. prop("Items", "ArrayProperty", fs("IntProperty") .. "\0", string.pack("<i4i4i4", 2, 10, 20))
            .. prop("Tags", "ArrayProperty", fs("NameProperty") .. "\0", string.pack("<i4", 2) .. fs("a") .. fs("bb"))
            .. prop("Odd", "TextProperty", "\0", "\1\2\3")
            .. fs("None") .. string.pack("<i4", 0)
        st.save:file("save.dat"):setContent(data)
        return ""
    )");
    CHECK(f.eval(R"(
        local gvas = require("st.gvas")
        local file = st.save:file("save.dat")
        local save = assert(gvas.parse(file))
        local p = {}
        for _, node in ipairs(save.properties) do p[node.name] = node end
        local stats = {}
        for _, node in ipairs(p.Stats.children) do stats[node.name] = node end
        return table.concat({ save.header.engine, save.header.class, #save.properties, gvas.bind(file, p.Gold):get(), p.Name.value,
            tostring(gvas.bind(file, p.Alive):get()), p.Pos.children[3].kind, gvas.bind(file, p.Pos.children[3]):get(),
            stats.Title.value, gvas.bind(file, p.Items.children[2]):get(), p.Tags.children[2].value, p.Odd.kind }, " ")
    )") == "4.27.2-0 /Script/Game.MySave 8 250 Rin true f32 3.0 Hero 20 bb raw");
    CHECK(f.eval(R"(
        local gvas = require("st.gvas")
        local file = st.save:file("save.dat")
        local function find(list, name) for _, n in ipairs(list) do if n.name == name then return n end end end
        local save = gvas.parse(file)
        gvas.bind(file, find(save.properties, "Name")):set("Rinoa")
        save = gvas.parse(file)
        gvas.bind(file, find(find(save.properties, "Stats").children, "Title")):set("Champion \u{e9}")
        save = assert(gvas.parse(file))
        gvas.bind(file, find(save.properties, "Tags").children[1]):set("abc")
        save = assert(gvas.parse(file))
        return find(save.properties, "Name").value .. "|" .. find(find(save.properties, "Stats").children, "Title").value .. "|"
            .. find(save.properties, "Tags").children[1].value .. "|" .. find(save.properties, "Odd").kind
    )") == "Rinoa|Champion \xC3\xA9|abc|raw");
    CHECK(f.eval("local g = require('st.gvas') st.save:file('save.dat'):setContent('nope') return select(2, g.parse(st.save:file('save.dat')))")
              .find("not an Unreal save") != std::string::npos);
}

TEST_CASE("st.gvas reads UE 5.4 saves with full type names") {
    Fixture f;
    f.eval(R"(
        local function fs(s) return string.pack("<i4", #s + 1) .. s .. "\0" end
        local function ty(name, ...) local params = { ... } return fs(name) .. string.pack("<i4", #params) .. table.concat(params) end
        local function prop(name, typeName, flags, body) return fs(name) .. typeName .. string.pack("<i4B", #body, flags) .. body end
        local data = "GVAS" .. string.pack("<i4i4i4I2I2I2I4", 3, 522, 1012, 5, 4, 4, 0) .. fs("++UE5+Release-5.4") .. string.pack("<i4i4", 3, 0)
            .. fs("/Script/Game.MySave") .. "\0"
            .. prop("Gold", ty("IntProperty"), 0, string.pack("<i4", 250))
            .. prop("Alive", ty("BoolProperty"), 0x10, "")
            .. prop("Pos", ty("StructProperty", ty("Vector", ty("/Script/CoreUObject"))), 0x08, string.pack("<ddd", 1, 2, 3))
            .. prop("Items", ty("ArrayProperty", ty("IntProperty")), 0, string.pack("<i4i4i4", 2, 10, 20))
            .. prop("Quests", ty("MapProperty", ty("NameProperty"), ty("ByteProperty", ty("EState", ty("/Script/Game")))), 0,
                string.pack("<i4i4", 0, 1) .. fs("Intro") .. fs("EState::Done"))
            .. prop("Name", ty("StrProperty"), 0, fs("Rin"))
            .. fs("None") .. string.pack("<i4", 0)
        st.save:file("save.dat"):setContent(data)
        return ""
    )");
    CHECK(f.eval(R"(
        local gvas = require("st.gvas")
        local file = st.save:file("save.dat")
        local function find(list, name) for _, n in ipairs(list) do if n.name == name then return n end end end
        local save = assert(gvas.parse(file))
        local alive = gvas.bind(file, find(save.properties, "Alive"))
        local before = tostring(alive:get())
        alive:set(false)
        gvas.bind(file, find(save.properties, "Name")):set("Rinoa")
        save = assert(gvas.parse(file))
        local p = {}
        for _, node in ipairs(save.properties) do p[node.name] = node end
        return table.concat({ save.header.engine, #save.properties, gvas.bind(file, p.Gold):get(), before, tostring(gvas.bind(file, p.Alive):get()),
            p.Pos.children[2].kind, gvas.bind(file, p.Pos.children[2]):get(), gvas.bind(file, p.Items.children[2]):get(),
            p.Quests.children[1].name, p.Quests.children[1].value.value, p.Name.value }, " ")
    )") == "5.4.4-0 6 250 true false f64 2.0 20 Intro EState::Done Rinoa");
}

TEST_CASE("st.es3 round trips encrypted and compressed saves") {
    Fixture f;
    CHECK(f.eval(R"(
        local es3 = require("st.es3")
        local text = '{"gold":{"__type":"int","value":5}}'
        local sealed = es3.encode(text, "password", { gzip = true })
        local wrong = select(2, es3.decode(sealed, "nope"))
        return tostring(es3.encrypted(sealed)) .. " " .. es3.decode(sealed, "password") .. " " .. es3.decode(text) .. " " .. tostring(wrong)
    )") == R"(true {"gold":{"__type":"int","value":5}} {"gold":{"__type":"int","value":5}} wrong password)");
    // Gzip without encryption is not mistaken for encrypted data when its size is a multiple of 16.
    CHECK(f.eval(R"(
        local es3 = require("st.es3")
        local text, packed = '{"a":1}', nil
        repeat
            text = text .. " "
            packed = es3.encode(text, nil, { gzip = true })
        until #packed >= 32 and #packed % 16 == 0
        local info = assert(es3.open(packed))
        return tostring(info.encrypted) .. " " .. tostring(info.gzip) .. " " .. tostring(info.text == text)
    )") == "false true true");
    CHECK(f.eval("return st.hex(st.crypto.pbkdf2('sha1', 'password', 'salt', 2, 20))") == "ea6c014dc72d6f8ccd1ed92ace1d41f0d8de8957");
    CHECK(f.eval("return #st.hash.hmac_sha1('k', 'd')") == "20");
}

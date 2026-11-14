#include <doctest.h>

#include "savetinker/checksum.hpp"
#include "savetinker/document.hpp"
#include "savetinker/formats.hpp"
#include "savetinker/manifest.hpp"
#include "savetinker/script_host.hpp"
#include "test_support.hpp"

namespace {

std::string repeat(std::string_view text, int count) {
    std::string out;
    for (int i = 0; i < count; ++i) out += text;
    return out;
}

class Writer {
public:
    explicit Writer(bool big = false) : big_(big) {}
    Writer& u8(std::uint8_t v) { data.push_back(v); return *this; }
    Writer& uint(std::uint64_t v, int width) {
        for (int i = 0; i < width; ++i) data.push_back(static_cast<std::uint8_t>(v >> (8 * (big_ ? width - 1 - i : i))));
        return *this;
    }
    Writer& u16(std::uint16_t v) { return uint(v, 2); }
    Writer& u24(std::uint32_t v) { return uint(v, 3); }
    Writer& u32(std::uint32_t v) { return uint(v, 4); }
    Writer& text(std::string_view s) { data.insert(data.end(), s.begin(), s.end()); return *this; }
    Writer& pad(std::size_t alignment) {
        while (data.size() % alignment) data.push_back(0);
        return *this;
    }
    void put32(std::size_t at, std::uint32_t v) {
        for (int i = 0; i < 4; ++i) data[at + i] = static_cast<std::uint8_t>(v >> (8 * (big_ ? 3 - i : i)));
    }
    st::Bytes data;

private:
    bool big_;
};

st::Bytes makeSarc() {
    Writer w;
    w.text("SARC").u16(0x14).u16(0xFEFF).u32(0).u32(0).u16(0x100).u16(0);
    w.text("SFAT").u16(0xC).u16(2).u32(0x65);
    w.u32(1).u32(0x01000000).u32(0).u32(3);
    w.u32(2).u32(0x01000002).u32(8).u32(10);
    w.text("SFNT").u16(8).u16(0);
    w.text("a.txt").pad(4).text("dir/b").pad(4);
    w.pad(8);
    w.put32(0xC, static_cast<std::uint32_t>(w.data.size()));
    w.text("one").pad(8).text("hi");
    w.put32(8, static_cast<std::uint32_t>(w.data.size()));
    return w.data;
}

st::Bytes makeByml(bool big) {
    // root: {name = "Link", hp = 12, list = [1.5, true]}
    Writer w(big);
    w.text(big ? "BY" : "YB").u16(7).u32(0).u32(0).u32(0);
    auto stringTable = [&](std::size_t headerSlot, std::initializer_list<std::string_view> values) {
        const auto base = w.data.size();
        w.put32(headerSlot, static_cast<std::uint32_t>(base));
        w.u8(0xC2).u24(static_cast<std::uint32_t>(values.size()));
        for (std::size_t i = 0; i <= values.size(); ++i) w.u32(0);
        std::size_t slot = 0;
        for (auto value : values) {
            w.put32(base + 4 + slot++ * 4, static_cast<std::uint32_t>(w.data.size() - base));
            w.text(value).u8(0);
        }
        w.put32(base + 4 + slot * 4, static_cast<std::uint32_t>(w.data.size() - base));
        w.pad(4);
    };
    stringTable(4, {"hp", "list", "name"});
    stringTable(8, {"Link"});
    const auto array = w.data.size();
    w.u8(0xC0).u24(2).u8(0xD2).u8(0xD0).pad(4);
    w.u32(std::bit_cast<std::uint32_t>(1.5f)).u32(1);
    const auto root = w.data.size();
    w.put32(12, static_cast<std::uint32_t>(root));
    w.u8(0xC1).u24(3);
    w.u24(0).u8(0xD1).u32(12);
    w.u24(1).u8(0xC0).u32(static_cast<std::uint32_t>(array));
    w.u24(2).u8(0xA0).u32(0);
    return w.data;
}

st::Bytes makeMsbt(bool wide) {
    Writer w;
    w.text("MsgStdBn").u16(0xFEFF).u16(0).u8(wide ? 1 : 0).u8(3).u16(2).u16(0).u32(0).pad(0x20);
    Writer labels;
    labels.u32(1).u32(1).u32(12).u8(5).text("Hello").u32(1);
    Writer texts;
    auto encode = [&](std::string_view s) {
        for (char c : s) {
            if (wide) texts.u16(static_cast<std::uint8_t>(c));
            else texts.u8(static_cast<std::uint8_t>(c));
        }
    };
    texts.u32(2).u32(12).u32(0);
    encode("A");
    wide ? texts.u16(0) : texts.u8(0);
    texts.put32(8, static_cast<std::uint32_t>(texts.data.size()));
    encode("Hi ");
    wide ? texts.u16(0x0E) : texts.u8(0x0E);
    texts.u16(2).u16(1).u16(2).u8(7).u8(8);
    encode("there");
    wide ? texts.u16(0) : texts.u8(0);
    for (auto [name, section] : {std::pair{"LBL1", &labels}, std::pair{"TXT2", &texts}}) {
        w.text(name).u32(static_cast<std::uint32_t>(section->data.size())).u32(0).u32(0);
        w.data.insert(w.data.end(), section->data.begin(), section->data.end());
        w.pad(16);
    }
    return w.data;
}

}

TEST_CASE("murmur3 matches the reference implementation") {
    CHECK(st::murmur3(test::viewOf("")) == 0);
    CHECK(st::murmur3(test::viewOf("hello")) == 0x248BFA47);
    CHECK(st::murmur3(test::viewOf("hello"), 1) != 0x248BFA47);
}

TEST_CASE("zstd frames decompress with and without a stored size") {
    auto sized = st::zstdDecompress(st::fromHex("28b52ffd20f09d000060736176652074696e6b6572200100e1532509"), {}, 1000);
    REQUIRE_MESSAGE(sized.has_value(), (sized ? "" : sized.error()));
    CHECK(*sized == test::bytesOf(repeat("save tinker ", 20)));

    auto streamed = st::zstdDecompress(st::fromHex("28b52ffd000055000018616263010010293704"), {}, 1000);
    REQUIRE_MESSAGE(streamed.has_value(), (streamed ? "" : streamed.error()));
    CHECK(*streamed == test::bytesOf(repeat("abc", 50)));

    CHECK_FALSE(st::zstdDecompress(st::fromHex("28b52ffd20f09d000060736176652074696e6b6572200100e1532509"), {}, 10).has_value());
    CHECK_FALSE(st::zstdDecompress(test::bytesOf("not compressed"), {}, 1000).has_value());
}

TEST_CASE("SARC archives list their files") {
    auto entries = st::readSarc(makeSarc());
    REQUIRE_MESSAGE(entries.has_value(), (entries ? "" : entries.error()));
    REQUIRE(entries->size() == 2);
    CHECK((*entries)[0].name == "a.txt");
    CHECK((*entries)[0].data == test::bytesOf("one"));
    CHECK((*entries)[1].name == "dir/b");
    CHECK((*entries)[1].data == test::bytesOf("hi"));
    CHECK_FALSE(st::readSarc(test::bytesOf("SARC")).has_value());
}

TEST_CASE("BYML documents parse in both byte orders") {
    for (bool big : {false, true}) {
        auto root = st::readByml(makeByml(big));
        REQUIRE_MESSAGE(root.has_value(), (root ? "" : root.error()));
        const auto& map = *std::get<std::shared_ptr<st::BymlMap>>(root->value);
        CHECK(std::get<std::string>(map.at("name").value) == "Link");
        CHECK(std::get<std::int64_t>(map.at("hp").value) == 12);
        const auto& list = *std::get<std::shared_ptr<st::BymlArray>>(map.at("list").value);
        CHECK(std::get<double>(list[0].value) == 1.5);
        CHECK(std::get<bool>(list[1].value));
    }
}

TEST_CASE("MSBT messages keep labels and strip tags in plain text") {
    for (bool wide : {false, true}) {
        auto messages = st::readMsbt(makeMsbt(wide));
        REQUIRE_MESSAGE(messages.has_value(), (messages ? "" : messages.error()));
        REQUIRE(messages->size() == 2);
        CHECK((*messages)[0].label == "0");
        CHECK((*messages)[0].text == "A");
        CHECK((*messages)[1].label == "Hello");
        CHECK((*messages)[1].text == "Hi there");
        CHECK((*messages)[1].raw.size() == std::string("Hi there").size() + 9);
    }
}

TEST_CASE("formats are available to scripts") {
    st::Document document;
    st::ScriptContext context;
    context.editor = *st::parseManifest(R"({"id": "a", "name": "A", "version": "1.0.0",
        "games": [{"platform": "switch", "title_id": "0100000000001000"}]})");
    st::ScriptHost host(document, context, [](std::string_view) -> std::optional<std::string> { return ""; });
    CHECK(host.evaluate("return st.hash.murmur3('hello')").value() == std::to_string(0x248BFA47));
    CHECK(host.evaluate("return st.zstd.decompress(st.unhex('28b52ffd000055000018616263010010293704')):sub(1, 6)").value() ==
          "abcabc");
    const auto sarc = makeSarc();
    const std::string sarcHex = st::toHex(sarc);
    CHECK(host.evaluate("return st.format.sarc(st.unhex('" + sarcHex + "'))['dir/b']").value() == "hi");
    const std::string bymlHex = st::toHex(makeByml(false));
    CHECK(host.evaluate("local r = st.format.byml(st.unhex('" + bymlHex + "')) return r.name .. r.hp .. tostring(r.list[2])").value() ==
          "Link12true");
    const std::string msbtHex = st::toHex(makeMsbt(true));
    CHECK(host.evaluate("return st.format.msbt(st.unhex('" + msbtHex + "')).Hello").value() == "Hi there");
}

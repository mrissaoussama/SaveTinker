#include <doctest.h>

#include <algorithm>

#include <map>

#include "savetinker/editor_session.hpp"
#include "savetinker/formats.hpp"

using st::Bytes;
using st::UiNode;
using st::UiValue;
using st::WidgetKind;

namespace {

struct Fixture {
    st::Document document;
    std::unique_ptr<st::ScriptHost> host;
    std::unique_ptr<st::EditorSession> session;

    explicit Fixture(const std::string& code, std::map<std::string, std::string> data = {}) {
        document.addFile("save.dat", Bytes(32));
        st::ScriptContext context;
        context.platform = "switch";
        context.language = "fr";
        host = std::make_unique<st::ScriptHost>(document, context, [code](std::string_view name) -> std::optional<std::string> {
            if (name == "main") return code;
            return std::nullopt;
        });
        host->setDataLoader([data = std::move(data)](std::string_view path) -> std::optional<std::string> {
            auto found = data.find(std::string(path));
            if (found == data.end()) return std::nullopt;
            return found->second;
        });
        REQUIRE(host->loadEntry().has_value());
        session = std::make_unique<st::EditorSession>(*host);
        auto built = session->rebuild();
        REQUIRE_MESSAGE(built.has_value(), (built ? "" : built.error()));
    }

    const std::vector<UiNode>& nodes() const { return session->currentPage().nodes; }

    const UiNode& node(const std::string& label) const {
        for (const auto& n : nodes())
            if (n.label == label) return n;
        FAIL("no node labelled " << label);
        throw std::logic_error("unreachable");
    }

    bool has(const std::string& label) const {
        return std::ranges::any_of(nodes(), [&](const UiNode& n) { return n.label == label; });
    }

    void set(const std::string& label, UiValue value) {
        auto result = session->setValue(node(label), value);
        REQUIRE_MESSAGE(result.has_value(), (result ? "" : result.error()));
    }

    std::uint8_t byte(std::size_t offset) const { return document.file("save.dat").read<std::uint8_t>(offset); }
};

}

TEST_CASE("bound widgets show save values and infer ranges") {
    Fixture f(R"(
        function build(ui, save)
            local file = save:file("save.dat")
            ui:int{ label = "Gold", bind = file:field(0, "u16"), help = "Money" }
            ui:toggle{ label = "Hard", bind = file:flag(2, 0) }
            ui:text{ label = "Name", bind = file:text(4, 8, "utf16") }
            ui:label{ label = "Raw", value = 255, format = "hex" }
        end
    )");
    const auto& gold = f.node("Gold");
    CHECK(gold.kind == WidgetKind::Integer);
    CHECK(gold.value == UiValue(std::int64_t{0}));
    CHECK(gold.min == 0.0);
    CHECK(gold.max == 65535.0);
    CHECK(gold.help == "Money");
    CHECK(f.node("Name").maxLength == 8);
    CHECK(f.node("Raw").display == "0xFF");
    CHECK_FALSE(f.node("Raw").editable());
}

TEST_CASE("editing writes the save, rebuilds and can be undone") {
    Fixture f(R"(
        function build(ui, save)
            ui:int{ label = "Gold", bind = save:file("save.dat"):field(0, "u8") }
        end
    )");
    f.set("Gold", std::int64_t{42});
    CHECK(f.byte(0) == 42);
    CHECK(f.node("Gold").value == UiValue(std::int64_t{42}));
    CHECK(f.document.undoLabel() == "Gold");

    REQUIRE(f.session->undo().has_value());
    CHECK(f.node("Gold").value == UiValue(std::int64_t{0}));
    REQUIRE(f.session->redo().has_value());
    CHECK(f.byte(0) == 42);
}

TEST_CASE("values outside the range are refused") {
    Fixture f(R"(
        function build(ui, save)
            ui:int{ label = "Level", bind = save:file("save.dat"):field(0, "u8"), min = 1, max = 99 }
            ui:text{ label = "Name", bind = save:file("save.dat"):text(4, 3) }
        end
    )");
    CHECK_FALSE(f.session->setValue(f.node("Level"), std::int64_t{100}).has_value());
    CHECK_FALSE(f.session->setValue(f.node("Level"), std::string("x")).has_value());
    CHECK_FALSE(f.session->setValue(f.node("Name"), std::string("long")).has_value());
    CHECK(f.byte(0) == 0);
}

TEST_CASE("a toggle can spawn and hide other options") {
    Fixture f(R"(
        function build(ui, save)
            local file = save:file("save.dat")
            local dlc = file:flag(0, 0)
            ui:toggle{ label = "DLC", bind = dlc }
            if dlc:get() then
                ui:int{ label = "DLC level", bind = file:field(1, "u8") }
                ui:dropdown{ label = "DLC area", bind = file:field(2, "u8"), options = { "Beach", "Cave" } }
                ui:toggle{ label = "Boss beaten", bind = file:flag(3, 0) }
            end
        end
    )");
    CHECK(f.nodes().size() == 1);
    f.set("DLC", true);
    CHECK(f.nodes().size() == 4);
    CHECK(f.has("Boss beaten"));
    f.set("DLC", false);
    CHECK(f.nodes().size() == 1);
}

TEST_CASE("on_change can update other fields in the same undo step") {
    Fixture f(R"(
        function build(ui, save)
            local file = save:file("save.dat")
            ui:dropdown{
                label = "Class", bind = file:field(0, "u8"),
                options = { "Knight", "Mage" },
                on_change = function(value) file:set(1, "u8", value == 1 and 50 or 10) end,
            }
            ui:int{ label = "Mana", bind = file:field(1, "u8") }
        end
    )");
    f.set("Class", std::int64_t{1});
    CHECK(f.byte(1) == 50);
    CHECK(f.node("Mana").value == UiValue(std::int64_t{50}));
    CHECK(f.document.history().size() == 1);
    CHECK(f.session->undo().has_value());
    CHECK(f.byte(0) == 0);
    CHECK(f.byte(1) == 0);
}

TEST_CASE("option lists accept arrays, tables and maps") {
    Fixture f(R"(
        function build(ui, save)
            local file = save:file("save.dat")
            ui:dropdown{ label = "A", bind = file:field(0, "u8"), options = { "zero", "one" } }
            ui:dropdown{ label = "B", bind = file:field(0, "u8"), options = { { value = 7, label = "seven" } } }
            ui:dropdown{ label = "C", bind = file:field(0, "u8"), options = { [10] = "ten", [2] = "two" } }
        end
    )");
    CHECK(f.node("A").options[1].value == UiValue(std::int64_t{1}));
    CHECK(f.node("A").selectedOption() == 0u);
    CHECK(f.node("B").options[0].label == "seven");
    CHECK_FALSE(f.node("B").selectedOption().has_value());
    CHECK(f.node("C").options[0].label == "two");
    CHECK(f.node("C").options[1].value == UiValue(std::int64_t{10}));
}

TEST_CASE("labels load from data files with a language fallback") {
    Fixture f(R"(
        function build(ui, save)
            local items = st.labels("labels/items.json")
            local other = st.labels("labels/other.json")
            ui:dropdown{ label = "Item", bind = save:file("save.dat"):field(0, "u8"), options = items }
            ui:label{ label = "Other", value = other[3] }
        end
    )", {
        {"labels/fr/items.json", R"({"0": "Épée", "1": "Bouclier"})"},
        {"labels/items.json", R"({"0": "Sword"})"},
        {"labels/other.json", R"({"3": "Three"})"},
    });
    CHECK(f.node("Item").options.size() == 2);
    CHECK(f.node("Item").options[0].label == "Épée");
    CHECK(f.node("Other").display == "Three");
}

TEST_CASE("multiselect edits individual bits") {
    Fixture f(R"(
        function build(ui, save)
            ui:multiselect{ label = "Flags", bind = save:file("save.dat"):field(0, "u8"), bits = { [0] = "Fire", [3] = "Ice" } }
        end
    )");
    REQUIRE(f.session->setFlag(f.node("Flags"), 1, true).has_value());
    CHECK(f.byte(0) == 0b1000);
    CHECK(f.node("Flags").flagSet(1));
    CHECK_FALSE(f.node("Flags").flagSet(0));
    REQUIRE(f.session->setFlag(f.node("Flags"), 0, true).has_value());
    REQUIRE(f.session->setFlag(f.node("Flags"), 1, false).has_value());
    CHECK(f.byte(0) == 0b0001);
}

TEST_CASE("toggles with custom on and off values") {
    Fixture f(R"(
        function build(ui, save)
            ui:toggle{ label = "Unlocked", bind = save:file("save.dat"):field(0, "u8"), on = 255, off = 0 }
            ui:toggle{ label = "Numeric", bind = save:file("save.dat"):field(1, "u8") }
        end
    )");
    CHECK_FALSE(f.node("Unlocked").toggleState());
    f.set("Unlocked", f.node("Unlocked").onValue);
    CHECK(f.byte(0) == 255);
    CHECK(f.node("Unlocked").toggleState());
    f.set("Numeric", f.node("Numeric").onValue);
    CHECK(f.byte(1) == 1);
}

TEST_CASE("tristate needs three states") {
    Fixture ok(R"(
        function build(ui, save)
            ui:tristate{ label = "Quest", bind = save:file("save.dat"):field(0, "u8"), states = { "Locked", "Open", "Done" } }
        end
    )");
    CHECK(ok.node("Quest").kind == WidgetKind::Segmented);
    CHECK(ok.node("Quest").options.size() == 3);

    st::Document document;
    document.addFile("save.dat", Bytes(4));
    st::ScriptHost host(document, {}, [](std::string_view) -> std::optional<std::string> {
        return "function build(ui, save) ui:tristate{ label = 'Q', states = { 'a', 'b' } } end";
    });
    REQUIRE(host.loadEntry().has_value());
    st::EditorSession session(host);
    CHECK(session.rebuild().error().find("exactly 3") != std::string::npos);
}

TEST_CASE("buttons and side actions run as single undo steps") {
    Fixture f(R"(
        function build(ui, save)
            local file = save:file("save.dat")
            ui:section({ title = "Items", actions = { { label = "Clear", run = function()
                for i = 0, 3 do file:set(i, "u8", 0) end end } } }, function(s)
                s:int{ label = "Potions", bind = file:field(0, "u8"),
                       actions = { { label = "Max", run = function() file:set(0, "u8", 99) end } } }
            end)
            ui:button{ label = "Enable all", run = function()
                for i = 0, 3 do file:set(i, "u8", 1) end
            end }
        end
    )");
    REQUIRE(f.session->press(f.node("Enable all")).has_value());
    CHECK(f.byte(3) == 1);
    CHECK(f.document.history().size() == 1);
    CHECK(f.document.undoLabel() == "Enable all");

    const auto& section = f.node("Items");
    REQUIRE(section.children.size() == 1);
    REQUIRE(f.session->runAction(section.children[0], 0).has_value());
    CHECK(f.byte(0) == 99);
    REQUIRE(f.session->runAction(f.node("Items"), 0).has_value());
    CHECK(f.byte(0) == 0);
    CHECK(f.document.undoLabel() == "Clear");
}

TEST_CASE("script errors during an edit roll everything back") {
    Fixture f(R"(
        function build(ui, save)
            local file = save:file("save.dat")
            ui:button{ label = "Broken", run = function() file:set(0, "u8", 5); error("nope") end }
        end
    )");
    const auto result = f.session->press(f.node("Broken"));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().find("nope") != std::string::npos);
    CHECK(f.byte(0) == 0);
    CHECK_FALSE(f.document.canUndo());
}

TEST_CASE("ui state survives rebuilds and filters lists") {
    Fixture f(R"(
        local items = { { name = "Sword", kind = "weapon" }, { name = "Shield", kind = "armor" }, { name = "Axe", kind = "weapon" } }
        function build(ui, save)
            local filter = ui:state("filter", "all")
            ui:dropdown{ label = "Show", bind = filter, options = { { value = "all", label = "All" }, { value = "weapon", label = "Weapons" } } }
            for _, item in ipairs(items) do
                if filter:get() == "all" or item.kind == filter:get() then ui:label{ label = item.name } end
            end
        end
    )");
    CHECK(f.nodes().size() == 4);
    f.set("Show", std::string("weapon"));
    CHECK(f.nodes().size() == 3);
    CHECK_FALSE(f.has("Shield"));
    CHECK_FALSE(f.document.canUndo());
}

TEST_CASE("pages are built only when opened and support nesting") {
    Fixture f(R"(
        built = {}
        function build(ui, save)
            ui:page("Player", function(p)
                built.player = true
                p:int{ label = "Gold", bind = save:file("save.dat"):field(0, "u8") }
                p:page("Stats", function(s)
                    built.stats = true
                    s:int{ label = "Strength", bind = save:file("save.dat"):field(1, "u8") }
                end)
            end)
            ui:page("World", function(p) built.world = true; p:note("Map") end)
        end
    )");
    CHECK(f.session->topLevelLinks().size() == 2);
    CHECK(f.session->currentPage().title == "Player");
    CHECK(f.host->evaluate("return tostring(built.world)").value() == "nil");

    const auto statsLink = f.node("Stats");
    REQUIRE(statsLink.kind == WidgetKind::PageLink);
    REQUIRE(f.session->openPage(statsLink.pageId).has_value());
    CHECK(f.session->currentPage().title == "Stats");
    CHECK(f.session->breadcrumbs().size() == 2);

    f.set("Strength", std::int64_t{7});
    CHECK(f.session->currentPage().title == "Stats");
    CHECK(f.byte(1) == 7);

    REQUIRE(f.session->back().has_value());
    CHECK(f.session->currentPage().title == "Player");
    REQUIRE(f.session->openTopLevel(f.session->topLevelLinks()[1].pageId).has_value());
    CHECK(f.session->currentPage().title == "World");
    CHECK(f.host->evaluate("return tostring(built.world)").value() == "true");
}

TEST_CASE("an open page that disappears falls back to its parent") {
    Fixture f(R"(
        function build(ui, save)
            local file = save:file("save.dat")
            ui:page("Main", function(p)
                p:toggle{ label = "Show extra", bind = file:flag(0, 0) }
                if file:bit(0, 0) then
                    p:page("Extra", function(e)
                        e:toggle{ label = "Hide me", bind = file:flag(0, 0) }
                    end)
                end
            end)
        end
    )");
    f.set("Show extra", true);
    REQUIRE(f.session->openPage(f.node("Extra").pageId).has_value());
    f.set("Hide me", false);
    CHECK(f.session->currentPage().title == "Main");
}

TEST_CASE("popups keep their own widgets") {
    Fixture f(R"(
        function build(ui, save)
            ui:popup({ label = "Advanced" }, function(p)
                p:warning("Careful")
                p:int{ label = "Raw", bind = save:file("save.dat"):field(0, "u8") }
            end)
        end
    )");
    const auto& popup = f.node("Advanced");
    CHECK(popup.kind == WidgetKind::Popup);
    REQUIRE(popup.children.size() == 2);
    CHECK(popup.children[0].kind == WidgetKind::Warning);
    REQUIRE(f.session->setValue(popup.children[1], std::int64_t{3}).has_value());
    CHECK(f.byte(0) == 3);
}

TEST_CASE("custom bindings with st.bind") {
    Fixture f(R"(
        local stored = 5
        function build(ui, save)
            ui:int{ label = "Virtual", bind = st.bind(function() return stored end, function(v) stored = v end) }
        end
    )");
    CHECK(f.node("Virtual").value == UiValue(std::int64_t{5}));
    f.set("Virtual", std::int64_t{9});
    CHECK(f.node("Virtual").value == UiValue(std::int64_t{9}));
}

TEST_CASE("builder cannot be used after build returns") {
    Fixture f(R"(
        function build(ui, save) saved_ui = ui; ui:note("x") end
    )");
    CHECK(f.host->run("saved_ui:note('late')").error().find("outside of build") != std::string::npos);
}

TEST_CASE("editors without build report it") {
    st::Document document;
    document.addFile("save.dat", Bytes(4));
    st::ScriptHost host(document, {}, [](std::string_view) -> std::optional<std::string> { return "x = 1"; });
    REQUIRE(host.loadEntry().has_value());
    st::EditorSession session(host);
    CHECK_FALSE(session.hasUi());
    CHECK_FALSE(session.rebuild().has_value());
}

TEST_CASE("images take encoded files or raw pixels") {
    Fixture f(R"(
        function build(ui, save)
            ui:image{ label = "Encoded", data = "\137PNG fake" }
            ui:image{ label = "Thumbnail", pixels = string.rep("\255\0\0\255", 4), width = 2, height = 2 }
            ui:image{ label = "Small", pixels = string.rep("\0\248", 1), width = 1, height = 1, format = "rgb565" }
        end
    )");
    CHECK(f.node("Encoded").kind == WidgetKind::Image);
    CHECK(f.node("Encoded").image.size() == 9);
    const auto& tga = f.node("Thumbnail").image;
    REQUIRE(tga.size() == 18 + 16);
    CHECK(tga[12] == 2);
    CHECK(tga[18] == 0);
    CHECK(tga[20] == 255);
    CHECK(f.node("Small").image[20] == 255);

    CHECK_FALSE(st::pixelsToTga(st::Bytes(3), 1, 1, "rgba8").has_value());
    CHECK_FALSE(st::pixelsToTga(st::Bytes(4), 1, 1, "yuv").has_value());
}

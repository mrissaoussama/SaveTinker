#include "savetinker/editor_session.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <functional>
#include <new>

#include "lua_support.hpp"
#include "savetinker/formats.hpp"

namespace st {

namespace {

constexpr const char* kBuilderType = "SaveTinker.UiBuilder";
constexpr const char* kStateType = "SaveTinker.UiState";

}

std::string uiValueToString(const UiValue& value) {
    return std::visit(
        [](const auto& v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::monostate>) return "";
            else if constexpr (std::is_same_v<T, bool>) return v ? "true" : "false";
            else if constexpr (std::is_same_v<T, std::int64_t>) return std::to_string(v);
            else if constexpr (std::is_same_v<T, double>) return std::format("{}", v);
            else return v;
        },
        value);
}

namespace {

bool valuesEqual(const UiValue& a, const UiValue& b) {
    if (a.index() == b.index()) return a == b;
    auto asNumber = [](const UiValue& v) -> std::optional<double> {
        if (auto i = std::get_if<std::int64_t>(&v)) return static_cast<double>(*i);
        if (auto d = std::get_if<double>(&v)) return *d;
        if (auto b = std::get_if<bool>(&v)) return *b ? 1.0 : 0.0;
        return std::nullopt;
    };
    const auto x = asNumber(a), y = asNumber(b);
    return x && y && *x == *y;
}

std::int64_t asInteger(const UiValue& value) {
    if (auto i = std::get_if<std::int64_t>(&value)) return *i;
    if (auto d = std::get_if<double>(&value)) return static_cast<std::int64_t>(*d);
    if (auto b = std::get_if<bool>(&value)) return *b ? 1 : 0;
    return 0;
}

UiValue readValue(lua_State* L, int index) {
    switch (lua_type(L, index)) {
    case LUA_TBOOLEAN: return lua_toboolean(L, index) != 0;
    case LUA_TNUMBER:
        if (lua_isinteger(L, index)) return static_cast<std::int64_t>(lua_tointeger(L, index));
        return lua_tonumber(L, index);
    case LUA_TSTRING: return std::string(lua::checkString(L, index));
    default: return std::monostate{};
    }
}

void pushValue(lua_State* L, const UiValue& value) {
    std::visit(
        [L](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::monostate>) lua_pushnil(L);
            else if constexpr (std::is_same_v<T, bool>) lua_pushboolean(L, v);
            else if constexpr (std::is_same_v<T, std::int64_t>) lua_pushinteger(L, v);
            else if constexpr (std::is_same_v<T, double>) lua_pushnumber(L, v);
            else lua_pushlstring(L, v.data(), v.size());
        },
        value);
}

}

struct EditorSession::BuildContext {
    lua_State* L = nullptr;
    bool active = false;
    int refsTable = 0;
    int stateTable = 0;
    std::vector<std::vector<UiNode>*> containers;
    std::string pageId;

    int storeRef(int index) {
        index = lua_absindex(L, index);
        lua_rawgeti(L, LUA_REGISTRYINDEX, refsTable);
        lua_pushvalue(L, index);
        const int ref = luaL_ref(L, -2);
        lua_pop(L, 1);
        return ref;
    }

    std::vector<UiNode>& container() { return *containers.back(); }
};

namespace {

using Context = EditorSession::BuildContext;

struct BuilderRef {
    std::shared_ptr<Context> context;
};

struct StateRef {
    int tableRef;
    std::string name;
};

Context& checkBuilder(lua_State* L) {
    auto* builder = static_cast<BuilderRef*>(luaL_checkudata(L, 1, kBuilderType));
    if (!builder->context || !builder->context->active) luaL_error(L, "ui builder used outside of build()");
    return *builder->context;
}

std::optional<std::string> optStringField(lua_State* L, int table, const char* key) {
    lua_getfield(L, table, key);
    std::optional<std::string> result;
    if (lua_type(L, -1) == LUA_TSTRING) result = std::string(lua::checkString(L, -1));
    else if (!lua_isnil(L, -1)) luaL_error(L, "'%s' must be a string", key);
    lua_pop(L, 1);
    return result;
}

std::optional<double> optNumberField(lua_State* L, int table, const char* key) {
    lua_getfield(L, table, key);
    std::optional<double> result;
    if (lua_type(L, -1) == LUA_TNUMBER) result = lua_tonumber(L, -1);
    else if (!lua_isnil(L, -1)) luaL_error(L, "'%s' must be a number", key);
    lua_pop(L, 1);
    return result;
}

bool boolField(lua_State* L, int table, const char* key) {
    lua_getfield(L, table, key);
    const bool result = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return result;
}

int functionField(Context& context, int table, const char* key) {
    lua_State* L = context.L;
    lua_getfield(L, table, key);
    int ref = 0;
    if (lua_isfunction(L, -1)) ref = context.storeRef(-1);
    else if (!lua_isnil(L, -1)) luaL_error(L, "'%s' must be a function", key);
    lua_pop(L, 1);
    return ref;
}

// Accepts {"a", "b"} (values 0, 1, ...), {{value = 3, label = "c"}} or {[3] = "c"}.
std::vector<UiOption> readOptions(lua_State* L, int index) {
    std::vector<UiOption> options;
    index = lua_absindex(L, index);
    if (!lua_istable(L, index)) luaL_error(L, "options must be a table");
    const auto length = static_cast<lua_Integer>(lua_rawlen(L, index));
    lua_Integer keyCount = 0;
    lua_pushnil(L);
    while (lua_next(L, index)) {
        ++keyCount;
        lua_pop(L, 1);
    }
    if (length > 0 && keyCount == length) {
        for (lua_Integer i = 1; i <= length; ++i) {
            lua_rawgeti(L, index, i);
            if (lua_type(L, -1) == LUA_TSTRING) {
                options.push_back({static_cast<std::int64_t>(i - 1), std::string(lua::checkString(L, -1))});
            } else if (lua_istable(L, -1)) {
                const int entry = lua_gettop(L);
                lua_getfield(L, entry, "value");
                UiValue value = readValue(L, -1);
                lua_pop(L, 1);
                if (std::holds_alternative<std::monostate>(value)) value = static_cast<std::int64_t>(i - 1);
                options.push_back({value, optStringField(L, entry, "label").value_or(uiValueToString(value))});
            } else {
                luaL_error(L, "option %d must be a string or a table", static_cast<int>(i));
            }
            lua_pop(L, 1);
        }
        return options;
    }
    lua_pushnil(L);
    while (lua_next(L, index)) {
        std::size_t textLength = 0;
        const char* text = luaL_tolstring(L, -1, &textLength);
        options.push_back({readValue(L, -3), std::string(text, textLength)});
        lua_pop(L, 2);
    }
    std::ranges::sort(options, [](const UiOption& a, const UiOption& b) {
        if (a.value.index() != b.value.index()) return a.value.index() < b.value.index();
        return a.value < b.value;
    });
    return options;
}

std::vector<UiOption> optionsField(lua_State* L, int table, std::initializer_list<const char*> keys) {
    for (const char* key : keys) {
        lua_getfield(L, table, key);
        if (!lua_isnil(L, -1)) {
            auto options = readOptions(L, -1);
            lua_pop(L, 1);
            return options;
        }
        lua_pop(L, 1);
    }
    return {};
}

std::vector<UiAction> actionsField(Context& context, int table) {
    lua_State* L = context.L;
    std::vector<UiAction> actions;
    lua_getfield(L, table, "actions");
    if (lua_istable(L, -1)) {
        const int list = lua_gettop(L);
        const auto length = static_cast<lua_Integer>(lua_rawlen(L, list));
        for (lua_Integer i = 1; i <= length; ++i) {
            lua_rawgeti(L, list, i);
            if (!lua_istable(L, -1)) luaL_error(L, "each action must be a table {label=..., run=...}");
            const int entry = lua_gettop(L);
            UiAction action{optStringField(L, entry, "label").value_or("Action"), functionField(context, entry, "run")};
            if (!action.functionRef) luaL_error(L, "action '%s' needs a run function", action.label.c_str());
            actions.push_back(std::move(action));
            lua_pop(L, 1);
        }
    } else if (!lua_isnil(L, -1)) {
        luaL_error(L, "'actions' must be a list");
    }
    lua_pop(L, 1);
    return actions;
}

std::string makeId(const Context& context, int table, const std::string& label) {
    if (auto key = optStringField(context.L, table, "key")) return context.pageId + "/" + *key;
    return context.pageId + "/" + label + "#" + std::to_string(context.containers.back()->size());
}

UiNode readSpec(Context& context, WidgetKind kind) {
    lua_State* L = context.L;
    luaL_checktype(L, 2, LUA_TTABLE);
    UiNode node;
    node.kind = kind;
    node.label = optStringField(L, 2, "label").value_or("");
    node.id = makeId(context, 2, node.label);
    node.help = optStringField(L, 2, "help").value_or("");
    node.readOnly = boolField(L, 2, "readonly");
    node.actions = actionsField(context, 2);
    node.changeRef = functionField(context, 2, "on_change");

    lua_getfield(L, 2, "bind");
    if (!lua_isnil(L, -1)) {
        const int bind = lua_gettop(L);
        if (auto info = lua::fieldBindingInfo(L, bind)) {
            node.min = info->min;
            node.max = info->max;
            node.maxLength = info->maxLength;
        }
        lua_getfield(L, bind, "get");
        if (!lua_isfunction(L, -1)) luaL_error(L, "'bind' needs get and set methods");
        lua_pushvalue(L, bind);
        lua_call(L, 1, 1);
        node.value = readValue(L, -1);
        lua_pop(L, 1);
        node.bindingRef = context.storeRef(bind);
    } else {
        lua_getfield(L, 2, "value");
        node.value = readValue(L, -1);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);

    if (auto min = optNumberField(L, 2, "min")) node.min = min;
    if (auto max = optNumberField(L, 2, "max")) node.max = max;
    if (auto step = optNumberField(L, 2, "step")) node.step = *step;
    if (auto length = optNumberField(L, 2, "max_length")) node.maxLength = static_cast<std::size_t>(*length);
    node.hex = optStringField(L, 2, "format") == "hex";
    return node;
}

int add(Context& context, UiNode node) {
    context.container().push_back(std::move(node));
    return 0;
}

int buildInteger(lua_State* L) { auto& c = checkBuilder(L); return add(c, readSpec(c, WidgetKind::Integer)); }
int buildFloat(lua_State* L) { auto& c = checkBuilder(L); return add(c, readSpec(c, WidgetKind::Float)); }
int buildText(lua_State* L) { auto& c = checkBuilder(L); return add(c, readSpec(c, WidgetKind::Text)); }

int buildToggle(lua_State* L) {
    auto& context = checkBuilder(L);
    auto node = readSpec(context, WidgetKind::Toggle);
    lua_getfield(L, 2, "on");
    lua_getfield(L, 2, "off");
    const bool custom = !lua_isnil(L, -2) || !lua_isnil(L, -1);
    if (custom) {
        node.onValue = lua_isnil(L, -2) ? UiValue(std::int64_t{1}) : readValue(L, -2);
        node.offValue = lua_isnil(L, -1) ? UiValue(std::int64_t{0}) : readValue(L, -1);
    } else if (!std::holds_alternative<bool>(node.value) && !std::holds_alternative<std::monostate>(node.value)) {
        node.onValue = std::int64_t{1};
        node.offValue = std::int64_t{0};
    }
    lua_pop(L, 2);
    return add(context, std::move(node));
}

int buildChoice(lua_State* L) {
    auto& context = checkBuilder(L);
    auto node = readSpec(context, WidgetKind::Choice);
    node.options = optionsField(L, 2, {"options"});
    return add(context, std::move(node));
}

int buildSegmented(lua_State* L) {
    auto& context = checkBuilder(L);
    auto node = readSpec(context, WidgetKind::Segmented);
    node.options = optionsField(L, 2, {"states", "options"});
    return add(context, std::move(node));
}

int buildTriState(lua_State* L) {
    auto& context = checkBuilder(L);
    auto node = readSpec(context, WidgetKind::Segmented);
    node.options = optionsField(L, 2, {"states", "options"});
    if (node.options.empty()) {
        node.options = {{std::int64_t{0}, "Off"}, {std::int64_t{1}, "On"}, {std::int64_t{2}, "Mixed"}};
    } else if (node.options.size() != 3) {
        return luaL_error(L, "tristate needs exactly 3 states");
    }
    return add(context, std::move(node));
}

int buildFlags(lua_State* L) {
    auto& context = checkBuilder(L);
    auto node = readSpec(context, WidgetKind::Flags);
    node.options = optionsField(L, 2, {"bits", "options"});
    for (const auto& option : node.options) {
        const auto bit = asInteger(option.value);
        if (!std::holds_alternative<std::int64_t>(option.value) || bit < 0 || bit > 63)
            return luaL_error(L, "multiselect bits must be integers 0-63");
    }
    return add(context, std::move(node));
}

int buildButton(lua_State* L) {
    auto& context = checkBuilder(L);
    auto node = readSpec(context, WidgetKind::Button);
    node.actionRef = functionField(context, 2, "run");
    if (!node.actionRef) return luaL_error(L, "button '%s' needs a run function", node.label.c_str());
    return add(context, std::move(node));
}

int buildLabel(lua_State* L) {
    auto& context = checkBuilder(L);
    auto node = readSpec(context, WidgetKind::Label);
    node.display = node.hex && std::holds_alternative<std::int64_t>(node.value)
                       ? std::format("0x{:X}", std::get<std::int64_t>(node.value))
                       : uiValueToString(node.value);
    return add(context, std::move(node));
}

int buildMessage(lua_State* L, WidgetKind kind) {
    auto& context = checkBuilder(L);
    UiNode node;
    node.kind = kind;
    if (lua_type(L, 2) == LUA_TSTRING) node.label = lua::checkString(L, 2);
    else if (lua_istable(L, 2)) node.label = optStringField(L, 2, "text").value_or("");
    else return luaL_argerror(L, 2, "text expected");
    node.id = context.pageId + "/msg#" + std::to_string(context.container().size());
    return add(context, std::move(node));
}

int buildNote(lua_State* L) { return buildMessage(L, WidgetKind::Note); }
int buildWarning(lua_State* L) { return buildMessage(L, WidgetKind::Warning); }

int buildSeparator(lua_State* L) {
    auto& context = checkBuilder(L);
    UiNode node;
    node.kind = WidgetKind::Separator;
    node.id = context.pageId + "/sep#" + std::to_string(context.container().size());
    return add(context, std::move(node));
}

int buildImage(lua_State* L) {
    auto& context = checkBuilder(L);
    luaL_checktype(L, 2, LUA_TTABLE);
    UiNode node;
    node.kind = WidgetKind::Image;
    node.label = optStringField(L, 2, "label").value_or("");
    node.id = makeId(context, 2, node.label);
    node.help = optStringField(L, 2, "help").value_or("");
    if (auto data = optStringField(L, 2, "data")) {
        node.image.assign(data->begin(), data->end());
    } else if (auto pixels = optStringField(L, 2, "pixels")) {
        const auto width = static_cast<int>(optNumberField(L, 2, "width").value_or(0));
        const auto height = static_cast<int>(optNumberField(L, 2, "height").value_or(0));
        const auto format = optStringField(L, 2, "format").value_or("rgba8");
        auto tga = pixelsToTga(lua::asBytes(*pixels), width, height, format);
        if (!tga) return luaL_error(L, "image '%s': %s", node.label.c_str(), tga.error().c_str());
        node.image = std::move(*tga);
    } else {
        return luaL_error(L, "image '%s' needs data or pixels", node.label.c_str());
    }
    return add(context, std::move(node));
}

UiNode readContainerHeader(Context& context, WidgetKind kind, const char* titleKey) {
    lua_State* L = context.L;
    UiNode node;
    node.kind = kind;
    if (lua_type(L, 2) == LUA_TSTRING) {
        node.label = lua::checkString(L, 2);
        node.id = context.pageId + "/" + node.label;
    } else if (lua_istable(L, 2)) {
        node.label = optStringField(L, 2, titleKey).value_or(optStringField(L, 2, "label").value_or(""));
        node.id = makeId(context, 2, node.label);
        node.help = optStringField(L, 2, "help").value_or("");
        node.collapsed = boolField(L, 2, "collapsed");
        node.actions = actionsField(context, 2);
    } else {
        luaL_argerror(L, 2, "title or table expected");
    }
    return node;
}

void buildChildren(Context& context, UiNode& node, int functionIndex) {
    lua_State* L = context.L;
    context.containers.push_back(&node.children);
    lua_pushvalue(L, functionIndex);
    lua_pushvalue(L, 1);
    lua_call(L, 1, 0);
    context.containers.pop_back();
}

int buildSection(lua_State* L) {
    auto& context = checkBuilder(L);
    auto node = readContainerHeader(context, WidgetKind::Section, "title");
    luaL_checktype(L, 3, LUA_TFUNCTION);
    buildChildren(context, node, 3);
    return add(context, std::move(node));
}

int buildPopup(lua_State* L) {
    auto& context = checkBuilder(L);
    auto node = readContainerHeader(context, WidgetKind::Popup, "label");
    int function = 3;
    if (lua_istable(L, 2) && lua_isnoneornil(L, 3)) {
        lua_getfield(L, 2, "build");
        lua_replace(L, 3);
    }
    luaL_checktype(L, function, LUA_TFUNCTION);
    buildChildren(context, node, function);
    return add(context, std::move(node));
}

int buildPage(lua_State* L) {
    auto& context = checkBuilder(L);
    auto node = readContainerHeader(context, WidgetKind::PageLink, "title");
    luaL_checktype(L, 3, LUA_TFUNCTION);
    node.pageId = node.id;
    node.actionRef = context.storeRef(3);
    return add(context, std::move(node));
}

int buildState(lua_State* L) {
    auto& context = checkBuilder(L);
    const std::string name(lua::checkString(L, 2));
    lua_rawgeti(L, LUA_REGISTRYINDEX, context.stateTable);
    lua_getfield(L, -1, name.c_str());
    if (lua_isnil(L, -1)) {
        lua_pushvalue(L, 3);
        lua_setfield(L, -3, name.c_str());
    }
    lua_pop(L, 2);
    void* memory = lua_newuserdatauv(L, sizeof(StateRef), 0);
    new (memory) StateRef{context.stateTable, name};
    luaL_setmetatable(L, kStateType);
    return 1;
}

int builderCollect(lua_State* L) {
    static_cast<BuilderRef*>(luaL_checkudata(L, 1, kBuilderType))->~BuilderRef();
    return 0;
}

StateRef& checkState(lua_State* L) {
    return *static_cast<StateRef*>(luaL_checkudata(L, 1, kStateType));
}

int stateGet(lua_State* L) {
    auto& state = checkState(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, state.tableRef);
    lua_getfield(L, -1, state.name.c_str());
    return 1;
}

int stateSet(lua_State* L) {
    auto& state = checkState(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, state.tableRef);
    lua_pushvalue(L, 2);
    lua_setfield(L, -2, state.name.c_str());
    return 0;
}

int stateCollect(lua_State* L) {
    checkState(L).~StateRef();
    return 0;
}

void registerUiTypes(lua_State* L) {
    if (luaL_getmetatable(L, kBuilderType) != LUA_TNIL) {
        lua_pop(L, 1);
        return;
    }
    lua_pop(L, 1);

    static const luaL_Reg builderMethods[] = {
        {"int", lua::guarded<buildInteger>},
        {"float", lua::guarded<buildFloat>},
        {"text", lua::guarded<buildText>},
        {"toggle", lua::guarded<buildToggle>},
        {"dropdown", lua::guarded<buildChoice>},
        {"segmented", lua::guarded<buildSegmented>},
        {"tristate", lua::guarded<buildTriState>},
        {"multiselect", lua::guarded<buildFlags>},
        {"button", lua::guarded<buildButton>},
        {"label", lua::guarded<buildLabel>},
        {"note", lua::guarded<buildNote>},
        {"warning", lua::guarded<buildWarning>},
        {"separator", lua::guarded<buildSeparator>},
        {"image", lua::guarded<buildImage>},
        {"section", lua::guarded<buildSection>},
        {"popup", lua::guarded<buildPopup>},
        {"page", lua::guarded<buildPage>},
        {"state", lua::guarded<buildState>},
        {nullptr, nullptr},
    };
    luaL_newmetatable(L, kBuilderType);
    lua_pushcfunction(L, builderCollect);
    lua_setfield(L, -2, "__gc");
    luaL_newlib(L, builderMethods);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);

    static const luaL_Reg stateMethods[] = {
        {"get", stateGet},
        {"set", stateSet},
        {nullptr, nullptr},
    };
    luaL_newmetatable(L, kStateType);
    lua_pushcfunction(L, stateCollect);
    lua_setfield(L, -2, "__gc");
    luaL_newlib(L, stateMethods);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);
}

const UiNode* findPageLink(const std::vector<UiNode>& nodes, const std::string& pageId) {
    for (const auto& node : nodes) {
        if (node.kind == WidgetKind::PageLink && node.pageId == pageId) return &node;
        if (node.kind == WidgetKind::Section)
            if (auto* found = findPageLink(node.children, pageId)) return found;
    }
    return nullptr;
}

void collectLinks(const std::vector<UiNode>& nodes, std::vector<UiNode>& out) {
    for (const auto& node : nodes) {
        if (node.kind == WidgetKind::PageLink) out.push_back(node);
        else if (node.kind == WidgetKind::Section) collectLinks(node.children, out);
    }
}

bool hasContent(const std::vector<UiNode>& nodes) {
    return std::ranges::any_of(nodes, [](const UiNode& node) {
        if (node.kind == WidgetKind::PageLink) return false;
        if (node.kind == WidgetKind::Section) return hasContent(node.children);
        return true;
    });
}

}

std::optional<std::size_t> UiNode::selectedOption() const {
    for (std::size_t i = 0; i < options.size(); ++i)
        if (valuesEqual(options[i].value, value)) return i;
    return std::nullopt;
}

bool UiNode::flagSet(std::size_t optionIndex) const {
    if (optionIndex >= options.size()) return false;
    const auto bit = asInteger(options[optionIndex].value);
    return (static_cast<std::uint64_t>(asInteger(value)) >> bit) & 1u;
}

bool UiNode::toggleState() const {
    if (auto flag = std::get_if<bool>(&value); flag && std::holds_alternative<bool>(onValue)) return *flag == std::get<bool>(onValue);
    return valuesEqual(value, onValue);
}

EditorSession::EditorSession(ScriptHost& host) : host_(host) {
    lua_State* L = host_.state();
    registerUiTypes(L);
    lua_newtable(L);
    stateTable_ = luaL_ref(L, LUA_REGISTRYINDEX);
}

EditorSession::~EditorSession() {
    lua_State* L = host_.state();
    if (context_) context_->active = false;
    if (refsTable_) luaL_unref(L, LUA_REGISTRYINDEX, refsTable_);
    luaL_unref(L, LUA_REGISTRYINDEX, stateTable_);
}

bool EditorSession::hasUi() {
    return host_.hasFunction("build");
}

std::expected<void, std::string> EditorSession::rebuild() {
    lua_State* L = host_.state();
    if (!hasUi()) return std::unexpected("editor has no build(ui, save) function");

    auto context = std::make_shared<BuildContext>();
    context->L = L;
    context->stateTable = stateTable_;
    lua_newtable(L);
    context->refsTable = luaL_ref(L, LUA_REGISTRYINDEX);

    UiPage root{"", "General", {}};
    std::vector<UiPage> stack;
    stack.reserve(activePath_.size() + 1);
    std::vector<std::string> path;

    auto runPage = [&](int functionRef, std::vector<UiNode>& target, const std::string& pageId) -> std::expected<void, std::string> {
        context->containers = {&target};
        context->pageId = pageId;
        lua_rawgeti(L, LUA_REGISTRYINDEX, context->refsTable);
        if (functionRef) lua_rawgeti(L, -1, functionRef);
        else lua_getglobal(L, "build");
        lua_remove(L, -2);
        auto* builder = static_cast<BuilderRef*>(lua_newuserdatauv(L, sizeof(BuilderRef), 0));
        new (builder) BuilderRef{context};
        luaL_setmetatable(L, kBuilderType);
        lua::pushSave(L, host_.document());
        return host_.call(2, 0);
    };

    context->active = true;
    auto result = runPage(0, root.nodes, "");
    if (result) {
        const UiNode* link = nullptr;
        const std::vector<UiNode>* level = &root.nodes;
        for (std::size_t depth = 0;; ++depth) {
            std::string wanted;
            if (depth < activePath_.size()) {
                wanted = activePath_[depth];
            } else if (depth == 0 && activePath_.empty() && !hasContent(root.nodes)) {
                std::vector<UiNode> links;
                collectLinks(root.nodes, links);
                if (!links.empty()) wanted = links.front().pageId;
            }
            if (wanted.empty() || !(link = findPageLink(*level, wanted))) break;
            const int functionRef = link->actionRef;
            UiPage page{link->pageId, link->label, {}};
            stack.push_back(std::move(page));
            if (result = runPage(functionRef, stack.back().nodes, stack.back().id); !result) break;
            path.push_back(wanted);
            level = &stack.back().nodes;
        }
    }
    context->active = false;

    if (!result) {
        luaL_unref(L, LUA_REGISTRYINDEX, context->refsTable);
        return result;
    }

    if (context_) context_->active = false;
    if (refsTable_) luaL_unref(L, LUA_REGISTRYINDEX, refsTable_);
    context_ = std::move(context);
    refsTable_ = context_->refsTable;
    root_ = std::move(root);
    stack_ = std::move(stack);
    activePath_ = std::move(path);
    topLinks_.clear();
    collectLinks(root_.nodes, topLinks_);
    breadcrumbs_.clear();
    for (const auto& page : stack_) breadcrumbs_.push_back({page.id, page.title});
    return {};
}

const UiPage& EditorSession::currentPage() const {
    return stack_.empty() ? root_ : stack_.back();
}

std::expected<void, std::string> EditorSession::openPage(const std::string& pageId) {
    activePath_.push_back(pageId);
    return rebuild();
}

std::expected<void, std::string> EditorSession::openTopLevel(const std::string& pageId) {
    // An empty id selects the root page, which the placeholder keeps from auto-selecting the first page.
    activePath_ = {pageId};
    return rebuild();
}

std::expected<void, std::string> EditorSession::back() {
    if (!activePath_.empty()) activePath_.pop_back();
    return rebuild();
}

std::expected<void, std::string> EditorSession::invoke(int functionRef, const std::string& label,
                                                       const UiValue* argument, int bindingRef) {
    lua_State* L = host_.state();
    auto& document = host_.document();
    document.beginTransaction(label.empty() ? "Edit" : label);

    auto callRef = [&](int ref, bool asSetter) -> std::expected<void, std::string> {
        lua_rawgeti(L, LUA_REGISTRYINDEX, refsTable_);
        lua_rawgeti(L, -1, ref);
        lua_remove(L, -2);
        int arguments = 0;
        if (asSetter) {
            const int binding = lua_gettop(L);
            lua_getfield(L, binding, "set");
            lua_insert(L, binding);
            arguments = 1;
        }
        if (argument) {
            pushValue(L, *argument);
            ++arguments;
        }
        return host_.call(arguments, 0);
    };

    std::expected<void, std::string> result;
    if (bindingRef) result = callRef(bindingRef, true);
    if (result && functionRef) result = callRef(functionRef, false);
    if (!result) {
        document.rollbackTransaction();
        return result;
    }
    document.commitTransaction();
    return rebuild();
}

std::expected<void, std::string> EditorSession::setValue(const UiNode& node, const UiValue& value) {
    if (!node.editable()) return std::unexpected("'" + node.label + "' is read only");
    UiValue checked = value;
    if (node.kind == WidgetKind::Integer || node.kind == WidgetKind::Float) {
        const double number = std::holds_alternative<std::int64_t>(value) ? static_cast<double>(std::get<std::int64_t>(value))
                              : std::holds_alternative<double>(value)     ? std::get<double>(value)
                                                                          : NAN;
        if (std::isnan(number)) return std::unexpected("'" + node.label + "' needs a number");
        if ((node.min && number < *node.min) || (node.max && number > *node.max))
            return std::unexpected(std::format("'{}' must be between {} and {}", node.label, node.min.value_or(-INFINITY),
                                               node.max.value_or(INFINITY)));
        if (node.kind == WidgetKind::Integer) checked = static_cast<std::int64_t>(std::llround(number));
        else checked = number;
    }
    if (node.kind == WidgetKind::Text && node.maxLength && std::get_if<std::string>(&value) &&
        utf8ToUtf16(std::get<std::string>(value)).size() > node.maxLength)
        return std::unexpected(std::format("'{}' can be at most {} characters", node.label, node.maxLength));
    return invoke(node.changeRef, node.label, &checked, node.bindingRef);
}

std::expected<void, std::string> EditorSession::setFlag(const UiNode& node, std::size_t optionIndex, bool enabled) {
    if (optionIndex >= node.options.size()) return std::unexpected("no such option");
    const auto bit = asInteger(node.options[optionIndex].value);
    auto mask = static_cast<std::uint64_t>(asInteger(node.value));
    mask = enabled ? (mask | (1ull << bit)) : (mask & ~(1ull << bit));
    const UiValue newMask = static_cast<std::int64_t>(mask);
    return invoke(node.changeRef, node.label, &newMask, node.bindingRef);
}

std::expected<void, std::string> EditorSession::press(const UiNode& node) {
    if (!node.actionRef) return std::unexpected("nothing to run");
    return invoke(node.actionRef, node.label, nullptr);
}

std::expected<void, std::string> EditorSession::runAction(const UiNode& node, std::size_t actionIndex) {
    if (actionIndex >= node.actions.size()) return std::unexpected("no such action");
    const auto& action = node.actions[actionIndex];
    return invoke(action.functionRef, action.label, nullptr);
}

std::expected<void, std::string> EditorSession::undo() {
    if (!host_.document().undo()) return std::unexpected("nothing to undo");
    return rebuild();
}

std::expected<void, std::string> EditorSession::redo() {
    if (!host_.document().redo()) return std::unexpected("nothing to redo");
    return rebuild();
}

}

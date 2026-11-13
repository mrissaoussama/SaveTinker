#include <cmath>
#include <nlohmann/json.hpp>

#include "lua_support.hpp"
#include "savetinker/checksum.hpp"

namespace st::lua {

namespace {

char jsonNullSentinel;
constexpr int kMaxJsonDepth = 128;

int hashCrc32(lua_State* L) {
    const auto seed = static_cast<std::uint32_t>(luaL_optinteger(L, 2, 0));
    lua_pushinteger(L, crc32(asBytes(checkString(L, 1)), seed));
    return 1;
}

int hashCrc16(lua_State* L) {
    const auto initial = static_cast<std::uint16_t>(luaL_optinteger(L, 2, 0xFFFF));
    lua_pushinteger(L, crc16Ccitt(asBytes(checkString(L, 1)), initial));
    return 1;
}

int hashAdler32(lua_State* L) {
    lua_pushinteger(L, adler32(asBytes(checkString(L, 1))));
    return 1;
}

template <auto Digest>
int hashDigest(lua_State* L) {
    const auto digest = Digest(asBytes(checkString(L, 1)));
    pushBytes(L, ByteView(digest));
    return 1;
}

int hexEncode(lua_State* L) {
    const auto text = toHex(asBytes(checkString(L, 1)));
    lua_pushlstring(L, text.data(), text.size());
    return 1;
}

int hexDecode(lua_State* L) {
    pushBytes(L, fromHex(checkString(L, 1)));
    return 1;
}

bool isJsonNull(lua_State* L, int index) {
    return lua_islightuserdata(L, index) && lua_touserdata(L, index) == &jsonNullSentinel;
}

nlohmann::json toJson(lua_State* L, int index, int depth) {
    if (depth > kMaxJsonDepth) luaL_error(L, "value nested too deeply for JSON");
    index = lua_absindex(L, index);
    switch (lua_type(L, index)) {
    case LUA_TNIL: return nullptr;
    case LUA_TBOOLEAN: return lua_toboolean(L, index) != 0;
    case LUA_TNUMBER:
        if (lua_isinteger(L, index)) return lua_tointeger(L, index);
        if (!std::isfinite(lua_tonumber(L, index))) luaL_error(L, "cannot encode inf or nan as JSON");
        return lua_tonumber(L, index);
    case LUA_TSTRING: return std::string(checkString(L, index));
    case LUA_TLIGHTUSERDATA:
        if (isJsonNull(L, index)) return nullptr;
        break;
    case LUA_TTABLE: {
        const auto length = static_cast<lua_Integer>(lua_rawlen(L, index));
        lua_Integer keyCount = 0;
        bool onlyStringKeys = true;
        lua_pushnil(L);
        while (lua_next(L, index)) {
            ++keyCount;
            if (lua_type(L, -2) != LUA_TSTRING) onlyStringKeys = false;
            lua_pop(L, 1);
        }
        if (length > 0 && keyCount == length) {
            auto array = nlohmann::json::array();
            for (lua_Integer i = 1; i <= length; ++i) {
                lua_rawgeti(L, index, i);
                array.push_back(toJson(L, -1, depth + 1));
                lua_pop(L, 1);
            }
            return array;
        }
        if (!onlyStringKeys) luaL_error(L, "JSON objects need string keys; arrays need keys 1..n");
        auto object = nlohmann::json::object();
        lua_pushnil(L);
        while (lua_next(L, index)) {
            object[std::string(checkString(L, -2))] = toJson(L, -1, depth + 1);
            lua_pop(L, 1);
        }
        return object;
    }
    default: break;
    }
    luaL_error(L, "cannot encode %s as JSON", luaL_typename(L, index));
    return nullptr;
}

void pushJson(lua_State* L, const nlohmann::json& value) {
    luaL_checkstack(L, 4, "JSON nested too deeply");
    switch (value.type()) {
    case nlohmann::json::value_t::null: lua_pushlightuserdata(L, &jsonNullSentinel); break;
    case nlohmann::json::value_t::boolean: lua_pushboolean(L, value.get<bool>()); break;
    case nlohmann::json::value_t::number_integer: lua_pushinteger(L, value.get<std::int64_t>()); break;
    case nlohmann::json::value_t::number_unsigned: lua_pushinteger(L, static_cast<lua_Integer>(value.get<std::uint64_t>())); break;
    case nlohmann::json::value_t::number_float: lua_pushnumber(L, value.get<double>()); break;
    case nlohmann::json::value_t::string: {
        const auto& text = value.get_ref<const std::string&>();
        lua_pushlstring(L, text.data(), text.size());
        break;
    }
    case nlohmann::json::value_t::array: {
        lua_createtable(L, static_cast<int>(value.size()), 0);
        lua_Integer i = 1;
        for (const auto& item : value) {
            pushJson(L, item);
            lua_rawseti(L, -2, i++);
        }
        break;
    }
    case nlohmann::json::value_t::object: {
        lua_createtable(L, 0, static_cast<int>(value.size()));
        for (const auto& [key, item] : value.items()) {
            pushJson(L, item);
            lua_setfield(L, -2, key.c_str());
        }
        break;
    }
    default: lua_pushnil(L); break;
    }
}

int jsonDecode(lua_State* L) {
    const auto text = checkString(L, 1);
    const auto value = nlohmann::json::parse(text, nullptr, false);
    if (value.is_discarded()) return luaL_error(L, "invalid JSON");
    pushJson(L, value);
    return 1;
}

int jsonEncode(lua_State* L) {
    const bool pretty = lua_toboolean(L, 2) != 0;
    const auto text = toJson(L, 1, 0).dump(pretty ? 2 : -1);
    lua_pushlstring(L, text.data(), text.size());
    return 1;
}

}

void registerUtilities(lua_State* L, int stTableIndex) {
    stTableIndex = lua_absindex(L, stTableIndex);

    static const luaL_Reg hashFunctions[] = {
        {"crc32", guarded<hashCrc32>},
        {"crc16", guarded<hashCrc16>},
        {"adler32", guarded<hashAdler32>},
        {"md5", guarded<hashDigest<md5>>},
        {"sha1", guarded<hashDigest<sha1>>},
        {"sha256", guarded<hashDigest<sha256>>},
        {nullptr, nullptr},
    };
    luaL_newlib(L, hashFunctions);
    lua_setfield(L, stTableIndex, "hash");

    lua_pushcfunction(L, guarded<hexEncode>);
    lua_setfield(L, stTableIndex, "hex");
    lua_pushcfunction(L, guarded<hexDecode>);
    lua_setfield(L, stTableIndex, "unhex");

    static const luaL_Reg jsonFunctions[] = {
        {"decode", guarded<jsonDecode>},
        {"encode", guarded<jsonEncode>},
        {nullptr, nullptr},
    };
    luaL_newlib(L, jsonFunctions);
    lua_pushlightuserdata(L, &jsonNullSentinel);
    lua_setfield(L, -2, "null");
    lua_setfield(L, stTableIndex, "json");
}

}

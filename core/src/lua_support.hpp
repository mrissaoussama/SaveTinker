#pragma once

#include <climits>
#include <exception>
#include <string_view>

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#include "savetinker/byte_buffer.hpp"
#include "savetinker/document.hpp"

namespace st::lua {

// Turns C++ exceptions into Lua errors. Lua's own errors pass through untouched.
template <int (*Function)(lua_State*)>
int guarded(lua_State* L) {
    try {
        return Function(L);
    } catch (const std::exception& error) {
        return luaL_error(L, "%s", error.what());
    }
}

inline std::string_view checkString(lua_State* L, int index) {
    std::size_t length = 0;
    const char* data = luaL_checklstring(L, index, &length);
    return {data, length};
}

inline ByteView asBytes(std::string_view text) {
    return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
}

inline void pushBytes(lua_State* L, ByteView bytes) {
    lua_pushlstring(L, reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

inline std::size_t checkOffset(lua_State* L, int index) {
    const lua_Integer value = luaL_checkinteger(L, index);
    luaL_argcheck(L, value >= 0, index, "offset must not be negative");
    return static_cast<std::size_t>(value);
}

void pushSave(lua_State* L, Document& document);
void registerSaveTypes(lua_State* L);
void registerUtilities(lua_State* L, int stTableIndex);

}

#include "savetinker/script_host.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "lua_support.hpp"

namespace st {

namespace fs = std::filesystem;

namespace {

constexpr const char* kModuleCache = "SaveTinker.modules";
constexpr int kHookInterval = 1000;

bool isValidModuleName(std::string_view name) {
    if (name.empty() || name.front() == '.' || name.back() == '.' || name.find("..") != std::string_view::npos)
        return false;
    return std::ranges::all_of(name, [](unsigned char c) { return std::isalnum(c) || c == '_' || c == '.' || c == '-'; });
}

std::optional<std::string> readTextFile(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return std::nullopt;
    std::ostringstream content;
    content << stream.rdbuf();
    return content.str();
}

}

ModuleLoader directoryModuleLoader(fs::path editorDir, fs::path libraryDir) {
    return [editorDir = std::move(editorDir), libraryDir = std::move(libraryDir)](std::string_view name) -> std::optional<std::string> {
        if (!isValidModuleName(name)) return std::nullopt;
        std::string relative(name);
        std::ranges::replace(relative, '.', '/');
        relative += ".lua";
        for (const auto& root : {editorDir, libraryDir}) {
            if (root.empty()) continue;
            if (auto code = readTextFile(root / relative)) return code;
        }
        return std::nullopt;
    };
}

struct ScriptHost::Impl {
    Document& document;
    ScriptContext context;
    ModuleLoader loader;
    ScriptLimits limits;
    lua_State* L = nullptr;
    std::size_t memoryUsed = 0;
    std::uint64_t instructionsUsed = 0;
    std::vector<LogEntry> log;

    Impl(Document& doc, ScriptContext ctx, ModuleLoader load, ScriptLimits lim)
        : document(doc), context(std::move(ctx)), loader(std::move(load)), limits(lim) {}

    static Impl& from(lua_State* L) {
        void* userData = nullptr;
        lua_getallocf(L, &userData);
        return *static_cast<Impl*>(userData);
    }

    static void* allocate(void* userData, void* block, std::size_t oldSize, std::size_t newSize) {
        auto& self = *static_cast<Impl*>(userData);
        const std::size_t previous = block ? oldSize : 0;
        if (newSize == 0) {
            std::free(block);
            self.memoryUsed -= previous;
            return nullptr;
        }
        if (newSize > previous && self.memoryUsed + (newSize - previous) > self.limits.memoryBytes) return nullptr;
        void* resized = std::realloc(block, newSize);
        if (!resized) return nullptr;
        self.memoryUsed = self.memoryUsed - previous + newSize;
        return resized;
    }

    static void countInstructions(lua_State* L, lua_Debug*) {
        auto& self = from(L);
        self.instructionsUsed += kHookInterval;
        if (self.instructionsUsed > self.limits.instructionsPerCall)
            luaL_error(L, "script took too long (instruction limit reached)");
    }

    static int messageHandler(lua_State* L) {
        const char* message = lua_tostring(L, 1);
        if (!message) message = luaL_tolstring(L, 1, nullptr);
        luaL_traceback(L, L, message, 1);
        return 1;
    }

    static std::string joinArguments(lua_State* L) {
        std::string text;
        const int count = lua_gettop(L);
        for (int i = 1; i <= count; ++i) {
            if (i > 1) text += '\t';
            std::size_t length = 0;
            const char* piece = luaL_tolstring(L, i, &length);
            text.append(piece, length);
            lua_pop(L, 1);
        }
        return text;
    }

    static int print(lua_State* L) {
        from(L).log.push_back({LogEntry::Level::Info, joinArguments(L)});
        return 0;
    }

    static int warn(lua_State* L) {
        from(L).log.push_back({LogEntry::Level::Warning, joinArguments(L)});
        return 0;
    }

    static int require(lua_State* L) {
        const std::string name(lua::checkString(L, 1));
        lua_getfield(L, LUA_REGISTRYINDEX, kModuleCache);
        lua_getfield(L, -1, name.c_str());
        if (!lua_isnil(L, -1)) return 1;
        lua_pop(L, 1);

        if (!isValidModuleName(name)) return luaL_error(L, "invalid module name '%s'", name.c_str());
        const auto code = from(L).loader(name);
        if (!code) return luaL_error(L, "module '%s' not found", name.c_str());
        const std::string chunkName = "@" + name;
        if (luaL_loadbufferx(L, code->data(), code->size(), chunkName.c_str(), "t") != LUA_OK) return lua_error(L);
        lua_pushstring(L, name.c_str());
        lua_call(L, 1, 1);
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            lua_pushboolean(L, 1);
        }
        lua_pushvalue(L, -1);
        lua_setfield(L, -3, name.c_str());
        return 1;
    }

    static int transaction(lua_State* L) {
        const std::string label(lua::checkString(L, 1));
        luaL_checktype(L, 2, LUA_TFUNCTION);
        auto& document = from(L).document;
        document.beginTransaction(label);
        lua_pushvalue(L, 2);
        if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
            document.rollbackTransaction();
            return lua_error(L);
        }
        document.commitTransaction();
        return 0;
    }

    void setStringField(const char* key, const std::string& value) {
        lua_pushlstring(L, value.data(), value.size());
        lua_setfield(L, -2, key);
    }

    void openSandbox() {
        static const luaL_Reg libraries[] = {
            {LUA_GNAME, luaopen_base},       {LUA_TABLIBNAME, luaopen_table}, {LUA_STRLIBNAME, luaopen_string},
            {LUA_MATHLIBNAME, luaopen_math}, {LUA_UTF8LIBNAME, luaopen_utf8}, {LUA_COLIBNAME, luaopen_coroutine},
        };
        for (const auto& library : libraries) {
            luaL_requiref(L, library.name, library.func, 1);
            lua_pop(L, 1);
        }

        for (const char* removed : {"dofile", "loadfile", "load", "collectgarbage"}) {
            lua_pushnil(L);
            lua_setglobal(L, removed);
        }
        lua_getglobal(L, LUA_STRLIBNAME);
        lua_pushnil(L);
        lua_setfield(L, -2, "dump");
        lua_pop(L, 1);

        lua_register(L, "print", lua::guarded<print>);
        lua_register(L, "require", lua::guarded<require>);
        lua_newtable(L);
        lua_setfield(L, LUA_REGISTRYINDEX, kModuleCache);

        lua::registerSaveTypes(L);
        buildApiTable();
    }

    void buildApiTable() {
        lua_newtable(L);
        lua_pushinteger(L, kApiVersion);
        lua_setfield(L, -2, "api_version");

        lua_newtable(L);
        setStringField("id", context.platform);
        setStringField("name", platformDisplayName(context.platform));
        lua_setfield(L, -2, "platform");

        lua_newtable(L);
        setStringField("platform", context.platform);
        setStringField("title_id", context.titleId);
        setStringField("name", context.gameName);
        setStringField("version", context.gameVersion);
        lua_setfield(L, -2, "game");

        lua_newtable(L);
        setStringField("id", context.editor.id);
        setStringField("name", context.editor.name);
        setStringField("version", context.editor.version);
        lua_setfield(L, -2, "editor");

        lua::pushSave(L, document);
        lua_setfield(L, -2, "save");

        lua_pushcfunction(L, lua::guarded<warn>);
        lua_setfield(L, -2, "warn");
        lua_pushcfunction(L, lua::guarded<print>);
        lua_setfield(L, -2, "log");
        lua_pushcfunction(L, lua::guarded<transaction>);
        lua_setfield(L, -2, "transaction");

        lua::registerUtilities(L, -1);
        lua_setglobal(L, "st");
    }

    // Expects the function and its arguments on the stack.
    std::expected<void, std::string> protectedCall(int argumentCount, int resultCount) {
        instructionsUsed = 0;
        const int functionIndex = lua_gettop(L) - argumentCount;
        lua_pushcfunction(L, messageHandler);
        lua_insert(L, functionIndex);
        const int status = lua_pcall(L, argumentCount, resultCount, functionIndex);
        lua_remove(L, functionIndex);
        if (status == LUA_OK) return {};
        std::string message = lua_tostring(L, -1) ? lua_tostring(L, -1) : "unknown script error";
        if (status == LUA_ERRMEM) message = "script ran out of memory";
        lua_pop(L, 1);
        return std::unexpected(std::move(message));
    }

    std::expected<void, std::string> load(std::string_view code, const std::string& chunkName) {
        if (luaL_loadbufferx(L, code.data(), code.size(), chunkName.c_str(), "t") != LUA_OK) {
            std::string message = lua_tostring(L, -1);
            lua_pop(L, 1);
            return std::unexpected(std::move(message));
        }
        return {};
    }
};

ScriptHost::ScriptHost(Document& document, ScriptContext context, ModuleLoader loader, ScriptLimits limits)
    : impl_(std::make_unique<Impl>(document, std::move(context), std::move(loader), limits)) {
    impl_->L = lua_newstate(&Impl::allocate, impl_.get());
    if (!impl_->L) throw std::runtime_error("cannot create Lua state");
    lua_sethook(impl_->L, &Impl::countInstructions, LUA_MASKCOUNT, kHookInterval);
    impl_->openSandbox();
}

ScriptHost::~ScriptHost() {
    if (impl_ && impl_->L) lua_close(impl_->L);
}

std::expected<void, std::string> ScriptHost::run(std::string_view code, std::string_view chunkName) {
    if (auto loaded = impl_->load(code, "=" + std::string(chunkName)); !loaded) return loaded;
    return impl_->protectedCall(0, 0);
}

std::expected<std::string, std::string> ScriptHost::evaluate(std::string_view code) {
    lua_State* L = impl_->L;
    if (auto loaded = impl_->load(code, "=evaluate"); !loaded) return std::unexpected(loaded.error());
    if (auto called = impl_->protectedCall(0, 1); !called) return std::unexpected(called.error());
    std::size_t length = 0;
    const char* text = luaL_tolstring(L, -1, &length);
    std::string result(text, length);
    lua_pop(L, 2);
    return result;
}

std::expected<void, std::string> ScriptHost::loadEntry() {
    std::string moduleName = impl_->context.editor.entry;
    if (moduleName.ends_with(".lua")) moduleName.resize(moduleName.size() - 4);
    std::ranges::replace(moduleName, '/', '.');
    const auto code = impl_->loader(moduleName);
    if (!code) return std::unexpected("entry script '" + impl_->context.editor.entry + "' not found");
    if (auto loaded = impl_->load(*code, "@" + impl_->context.editor.entry); !loaded) return loaded;
    return impl_->protectedCall(0, 0);
}

bool ScriptHost::hasFunction(const char* name) {
    const bool found = lua_getglobal(impl_->L, name) == LUA_TFUNCTION;
    lua_pop(impl_->L, 1);
    return found;
}

std::expected<bool, std::string> ScriptHost::detect() {
    if (!hasFunction("detect")) return true;
    lua_State* L = impl_->L;
    lua_getglobal(L, "detect");
    lua::pushSave(L, impl_->document);
    if (auto called = impl_->protectedCall(1, 1); !called) return std::unexpected(called.error());
    const bool matched = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return matched;
}

std::expected<void, std::string> ScriptHost::beforeSave() {
    if (!hasFunction("before_save")) return {};
    lua_State* L = impl_->L;
    impl_->document.beginTransaction("Update checksums");
    lua_getglobal(L, "before_save");
    lua::pushSave(L, impl_->document);
    if (auto called = impl_->protectedCall(1, 0); !called) {
        impl_->document.rollbackTransaction();
        return called;
    }
    impl_->document.commitTransaction();
    return {};
}

const std::vector<LogEntry>& ScriptHost::log() const { return impl_->log; }
void ScriptHost::clearLog() { impl_->log.clear(); }
std::size_t ScriptHost::memoryUsed() const { return impl_->memoryUsed; }
lua_State* ScriptHost::state() { return impl_->L; }

}

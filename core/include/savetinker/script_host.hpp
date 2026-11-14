#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "savetinker/document.hpp"
#include "savetinker/game_data.hpp"
#include "savetinker/manifest.hpp"

struct lua_State;

namespace st {

struct ScriptContext {
    std::string platform;
    std::string titleId;
    std::string gameName;
    std::string gameVersion;
    EditorManifest editor;
    std::string language = "en";
};

// Returns the source of a module ("main", "lib.crc") or nothing if it does not exist.
using ModuleLoader = std::function<std::optional<std::string>(std::string_view moduleName)>;

// Looks in the editor folder first, then in the shared library folder.
ModuleLoader directoryModuleLoader(std::filesystem::path editorDir, std::filesystem::path libraryDir = {});

// Returns the bytes of a data file shipped with the editor ("labels/items.json").
using DataLoader = std::function<std::optional<std::string>(std::string_view relativePath)>;
DataLoader directoryDataLoader(std::filesystem::path editorDir);

struct ScriptLimits {
    std::size_t memoryBytes = 64u << 20;
    std::uint64_t instructionsPerCall = 200'000'000;
};

struct LogEntry {
    enum class Level { Info, Warning };
    Level level;
    std::string message;
};

class ScriptHost {
public:
    ScriptHost(Document& document, ScriptContext context, ModuleLoader loader, ScriptLimits limits = {});
    ~ScriptHost();
    ScriptHost(const ScriptHost&) = delete;
    ScriptHost& operator=(const ScriptHost&) = delete;

    std::expected<void, std::string> loadEntry();
    std::expected<void, std::string> run(std::string_view code, std::string_view chunkName = "chunk");
    // Runs code and returns the first result converted with tostring().
    std::expected<std::string, std::string> evaluate(std::string_view code);

    void setDataLoader(DataLoader loader);
    // Opened on the first st.game_data call, and only for editors whose manifest asks for game data.
    void setGameDataOpener(GameDataOpener opener);
    Document& document();

    // Calls the function below `argumentCount` arguments on the stack with the script limits applied.
    std::expected<void, std::string> call(int argumentCount, int resultCount);

    bool hasFunction(const char* name);
    std::expected<bool, std::string> detect();
    std::expected<void, std::string> beforeSave();

    const std::vector<LogEntry>& log() const;
    void clearLog();
    std::size_t memoryUsed() const;
    lua_State* state();

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

}

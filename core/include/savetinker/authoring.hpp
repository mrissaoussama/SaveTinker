#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <vector>

#include "savetinker/editor_session.hpp"
#include "savetinker/editor_store.hpp"
#include "savetinker/repository.hpp"

namespace st {

// Reads an editor folder the way it would be packed. Hidden files and folders are skipped.
std::expected<EditorPackage, std::string> readEditorDirectory(const std::filesystem::path& directory);

struct ValidationIssue {
    enum class Severity { Error, Warning };
    Severity severity;
    std::string file;
    std::string message;
};

// Checks Lua syntax, JSON data files and manifest details without running any code.
std::vector<ValidationIssue> validatePackage(const EditorPackage& package);

struct PackedEditor {
    RepoEditorEntry entry;
    std::string fileName;
    Bytes zip;
};

PackedEditor packEditor(const EditorPackage& package, std::string_view downloadFolder = "editors");

std::string manifestToJson(const EditorManifest& manifest);
// Download links are written as stored, so relative links stay relative.
std::string repoIndexToJson(const RepoIndex& index);

struct UiDumpOptions {
    bool openPages = true;
    int maxDepth = 8;
};

std::expected<std::string, std::string> dumpUi(EditorSession& session, const UiDumpOptions& options = {});

}

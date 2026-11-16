#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace app {

// The operating system's own open dialog. Nothing means none is available; an empty inner value means the user
// cancelled. It blocks until the dialog closes.
std::optional<std::optional<std::filesystem::path>> nativePickPath(const std::string& title, bool folder,
                                                                   const std::filesystem::path& start);

}

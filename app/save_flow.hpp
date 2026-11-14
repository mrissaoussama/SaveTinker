#pragma once

#include <expected>
#include <functional>
#include <string>

#include "app.hpp"
#include "savetinker/document.hpp"

namespace app {

struct SaveTarget {
    st::SaveInfo save;
    std::string titleName;
    bool backedUp = false;
};

std::expected<st::Document, std::string> loadSave(App& app, const st::SaveInfo& save);

// Backs up the save the first time it is written (if enabled), then writes and commits.
std::expected<void, std::string> writeSave(App& app, SaveTarget& target, const st::Document& document, const std::string& reason);

// Modal listing what changed. Calls onConfirm when the user accepts. Returns false once closed.
bool drawReviewChanges(App& app, const st::Document& document, const std::function<void()>& onConfirm);

}

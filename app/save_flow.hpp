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

std::expected<void, std::string> writeSave(App& app, SaveTarget& target, const st::Document& document, const std::string& reason);

bool drawReviewChanges(App& app, const st::Document& document, const std::function<void()>& onConfirm);

}

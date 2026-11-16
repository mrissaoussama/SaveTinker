#pragma once

#include <expected>
#include <functional>
#include <string>

#include "app.hpp"
#include "savetinker/document.hpp"
#include "savetinker/editor_session.hpp"

namespace app {

struct SaveTarget {
    st::SaveInfo save;
    std::string titleName;
    bool backedUp = false;
};

std::expected<st::Document, std::string> loadSave(App& app, const st::SaveInfo& save);

std::expected<void, std::string> writeSave(App& app, SaveTarget& target, const st::Document& document, const std::string& reason);

// `fields` lists the values edited on screen; the byte counts below them cover everything else.
bool drawReviewChanges(App& app, const st::Document& document, const std::function<void()>& onConfirm,
                       const std::vector<st::FieldChange>& fields = {});

}

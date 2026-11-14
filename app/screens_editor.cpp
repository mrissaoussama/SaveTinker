#include <algorithm>
#include <cmath>
#include <format>
#include <map>

#include "app.hpp"
#include "i18n.hpp"
#include "save_flow.hpp"
#include "savetinker/editor_session.hpp"
#include "ui.hpp"

namespace app {

namespace {

using st::UiNode;
using st::UiValue;
using st::WidgetKind;

std::string formatNumber(const UiNode& node) {
    if (auto integer = std::get_if<std::int64_t>(&node.value))
        return node.hex ? std::format("0x{:X}", *integer) : std::to_string(*integer);
    if (auto number = std::get_if<double>(&node.value)) return std::format("{:.6g}", *number);
    return st::uiValueToString(node.value);
}

std::optional<UiValue> parseNumber(const UiNode& node, const std::string& text) {
    if (text.empty()) return std::nullopt;
    try {
        if (node.kind == WidgetKind::Float) return std::stod(text);
        std::size_t used = 0;
        const bool hex = text.starts_with("0x") || text.starts_with("0X");
        const auto value = std::stoll(hex ? text.substr(2) : text, &used, hex ? 16 : 10);
        if (used != (hex ? text.size() - 2 : text.size())) return std::nullopt;
        return static_cast<std::int64_t>(value);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

class EditorScreen : public Screen {
public:
    EditorScreen(App& app, st::SaveInfo save, st::InstalledEditorInfo editor)
        : target_{std::move(save), {}}, editor_(std::move(editor)) {
        const auto* title = app.findTitle(target_.save.titleId);
        target_.titleName = title ? title->info.name : target_.save.titleId;
        load(app, title);
    }

    std::string title() const override { return editor_.manifest.name; }

    std::vector<Hint> hints(const App&) const override {
        if (!session_) return {{"B", tr("Back")}};
        std::vector<Hint> hints = {{"A", tr("Edit")}};
        if (focusedHasActions_) hints.push_back({"Y", tr("Actions")});
        if (!session_->topLevelLinks().empty()) hints.push_back({"L", tr("Page")}), hints.push_back({"R", tr("Page")});
        hints.push_back({"ZL", tr("Undo")});
        hints.push_back({"ZR", tr("Redo")});
        hints.push_back({"+", tr("Write")});
        hints.push_back({"B", tr("Back")});
        return hints;
    }

    bool requestClose(App& app) override {
        if (session_ && !session_->breadcrumbs().empty() && session_->breadcrumbs().size() > 1) {
            report(app, session_->back());
            return false;
        }
        if (document_ && document_->isDirty()) {
            app.confirm(tr("Discard changes?"), tr("Your edits have not been written to the save."), tr("Discard"), [](App& app) { app.pop(); });
            return false;
        }
        return true;
    }

    void draw(App& app) override {
        if (!error_.empty()) {
            ui::emptyState(tr("This editor could not open the save"), error_);
            return;
        }
        if (!session_) return;
        if (detectWarning_) {
            ui::wrappedText(detectReason_.empty() ? tr("This save does not look like what the editor expects. Changes may damage it.")
                                                  : detectReason_,
                            ui::color::Warning);
        }

        if (!app.modalOpen()) {
            handleShortcuts(app);
            if (sidebarHadFocus_ && ui::pressed(ui::Button::Right)) focusContent_ = true;
            if (fieldCouldLeave_ && ui::pressed(ui::Button::Left)) focusSidebar_ = true;
        }
        sidebarHadFocus_ = false;
        fieldCouldLeave_ = false;
        focusedHelp_.clear();
        focusedHasActions_ = false;

        const auto& links = session_->topLevelLinks();
        const bool sidebar = !links.empty();
        if (sidebar) {
            ImGui::BeginChild("##pages", ImVec2(ImGui::GetContentRegionAvail().x * 0.28f, 0), ImGuiChildFlags_NavFlattened);
            ui::holdFocusRequests(true);
            const std::string currentTop = session_->breadcrumbs().empty() ? "" : session_->breadcrumbs().front().id;
            bool sidebarFocused = false;
            // A page in the list opens it and moves into its fields; right does the same for the current page.
            auto pageRow = [&](const char* id, const std::string& label, const std::string& pageId) {
                const bool current = pageId == currentTop;
                if (current && focusSidebar_) {
                    ImGui::SetKeyboardFocusHere();
                    ImGui::SetNavCursorVisible(true);
                    ImGui::SetScrollHereY(0.5f);
                    focusSidebar_ = false;
                }
                const auto result = ui::row(id, label, "", ui::color::TextDim, current);
                sidebarFocused = sidebarFocused || result.focused;
                if (result.activated) {
                    pending_ = [this, &app, pageId, current] {
                        if (!current) report(app, session_->openTopLevel(pageId));
                        pageChanged_ = !current;
                        focusContent_ = true;
                    };
                }
                if (current && pageChanged_) ImGui::SetScrollHereY(0.5f);
            };
            if (hasRootContent()) pageRow("##root", session_->root().title, "");
            for (const auto& link : links) pageRow(link.pageId.c_str(), link.label, link.pageId);
            sidebarHadFocus_ = sidebarFocused;
            ui::holdFocusRequests(false);
            ImGui::EndChild();
            ImGui::SameLine();
        }

        ImGui::BeginChild("##fields", ImVec2(0, 0), ImGuiChildFlags_NavFlattened);
        const auto& crumbs = session_->breadcrumbs();
        if (crumbs.size() > 1) {
            std::string path;
            for (const auto& crumb : crumbs) path += (path.empty() ? "" : "  \xE2\x80\xBA  ") + crumb.title;
            ui::dimText(path);
        }
        if (pageChanged_) {
            ImGui::SetScrollY(0);
            pageChanged_ = false;
            ui::focusNextRow();
        } else if (focusContent_) {
            const auto remembered = lastFieldFocus_.find(session_->currentPage().id);
            if (remembered != lastFieldFocus_.end()) ui::restoreFocus(remembered->second);
            else ui::focusNextRow();
        }
        focusContent_ = false;
        canLeaveFields_ = sidebar;
        const auto& page = session_->currentPage();
        drawNodes(app, page.nodes, sidebar && crumbs.size() <= 1 && page.id.empty());
        if (page.nodes.empty()) ui::dimText(tr("Nothing here."));
        ImGui::EndChild();

        drawStatusLine(app);
        if (reviewing_ && !drawReviewChanges(app, *document_, [this, &app] { write(app); })) reviewing_ = false;
        drawLog(app);

        if (pending_) {
            auto action = std::move(pending_);
            pending_ = nullptr;
            action();
        }
    }

private:
    void load(App& app, const TitleEntry* title) {
        auto document = loadSave(app, target_.save);
        if (!document) {
            error_ = document.error();
            return;
        }
        document_ = std::make_unique<st::Document>(std::move(*document));
        st::ScriptContext context;
        context.platform = title ? title->info.platform : app.platform().gamePlatform();
        context.titleId = target_.save.titleId;
        context.gameName = target_.titleName;
        context.gameVersion = title ? title->info.displayVersion : "";
        if (title) context.addOns = title->info.addOns;
        context.saveKind = std::string(st::saveKindName(target_.save.kind));
        context.editor = editor_.manifest;
        context.language = app.language();
        try {
            host_ = std::make_unique<st::ScriptHost>(*document_, context,
                                                     st::directoryModuleLoader(editor_.directory, app.editors().libraryDir()));
        } catch (const std::exception& failure) {
            error_ = failure.what();
            return;
        }
        host_->setDataLoader(st::directoryDataLoader(editor_.directory));
        host_->setGameDataOpener([&platform = app.platform(), titleId = target_.save.titleId] {
            return platform.openGameData(titleId);
        });
        if (auto loaded = host_->loadEntry(); !loaded) {
            error_ = loaded.error();
            return;
        }
        auto detected = host_->detect();
        if (!detected) {
            error_ = detected.error();
            return;
        }
        detectWarning_ = !*detected;
        detectReason_ = host_->detectReason();
        session_ = std::make_unique<st::EditorSession>(*host_);
        if (auto built = session_->rebuild(); !built) {
            error_ = built.error();
            session_.reset();
        }
    }

    bool hasRootContent() const {
        return std::ranges::any_of(session_->root().nodes, [](const UiNode& n) { return n.kind != WidgetKind::PageLink; });
    }

    void handleShortcuts(App& app) {
        if (ui::pressed(ui::Button::ZL, true)) report(app, session_->undo(), true);
        if (ui::pressed(ui::Button::ZR, true)) report(app, session_->redo(), true);
        if (ui::pressed(ui::Button::Plus)) reviewing_ = true;
        if (ui::pressed(ui::Button::Minus)) showLog_ = true;
        const auto& links = session_->topLevelLinks();
        if (!links.empty() && (ui::pressed(ui::Button::L) || ui::pressed(ui::Button::R))) {
            std::vector<std::string> ids;
            if (hasRootContent()) ids.push_back("");
            for (const auto& link : links) ids.push_back(link.pageId);
            const std::string current = session_->breadcrumbs().empty() ? "" : session_->breadcrumbs().front().id;
            auto position = std::ranges::find(ids, current);
            std::size_t index = position == ids.end() ? 0 : static_cast<std::size_t>(position - ids.begin());
            index = ui::pressed(ui::Button::R) ? (index + 1) % ids.size() : (index + ids.size() - 1) % ids.size();
            const auto id = ids[index];
            pending_ = [this, &app, id] {
                report(app, session_->openTopLevel(id));
                pageChanged_ = true;
            };
        }
    }

    void report(App& app, const std::expected<void, std::string>& result, bool quiet = false) {
        if (!result && !quiet) app.showMessage(tr("Editor error"), result.error());
        else if (!result) app.toast(result.error(), true);
    }

    void edit(App& app, const UiNode& node, UiValue value) {
        pending_ = [this, &app, node, value] { report(app, session_->setValue(node, value)); };
    }

    void drawNodes(App& app, const std::vector<UiNode>& nodes, bool skipLinks) {
        for (const auto& node : nodes) {
            if (skipLinks && node.kind == WidgetKind::PageLink) continue;
            ImGui::PushID(node.id.c_str());
            drawNode(app, node);
            ImGui::PopID();
        }
    }

    void trackFocus(const UiNode& node, const ui::RowResult& result) {
        if (!result.focused) return;
        if (result.id) lastFieldFocus_[session_->currentPage().id] = result.id;
        // Left on a field that does not use left/right goes back to the page list.
        const bool usesLeftRight = (node.kind == WidgetKind::Integer || node.kind == WidgetKind::Float ||
                                    node.kind == WidgetKind::Segmented) && node.editable();
        fieldCouldLeave_ = canLeaveFields_ && !usesLeftRight;
        focusedHelp_ = node.help;
        focusedHasActions_ = !node.actions.empty();
        if (focusedHasActions_ && ui::pressed(ui::Button::Y)) {
            actionNode_ = node;
            ImGui::OpenPopup("##actions");
        }
    }

    std::string actionChips(const UiNode& node) {
        std::string chips;
        for (const auto& action : node.actions) chips += "[" + action.label + "] ";
        return chips;
    }

    void drawNode(App& app, const UiNode& node) {
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        const std::string label = node.label + (node.actions.empty() ? "" : "   " + actionChips(node));
        switch (node.kind) {
        case WidgetKind::Integer:
        case WidgetKind::Float: {
            const bool editable = node.editable();
            const auto result = ui::row("##n", label, editable ? ui::stepper(formatNumber(node)) : formatNumber(node),
                                        editable ? ui::color::Text : ui::color::TextDim);
            trackFocus(node, result);
            if (!editable) break;
            if (const int step = ui::horizontalInput(result.id, result.focused)) {
                const double direction = step;
                const double current = std::holds_alternative<std::int64_t>(node.value) ? static_cast<double>(std::get<std::int64_t>(node.value))
                                       : std::holds_alternative<double>(node.value)   ? std::get<double>(node.value)
                                                                                      : 0.0;
                double next = current + direction * node.step;
                if (node.min) next = std::max(next, *node.min);
                if (node.max) next = std::min(next, *node.max);
                if (next != current)
                    edit(app, node, node.kind == WidgetKind::Integer ? UiValue(static_cast<std::int64_t>(std::llround(next))) : UiValue(next));
            }
            if (result.activated) {
                std::string range;
                if (node.min && node.max) range = trf(" ({} to {})", *node.min, *node.max);
                app.promptText({node.label + range, formatNumber(node), node.hex ? TextKind::Hex : TextKind::Number, 32},
                               [this, node](App& app, std::string text) {
                                   if (node.hex && !text.starts_with("0x")) text = "0x" + text;
                                   auto value = parseNumber(node, text);
                                   if (!value) return app.toast(tr("Not a valid number"), true);
                                   edit(app, node, *value);
                               });
            }
            break;
        }
        case WidgetKind::Text: {
            const auto text = st::uiValueToString(node.value);
            const auto result = ui::row("##t", label, text.empty() ? "(empty)" : text);
            trackFocus(node, result);
            if (result.activated && node.editable())
                app.promptText({node.label, text, TextKind::Text, node.maxLength},
                               [this, node](App& app, std::string value) { edit(app, node, value); });
            break;
        }
        case WidgetKind::Toggle: {
            const auto result = ui::row("##g", label);
            ui::toggleSwitch(ImVec2(start.x + width - ImGui::GetFontSize() * 0.6f, start.y), ui::rowHeight(), node.toggleState());
            trackFocus(node, result);
            if (result.activated && node.editable()) edit(app, node, node.toggleState() ? node.offValue : node.onValue);
            break;
        }
        case WidgetKind::Choice: {
            const auto selected = node.selectedOption();
            const std::string value = selected ? node.options[*selected].label : trf("Unknown ({})", st::uiValueToString(node.value));
            const auto result = ui::row("##c", label, value + "  \xE2\x96\xBE", selected ? ui::color::Text : ui::color::Warning);
            trackFocus(node, result);
            if (result.activated && node.editable()) {
                choiceNode_ = node;
                choiceFilter_.clear();
                ImGui::OpenPopup("##choice");
            }
            break;
        }
        case WidgetKind::Segmented: drawSegmented(app, node, start, width); break;
        case WidgetKind::Flags: {
            std::size_t count = 0;
            for (std::size_t i = 0; i < node.options.size(); ++i) count += node.flagSet(i);
            const auto result = ui::row("##f", label, std::format("{} of {}  \xE2\x96\xBE", count, node.options.size()));
            trackFocus(node, result);
            if (result.activated && node.editable()) {
                choiceNode_ = node;
                ImGui::OpenPopup("##flags");
            }
            break;
        }
        case WidgetKind::Button: {
            const auto result = ui::row("##b", label, "", ui::color::Accent);
            ImGui::GetWindowDrawList()->AddText(ImVec2(start.x + width - ImGui::GetFontSize() * 1.5f, start.y + (ui::rowHeight() - ImGui::GetFontSize()) * 0.5f),
                                                ui::color::Accent, "\xE2\x96\xB6");
            trackFocus(node, result);
            if (result.activated) pending_ = [this, &app, node] { report(app, session_->press(node)); };
            break;
        }
        case WidgetKind::Label: {
            const auto result = ui::row("##l", label, node.display);
            trackFocus(node, result);
            break;
        }
        case WidgetKind::Note: ui::dimText(node.label); break;
        case WidgetKind::Warning: ui::wrappedText(node.label, ui::color::Warning); break;
        case WidgetKind::Separator: ImGui::Separator(); break;
        case WidgetKind::PageLink: {
            const auto result = ui::row("##p", label, "\xE2\x80\xBA", ui::color::Text);
            trackFocus(node, result);
            if (result.activated) {
                const auto id = node.pageId;
                pending_ = [this, &app, id] {
                    report(app, session_->openPage(id));
                    pageChanged_ = true;
                };
            }
            break;
        }
        case WidgetKind::Section: {
            const bool open = ImGui::CollapsingHeader(label.c_str(), node.collapsed ? 0 : ImGuiTreeNodeFlags_DefaultOpen);
            ui::RowResult header;
            header.focused = ImGui::IsItemFocused();
            trackFocus(node, header);
            if (open) {
                ImGui::Indent(ImGui::GetFontSize() * 0.6f);
                drawNodes(app, node.children, false);
                ImGui::Unindent(ImGui::GetFontSize() * 0.6f);
            }
            break;
        }
        case WidgetKind::Popup: {
            const auto result = ui::row("##o", label, "\xE2\x80\xA6");
            trackFocus(node, result);
            if (result.activated) ImGui::OpenPopup("##nodepopup");
            ImGui::SetNextWindowPos(ImGui::GetIO().DisplaySize * 0.5f, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x * 0.6f, ImGui::GetIO().DisplaySize.y * 0.7f));
            if (ImGui::BeginPopupModal("##nodepopup", nullptr, ImGuiWindowFlags_NoTitleBar)) {
                ui::sectionTitle(node.label);
                ImGui::BeginChild("##popupbody", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), ImGuiChildFlags_NavFlattened);
                drawNodes(app, node.children, false);
                ImGui::EndChild();
                if (ImGui::Button(trc("Close")) || ui::pressed(ui::Button::B)) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            break;
        }
        }

        if (!node.actions.empty() && actionNode_ && actionNode_->id == node.id) drawActionsPopup(app);
        if (choiceNode_ && choiceNode_->id == node.id) {
            drawChoicePopup(app);
            drawFlagsPopup(app);
        }
    }

    void drawSegmented(App& app, const UiNode& node, ImVec2 start, float width) {
        const std::string label = node.label + (node.actions.empty() ? "" : "   " + actionChips(node));
        const auto result = ui::row("##s", label);
        trackFocus(node, result);
        const int step = node.editable() ? ui::horizontalInput(result.id, result.focused) : 0;
        const auto selected = node.selectedOption();
        auto* draw = ImGui::GetWindowDrawList();
        const float pad = ImGui::GetFontSize() * 0.5f;
        float x = start.x + width - pad;
        const float height = ImGui::GetFontSize() * 1.5f;
        const float y = start.y + (ui::rowHeight() - height) * 0.5f;
        for (std::size_t i = node.options.size(); i-- > 0;) {
            const auto& text = node.options[i].label;
            const float segment = ImGui::CalcTextSize(text.c_str()).x + pad * 2;
            x -= segment;
            const bool active = selected && *selected == i;
            draw->AddRectFilled(ImVec2(x, y), ImVec2(x + segment - 2, y + height), active ? ui::color::Accent : IM_COL32(60, 66, 80, 255), 5);
            draw->AddText(ImVec2(x + pad, y + (height - ImGui::GetFontSize()) * 0.5f), active ? IM_COL32(15, 20, 25, 255) : ui::color::Text,
                          text.c_str());
        }
        if (!node.editable() || node.options.empty()) return;
        const std::size_t count = node.options.size();
        const std::size_t current = selected.value_or(0);
        std::optional<std::size_t> next;
        if (result.activated) next = (current + 1) % count;
        if (step > 0) next = std::min(current + 1, count - 1);
        if (step < 0) next = current == 0 ? 0 : current - 1;
        if (next && (*next != current || !selected)) edit(app, node, node.options[*next].value);
    }

    void drawChoicePopup(App& app) {
        ImGui::SetNextWindowPos(ImGui::GetIO().DisplaySize * 0.5f, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 22, ImGui::GetIO().DisplaySize.y * 0.75f));
        if (!ImGui::BeginPopup("##choice")) return;
        const auto& node = *choiceNode_;
        ui::sectionTitle(node.label);
        if (node.options.size() > 12) {
            if (ui::row("##filter", choiceFilter_.empty() ? tr("Search...") : trf("Search: {}", choiceFilter_), "", ui::color::Accent).activated ||
                ui::pressed(ui::Button::X))
                app.promptText({trf("Search {}", node.label), choiceFilter_, TextKind::Text, 64},
                               [this](App&, std::string text) { choiceFilter_ = std::move(text); });
        }
        ImGui::BeginChild("##options", ImVec2(0, 0), ImGuiChildFlags_NavFlattened);
        const auto selected = node.selectedOption();
        auto lower = [](std::string text) {
            std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return text;
        };
        const auto filter = lower(choiceFilter_);
        for (std::size_t i = 0; i < node.options.size(); ++i) {
            const auto& option = node.options[i];
            if (!filter.empty() && lower(option.label).find(filter) == std::string::npos) continue;
            ImGui::PushID(static_cast<int>(i));
            const bool isSelected = selected && *selected == i;
            const auto result = ui::row("##opt", option.label, st::uiValueToString(option.value), ui::color::TextDim, isSelected);
            if (isSelected && ImGui::IsWindowAppearing()) ImGui::SetItemDefaultFocus(), ImGui::SetScrollHereY(0.5f);
            if (result.activated) {
                edit(app, node, option.value);
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }

    void drawFlagsPopup(App& app) {
        ImGui::SetNextWindowPos(ImGui::GetIO().DisplaySize * 0.5f, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 22, 0));
        if (!ImGui::BeginPopup("##flags")) return;
        const auto& node = *choiceNode_;
        ui::sectionTitle(node.label);
        for (std::size_t i = 0; i < node.options.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            const ImVec2 start = ImGui::GetCursorScreenPos();
            const float width = ImGui::GetContentRegionAvail().x;
            const bool on = node.flagSet(i);
            if (ui::row("##flag", node.options[i].label).activated)
                pending_ = [this, &app, node, i, on] {
                    report(app, session_->setFlag(node, i, !on));
                    refreshChoiceNode();
                };
            ui::toggleSwitch(ImVec2(start.x + width - ImGui::GetFontSize() * 0.6f, start.y), ui::rowHeight(), on);
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }

    void drawActionsPopup(App& app) {
        if (!ImGui::BeginPopup("##actions")) return;
        const auto node = *actionNode_;
        ui::sectionTitle(node.label);
        for (std::size_t i = 0; i < node.actions.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            if (ui::row("##action", node.actions[i].label).activated) {
                pending_ = [this, &app, node, i] { report(app, session_->runAction(node, i)); };
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }

    // After a rebuild the copied node is stale; find the fresh one by id.
    void refreshChoiceNode() {
        if (!choiceNode_) return;
        std::function<const UiNode*(const std::vector<UiNode>&)> find = [&](const std::vector<UiNode>& nodes) -> const UiNode* {
            for (const auto& node : nodes) {
                if (node.id == choiceNode_->id) return &node;
                if (auto* child = find(node.children)) return child;
            }
            return nullptr;
        };
        if (auto* fresh = find(session_->currentPage().nodes)) choiceNode_ = *fresh;
    }

    void drawStatusLine(App& app) {
        if (!focusedHelp_.empty())
            ImGui::GetForegroundDrawList()->AddText(app.footerTextPosition(), ui::color::TextDim, focusedHelp_.c_str());
        if (document_ && document_->isDirty()) {
            auto* draw = ImGui::GetForegroundDrawList();
            const char* text = trc("\xE2\x97\x8F Unsaved");
            const float width = ImGui::CalcTextSize(text).x;
            draw->AddText(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f - width * 0.5f, ImGui::GetFontSize() * 0.5f), ui::color::Modified, text);
        }
    }

    void drawLog(App&) {
        if (showLog_) {
            ImGui::OpenPopup("##log");
            showLog_ = false;
        }
        ImGui::SetNextWindowPos(ImGui::GetIO().DisplaySize * 0.5f, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x * 0.7f, ImGui::GetIO().DisplaySize.y * 0.6f));
        if (!ImGui::BeginPopup("##log")) return;
        ui::sectionTitle(tr("Editor log"));
        if (host_->log().empty()) ui::dimText(tr("The editor has not printed anything."));
        for (const auto& entry : host_->log())
            ui::wrappedText(entry.message, entry.level == st::LogEntry::Level::Warning ? ui::color::Warning : ui::color::Text);
        ImGui::EndPopup();
    }

    void write(App& app) {
        if (auto prepared = host_->beforeSave(); !prepared) {
            app.showMessage(tr("The editor could not prepare the save"), prepared.error());
            return;
        }
        auto written = writeSave(app, target_, *document_, trf("before edit with {}", editor_.manifest.name));
        if (!written) {
            app.showMessage(tr("Save not written"), written.error());
            return;
        }
        document_->markSaved();
        app.reloadTitles();
        report(app, session_->rebuild());
        app.toast(tr("Save written"));
    }

    SaveTarget target_;
    st::InstalledEditorInfo editor_;
    std::unique_ptr<st::Document> document_;
    std::unique_ptr<st::ScriptHost> host_;
    std::unique_ptr<st::EditorSession> session_;
    std::string error_;
    bool detectWarning_ = false;
    std::string detectReason_;
    bool reviewing_ = false;
    bool showLog_ = false;
    bool pageChanged_ = true;
    std::string focusedHelp_;
    bool focusedHasActions_ = false;
    bool focusSidebar_ = false;
    bool focusContent_ = false;
    bool canLeaveFields_ = false;
    bool sidebarHadFocus_ = false;
    bool fieldCouldLeave_ = false;
    std::map<std::string, ImGuiID> lastFieldFocus_;
    std::optional<UiNode> choiceNode_;
    std::optional<UiNode> actionNode_;
    std::string choiceFilter_;
    std::function<void()> pending_;
};

}

std::unique_ptr<Screen> makeEditorScreen(App& app, const st::SaveInfo& save, const st::InstalledEditorInfo& editor) {
    return std::make_unique<EditorScreen>(app, save, editor);
}

}

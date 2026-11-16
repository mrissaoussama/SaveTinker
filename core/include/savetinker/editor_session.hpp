#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

#include "savetinker/byte_buffer.hpp"
#include "savetinker/script_host.hpp"

namespace st {

using UiValue = std::variant<std::monostate, bool, std::int64_t, double, std::string>;

std::string uiValueToString(const UiValue& value);

enum class WidgetKind {
    PageLink,
    Section,
    Popup,
    Integer,
    Float,
    Text,
    Toggle,
    Choice,
    Segmented,
    Flags,
    Button,
    Label,
    Note,
    Warning,
    Separator,
    Image,
};

struct UiOption {
    UiValue value;
    std::string label;
};

struct UiAction {
    std::string label;
    int functionRef = 0;
};

struct UiNode {
    WidgetKind kind = WidgetKind::Label;
    std::string id;
    std::string label;
    std::string help;
    UiValue value;
    std::string display;
    std::vector<UiOption> options;
    std::vector<UiAction> actions;
    std::vector<UiNode> children;
    std::optional<double> min;
    std::optional<double> max;
    double step = 1;
    bool hex = false;
    bool readOnly = false;
    bool collapsed = false;
    UiValue onValue = true;
    UiValue offValue = false;
    std::size_t maxLength = 0;
    Bytes image;
    std::string pageId;
    int bindingRef = 0;
    int actionRef = 0;
    int changeRef = 0;

    bool editable() const { return !readOnly && bindingRef != 0; }
    std::optional<std::size_t> selectedOption() const;
    bool flagSet(std::size_t optionIndex) const;
    bool toggleState() const;
};

struct UiPage {
    std::string id;
    std::string title;
    std::vector<UiNode> nodes;
};

// One value edited through the editor screen, compared with what the save held when it was opened.
struct FieldChange {
    std::string id;
    std::string label;
    std::string where;
    std::string before;
    std::string after;
};

// The text a widget shows for a value: option labels for lists, On/Off for switches, hex when asked.
std::string describeValue(const UiNode& node, const UiValue& value);

struct Breadcrumb {
    std::string id;
    std::string title;
};

class EditorSession {
public:
    explicit EditorSession(ScriptHost& host);
    ~EditorSession();
    EditorSession(const EditorSession&) = delete;
    EditorSession& operator=(const EditorSession&) = delete;

    bool hasUi();
    std::expected<void, std::string> rebuild();

    const std::vector<UiNode>& topLevelLinks() const { return topLinks_; }
    const UiPage& root() const { return root_; }
    const UiPage& currentPage() const;
    const std::vector<Breadcrumb>& breadcrumbs() const { return breadcrumbs_; }

    std::expected<void, std::string> openPage(const std::string& pageId);
    std::expected<void, std::string> openTopLevel(const std::string& pageId);
    std::expected<void, std::string> back();

    std::expected<void, std::string> setValue(const UiNode& node, const UiValue& value);
    std::expected<void, std::string> setFlag(const UiNode& node, std::size_t optionIndex, bool enabled);
    std::expected<void, std::string> press(const UiNode& node);
    std::expected<void, std::string> runAction(const UiNode& node, std::size_t actionIndex);

    std::expected<void, std::string> undo();
    std::expected<void, std::string> redo();

    // The value the widget's binding gave when the save was loaded.
    std::expected<UiValue, std::string> originalValue(const UiNode& node);
    std::expected<void, std::string> reset(const UiNode& node);
    // Edited fields whose value now differs from the loaded save, in the order they were first edited.
    std::vector<FieldChange> fieldChanges();
    bool changed(const UiNode& node) const { return changedIds_.contains(node.id); }

    struct BuildContext;

private:
    std::expected<void, std::string> invoke(int functionRef, const std::string& label, const UiValue* argument,
                                            int bindingRef = 0);
    std::expected<UiValue, std::string> readBinding(int table, int ref);
    void track(const UiNode& node);
    void refreshChanged();

    struct Tracked {
        UiNode node;
        std::string where;
        UiValue before;
        int bindingRef = 0;
    };
    const Tracked* findTracked(const UiNode& node);
    std::vector<Tracked> tracked_;
    std::set<std::string> changedIds_;
    int trackedTable_ = 0;

    ScriptHost& host_;
    std::shared_ptr<BuildContext> context_;
    UiPage root_;
    std::vector<UiNode> topLinks_;
    std::vector<UiPage> stack_;
    std::vector<Breadcrumb> breadcrumbs_;
    std::vector<std::string> activePath_;
    int refsTable_ = 0;
    int stateTable_ = 0;
};

}

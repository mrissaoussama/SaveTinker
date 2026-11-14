#include <algorithm>
#include <format>
#include <fstream>
#include <map>
#include <span>

#include "app.hpp"
#include "i18n.hpp"
#include "savetinker/authoring.hpp"
#include "savetinker/checksum.hpp"
#include "ui.hpp"

namespace app {

namespace {

std::map<std::string, st::EditorTemplate> drafts;

std::filesystem::path draftPath(App& app, const std::string& titleId) {
    return app.platform().dataRoot() / "drafts" / (titleId + ".json");
}

st::EditorTemplate newDraft(App& app, const std::string& titleId) {
    const auto* title = app.findTitle(titleId);
    const std::string gameName = title && !title->info.name.empty() ? title->info.name : titleId;
    std::string id = "custom." + titleId;
    std::ranges::transform(id, id.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    st::EditorTemplate draft;
    draft.manifest.id = id;
    draft.manifest.name = gameName + " (custom)";
    draft.manifest.version = "0.1.0";
    draft.manifest.games.push_back({title ? title->info.platform : app.platform().gamePlatform(), titleId,
                                    title ? title->info.name : "", {}});
    return draft;
}

std::string valueLabel(const std::string& text) {
    return ui::stepper(text);
}

bool isNumeric(const std::string& type) {
    return type != "bit" && type != "utf8" && type != "utf16";
}

std::string describeField(const st::TemplateField& field) {
    std::string text = std::format("{} @ 0x{:X}", field.type, field.offset);
    if (field.type == "bit") text += trf(" bit {}", field.bit);
    if (field.type == "utf8" || field.type == "utf16") text += trf(" ({} chars)", field.length);
    if (field.bigEndian && field.type != "bit" && field.type != "utf8") text += " BE";
    return text;
}

std::string previewValue(const st::TemplateField& field, const st::Bytes& content) {
    const st::ByteBuffer buffer(content);
    if (field.offset + st::templateFieldSize(field) > buffer.size()) return tr("past the end of the file");
    const auto endian = field.bigEndian ? st::Endian::Big : st::Endian::Little;
    const auto offset = static_cast<std::size_t>(field.offset);
    const auto& type = field.type;
    try {
        if (type == "bit") return ((buffer.bytes()[offset] >> field.bit) & 1) ? "on" : "off";
        if (type == "utf8") return "\"" + buffer.readUtf8(offset, field.length) + "\"";
        if (type == "utf16") return "\"" + buffer.readUtf16(offset, field.length, endian) + "\"";
        if (type == "f32") return std::format("{:.6g}", buffer.readFloat(offset, endian));
        if (type == "f64") return std::format("{:.6g}", buffer.readDouble(offset, endian));
        std::int64_t value = 0;
        if (type == "u8") value = buffer.read<std::uint8_t>(offset, endian);
        else if (type == "s8") value = buffer.read<std::int8_t>(offset, endian);
        else if (type == "u16") value = buffer.read<std::uint16_t>(offset, endian);
        else if (type == "s16") value = buffer.read<std::int16_t>(offset, endian);
        else if (type == "u32") value = buffer.read<std::uint32_t>(offset, endian);
        else if (type == "s32") value = buffer.read<std::int32_t>(offset, endian);
        else if (type == "u64") return std::to_string(buffer.read<std::uint64_t>(offset, endian));
        else if (type == "s64") value = buffer.read<std::int64_t>(offset, endian);
        return field.hex ? std::format("0x{:X}", value) : std::to_string(value);
    } catch (const std::exception& error) {
        return error.what();
    }
}

void drawToggleRow(const char* id, const std::string& label, bool& value) {
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    if (ui::row(id, label).activated) value = !value;
    ui::toggleSwitch(ImVec2(start.x + width - ImGui::GetFontSize() * 0.6f, start.y), ui::rowHeight(), value);
}

class FieldScreen : public Screen {
public:
    FieldScreen(std::string titleId, st::TemplateField field, std::optional<std::size_t> index, st::Bytes content)
        : titleId_(std::move(titleId)), field_(std::move(field)), index_(index), content_(std::move(content)) {}

    std::string title() const override { return index_ ? tr("Edit field") : tr("New field"); }

    std::vector<Hint> hints(const App&) const override { return {{"A", tr("Select")}, {"B", tr("Cancel")}}; }

    void draw(App& app) override {
        ui::dimText(trf("{} at offset 0x{:X}", field_.file, field_.offset));

        ui::sectionTitle(tr("Field"));
        if (ui::row("##label", tr("Label"), field_.label.empty() ? tr("Not set") : field_.label,
                    field_.label.empty() ? ui::color::Warning : ui::color::TextDim)
                .activated)
            app.promptText({tr("Field label"), field_.label, TextKind::Text, 64}, [this](App&, std::string text) { field_.label = text; });
        if (ui::row("##page", tr("Page"), field_.page.empty() ? tr("General") : field_.page).activated)
            app.promptText({tr("Page name"), field_.page, TextKind::Text, 64}, [this](App&, std::string text) { field_.page = text; });

        {
            const auto types = std::span(st::kTemplateTypes);
            auto current = std::ranges::find(types, field_.type);
            std::size_t index = current == types.end() ? 0 : static_cast<std::size_t>(current - types.begin());
            const auto result = ui::row("##type", tr("Type"), valueLabel(std::string(types[index])));
            const int step = ui::horizontalInput(result.id, result.focused) + (result.activated ? 1 : 0);
            if (step) {
                const int count = static_cast<int>(types.size());
                index = static_cast<std::size_t>(((static_cast<int>(index) + step) % count + count) % count);
                field_.type = types[index];
                if (!isNumeric(field_.type) && field_.type != "bit" && field_.length == 0) field_.length = 16;
            }
        }
        if (field_.type == "bit") {
            const auto result = ui::row("##bit", tr("Bit"), valueLabel(std::to_string(field_.bit)));
            const int step = ui::horizontalInput(result.id, result.focused) + (result.activated ? 1 : 0);
            if (step) field_.bit = (field_.bit + step + 8) % 8;
        }
        if (field_.type == "utf8" || field_.type == "utf16") {
            if (ui::row("##length", tr("Length in characters"), std::to_string(field_.length)).activated)
                app.promptText({tr("Length in characters"), std::to_string(field_.length), TextKind::Number, 5}, [this](App& app, std::string text) {
                    try {
                        field_.length = std::stoul(text);
                    } catch (const std::exception&) {
                        app.toast(tr("Enter a number"), true);
                    }
                });
        }
        if (field_.type != "bit" && field_.type != "utf8" && field_.type != "u8" && field_.type != "s8")
            drawToggleRow("##endian", tr("Big endian"), field_.bigEndian);
        if (isNumeric(field_.type) && field_.type[0] != 'f') drawToggleRow("##hex", tr("Show as hex"), field_.hex);

        ui::sectionTitle(tr("Current value"));
        ui::row("##preview", previewValue(field_, content_));

        ImGui::Spacing();
        if (ui::row("##save", index_ ? tr("Save field") : tr("Add field"), "", ui::color::Accent).activated) save(app);
        if (index_ && ui::row("##remove", tr("Remove field"), "", ui::color::Danger).activated) {
            auto& draft = editorDraft(app, titleId_);
            if (*index_ < draft.fields.size()) draft.fields.erase(draft.fields.begin() + static_cast<std::ptrdiff_t>(*index_));
            storeEditorDraft(app, titleId_);
            app.pop();
        }
    }

private:
    void save(App& app) {
        if (auto problem = st::validateTemplateField(field_); !problem.empty()) return app.toast(problem, true);
        if (field_.offset + st::templateFieldSize(field_) > content_.size())
            return app.toast(tr("The field does not fit in the file"), true);
        auto& draft = editorDraft(app, titleId_);
        if (index_ && *index_ < draft.fields.size()) draft.fields[*index_] = field_;
        else draft.fields.push_back(field_);
        storeEditorDraft(app, titleId_);
        app.toast(trf("{} fields in the editor draft", draft.fields.size()));
        app.pop();
    }

    std::string titleId_;
    st::TemplateField field_;
    std::optional<std::size_t> index_;
    st::Bytes content_;
};

class DraftScreen : public Screen {
public:
    explicit DraftScreen(std::string titleId) : titleId_(std::move(titleId)) {}

    std::string title() const override { return tr("Editor creator"); }

    std::vector<Hint> hints(const App&) const override {
        return {{"A", tr("Select")}, {"X", tr("Remove field")}, {"B", tr("Back")}};
    }

    void draw(App& app) override {
        auto& draft = editorDraft(app, titleId_);
        auto& manifest = draft.manifest;
        ui::wrappedText(tr("Mark fields from the hex editor menu (Y), then install the result as an editor. The generated code can be edited later on a computer."),
                        ui::color::TextDim);

        ui::sectionTitle(tr("Editor"));
        if (ui::row("##name", tr("Name"), manifest.name).activated)
            app.promptText({tr("Editor name"), manifest.name, TextKind::Text, 64}, [this](App& app, std::string text) {
                if (text.empty()) return;
                editorDraft(app, titleId_).manifest.name = text;
                storeEditorDraft(app, titleId_);
            });
        if (ui::row("##id", tr("Identifier"), manifest.id).activated)
            app.promptText({tr("Identifier (lowercase, digits, . - _)"), manifest.id, TextKind::Text, 64}, [this](App& app, std::string text) {
                editorDraft(app, titleId_).manifest.id = text;
                storeEditorDraft(app, titleId_);
            });
        if (ui::row("##version", tr("Version"), manifest.version).activated)
            app.promptText({tr("Version"), manifest.version, TextKind::Text, 32}, [this](App& app, std::string text) {
                editorDraft(app, titleId_).manifest.version = text;
                storeEditorDraft(app, titleId_);
            });
        const std::string signature = draft.detectFile.empty()
                                          ? tr("None, any save opens")
                                          : std::format("{} @ 0x{:X}: {}", draft.detectFile, draft.magicOffset, st::toHex(draft.magic));
        if (ui::row("##signature", tr("Save signature"), signature).activated && !draft.detectFile.empty()) {
            app.confirm(tr("Remove the signature?"), tr("The editor will open any save of this game."), tr("Remove"), [this](App& app) {
                auto& draft = editorDraft(app, titleId_);
                draft.detectFile.clear();
                draft.magic.clear();
                storeEditorDraft(app, titleId_);
            });
        }

        ui::sectionTitle(trf("Fields ({})", draft.fields.size()));
        if (draft.fields.empty()) ui::dimText(tr("No fields yet. Open a save in the hex editor and choose \"Mark field here\"."));
        std::optional<std::size_t> removeIndex;
        for (std::size_t i = 0; i < draft.fields.size(); ++i) {
            const auto& field = draft.fields[i];
            const std::string page = field.page.empty() ? tr("General") : field.page;
            const auto result = ui::row(("##field" + std::to_string(i)).c_str(), page + " / " + field.label,
                                        field.file + "  " + describeField(field));
            if (result.activated) editField(app, i);
            if (result.focused && !app.modalOpen() && ui::pressed(ui::Button::X)) removeIndex = i;
        }
        if (removeIndex) {
            draft.fields.erase(draft.fields.begin() + static_cast<std::ptrdiff_t>(*removeIndex));
            storeEditorDraft(app, titleId_);
        }

        ui::sectionTitle(tr("Finish"));
        if (ui::row("##preview", tr("Preview code")).activated) {
            auto package = buildPackage(app, draft);
            if (package) app.push(makeInspectScreen(manifest.name, std::move(*package), std::nullopt, nullptr));
        }
        if (ui::row("##install", tr("Install editor"), "", ui::color::Accent).activated) install(app, draft);
        if (ui::row("##discard", tr("Discard draft"), "", ui::color::Danger).activated)
            app.confirm(tr("Discard the draft?"), tr("All marked fields are removed."), tr("Discard"), [this](App& app) {
                discardEditorDraft(app, titleId_);
                app.pop();
            });
    }

private:
    void editField(App& app, std::size_t index) {
        const auto& field = editorDraft(app, titleId_).fields[index];
        st::Bytes content;
        if (const auto* title = app.findTitle(titleId_); title && !title->saves.empty()) {
            if (auto mount = app.platform().mount(title->saves.front())) {
                try {
                    content = (*mount)->read(field.file);
                } catch (const std::exception&) {
                }
            }
        }
        if (content.empty()) content.resize(field.offset + st::templateFieldSize(field));
        app.push(std::make_unique<FieldScreen>(titleId_, field, index, std::move(content)));
    }

    static std::optional<st::EditorPackage> buildPackage(App& app, const st::EditorTemplate& draft) {
        auto manifest = st::parseManifest(st::manifestToJson(draft.manifest));
        if (!manifest) {
            app.showMessage(tr("Editor details are not valid"), manifest.error());
            return std::nullopt;
        }
        st::EditorPackage package{std::move(*manifest), st::generateEditorFiles(draft)};
        for (const auto& issue : st::validatePackage(package)) {
            if (issue.severity != st::ValidationIssue::Severity::Error) continue;
            app.showMessage(tr("The generated editor has a problem"), issue.file + ": " + issue.message);
            return std::nullopt;
        }
        return package;
    }

    void install(App& app, const st::EditorTemplate& draft) {
        if (draft.fields.empty()) return app.toast(tr("Mark at least one field first"), true);
        auto package = buildPackage(app, draft);
        if (!package) return;
        auto doInstall = [package = std::move(*package)](App& app) {
            auto installed = app.editors().install(package, "", "");
            if (!installed) return app.showMessage(tr("Install failed"), installed.error());
            app.toast(trf("{} installed", installed->manifest.name));
        };
        if (auto existing = app.editors().find(draft.manifest.id)) {
            app.confirm(tr("Replace the installed editor?"), trf("{} v{} uses the same identifier and will be replaced.",
                                                                 existing->manifest.name, existing->manifest.version),
                        tr("Replace"), std::move(doInstall));
            return;
        }
        doInstall(app);
    }

    std::string titleId_;
};

}

st::EditorTemplate& editorDraft(App& app, const std::string& titleId) {
    if (auto found = drafts.find(titleId); found != drafts.end()) return found->second;
    st::EditorTemplate draft = newDraft(app, titleId);
    std::ifstream stream(draftPath(app, titleId), std::ios::binary);
    if (stream) {
        auto loaded = st::templateFromJson(std::string(std::istreambuf_iterator<char>(stream), {}));
        if (loaded) draft = std::move(*loaded);
        else app.toast(trf("The saved editor draft could not be read: {}", loaded.error()), true);
    }
    return drafts[titleId] = std::move(draft);
}

void storeEditorDraft(App& app, const std::string& titleId) {
    const auto path = draftPath(app, titleId);
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << st::templateToJson(editorDraft(app, titleId));
    if (!stream) app.toast(tr("Could not save the editor draft"), true);
}

void discardEditorDraft(App& app, const std::string& titleId) {
    drafts.erase(titleId);
    std::error_code error;
    std::filesystem::remove(draftPath(app, titleId), error);
}

bool hasEditorDraft(App& app, const std::string& titleId) {
    std::error_code error;
    return drafts.contains(titleId) || std::filesystem::exists(draftPath(app, titleId), error);
}

std::unique_ptr<Screen> makeFieldScreen(const std::string& titleId, st::TemplateField field, st::Bytes content,
                                        std::optional<std::size_t> index) {
    return std::make_unique<FieldScreen>(titleId, std::move(field), index, std::move(content));
}

std::unique_ptr<Screen> makeDraftScreen(const std::string& titleId) {
    return std::make_unique<DraftScreen>(titleId);
}

}

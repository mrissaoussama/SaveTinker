#include <algorithm>
#include <format>
#include <fstream>
#include <ranges>

#include "app.hpp"
#include "save_flow.hpp"
#include "savetinker/checksum.hpp"
#include "ui.hpp"

namespace app {

namespace {

constexpr std::size_t kColumns = 16;

class HexScreen : public Screen {
public:
    HexScreen(App& app, st::SaveInfo save) : target_{std::move(save), {}} {
        const auto* title = app.findTitle(target_.save.titleId);
        target_.titleName = title ? title->info.name : target_.save.titleId;
        auto document = loadSave(app, target_.save);
        if (!document) {
            error_ = document.error();
            return;
        }
        document_ = std::make_unique<st::Document>(std::move(*document));
        files_ = document_->fileNames();
        if (files_.empty()) error_ = "This save contains no files.";
    }

    std::string title() const override { return "Hex editor"; }

    std::vector<Hint> hints(const App&) const override {
        if (!document_) return {{"B", "Back"}};
        return {{"A", "Edit"}, {"Y", "Menu"}, {"X", "Find next"}, {"L", "Page"}, {"R", "Page"},
                {"ZL", "Undo"}, {"ZR", "Redo"}, {"+", "Write"}, {"B", "Back"}};
    }

    bool requestClose(App& app) override {
        if (document_ && document_->isDirty()) {
            app.confirm("Discard changes?", "Your edits have not been written to the save.", "Discard", [](App& app) { app.pop(); });
            return false;
        }
        return true;
    }

    void draw(App& app) override {
        if (!error_.empty()) {
            ui::emptyState("Cannot open the save", error_);
            return;
        }
        const auto& buffer = document_->file(file());
        if (!app.modalOpen()) handleInput(app, buffer.size());
        collectMarkedFields(app);

        ui::dimText(std::format("{}   {}   {} of {}", file(), ui::formatSize(buffer.size()),
                                fileIndex_ + 1, files_.size()) +
                    (compare_ ? std::format("   comparing with backup: {} differences", differences_.size()) : ""));

        const float inspectorWidth = ImGui::GetFontSize() * 13;
        ImGui::BeginChild("##grid", ImVec2(ImGui::GetContentRegionAvail().x - inspectorWidth, 0), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoNavInputs);
        drawGrid(buffer);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##inspector", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoNavInputs);
        drawInspector(buffer);
        ImGui::EndChild();

        drawMenu(app);
        if (reviewing_ && !drawReviewChanges(app, *document_, [this, &app] { write(app); })) reviewing_ = false;
    }

private:
    const std::string& file() const { return files_[fileIndex_]; }

    void handleInput(App& app, std::size_t size) {
        if (size == 0) return;
        auto move = [&](std::ptrdiff_t delta) {
            const auto next = static_cast<std::ptrdiff_t>(cursor_) + delta;
            cursor_ = static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(next, 0, static_cast<std::ptrdiff_t>(size) - 1));
            followCursor_ = true;
        };
        auto key = [](ImGuiKey pad, ImGuiKey stick, ImGuiKey arrow) {
            return ImGui::IsKeyPressed(pad, true) || ImGui::IsKeyPressed(stick, true) || ImGui::IsKeyPressed(arrow, true);
        };
        if (key(ImGuiKey_GamepadDpadLeft, ImGuiKey_GamepadLStickLeft, ImGuiKey_LeftArrow)) move(-1);
        if (key(ImGuiKey_GamepadDpadRight, ImGuiKey_GamepadLStickRight, ImGuiKey_RightArrow)) move(1);
        if (key(ImGuiKey_GamepadDpadUp, ImGuiKey_GamepadLStickUp, ImGuiKey_UpArrow)) move(-static_cast<std::ptrdiff_t>(kColumns));
        if (key(ImGuiKey_GamepadDpadDown, ImGuiKey_GamepadLStickDown, ImGuiKey_DownArrow)) move(kColumns);
        if (ui::pressed(ui::Button::L, true) || ImGui::IsKeyPressed(ImGuiKey_PageUp)) move(-static_cast<std::ptrdiff_t>(kColumns * visibleRows_));
        if (ui::pressed(ui::Button::R, true) || ImGui::IsKeyPressed(ImGuiKey_PageDown)) move(kColumns * visibleRows_);

        if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown) || ImGui::IsKeyPressed(ImGuiKey_Enter)) editAtCursor(app);
        if (ui::pressed(ui::Button::Y)) ImGui::OpenPopup("##hexmenu");
        if (ui::pressed(ui::Button::X)) findNext(app);
        if (ui::pressed(ui::Button::ZL, true) && document_->undo()) refreshDifferences();
        if (ui::pressed(ui::Button::ZR, true) && document_->redo()) refreshDifferences();
        if (ui::pressed(ui::Button::Plus)) reviewing_ = true;
    }

    void drawGrid(const st::ByteBuffer& buffer) {
        auto* draw = ImGui::GetWindowDrawList();
        const float charWidth = ImGui::CalcTextSize("0").x;
        const float lineHeight = ImGui::GetTextLineHeightWithSpacing() * 1.15f;
        const float offsetWidth = charWidth * 9;
        const float byteWidth = charWidth * 3;
        const float asciiWidth = ImGui::CalcTextSize("W").x;
        const std::size_t rows = (buffer.size() + kColumns - 1) / kColumns;
        visibleRows_ = std::max<std::size_t>(1, static_cast<std::size_t>(ImGui::GetContentRegionAvail().y / lineHeight) - 1);

        if (followCursor_) {
            const float cursorY = static_cast<float>(cursor_ / kColumns) * lineHeight;
            const float scroll = ImGui::GetScrollY();
            const float height = ImGui::GetWindowHeight();
            if (cursorY < scroll) ImGui::SetScrollY(cursorY);
            else if (cursorY + lineHeight * 2 > scroll + height) ImGui::SetScrollY(cursorY + lineHeight * 2 - height);
            followCursor_ = false;
        }

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(rows), lineHeight);
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const ImVec2 origin = ImGui::GetCursorScreenPos();
                const std::size_t base = static_cast<std::size_t>(row) * kColumns;
                const auto offsetText = std::format("{:08X}", base);
                draw->AddText(origin, ui::color::TextDim, offsetText.c_str());
                for (std::size_t column = 0; column < kColumns && base + column < buffer.size(); ++column) {
                    const std::size_t index = base + column;
                    const auto value = buffer.bytes()[index];
                    const ImVec2 cell(origin.x + offsetWidth + column * byteWidth + (column >= 8 ? charWidth * 0.6f : 0), origin.y);
                    const bool atCursor = index == cursor_;
                    const bool differs = isDifferent(index);
                    const bool marked = markedField(index) != nullptr;
                    if (atCursor) draw->AddRectFilled(ImVec2(cell.x - 2, cell.y - 1), ImVec2(cell.x + charWidth * 2 + 2, cell.y + lineHeight - 3), ui::color::Accent, 3);
                    else if (differs) draw->AddRectFilled(ImVec2(cell.x - 2, cell.y - 1), ImVec2(cell.x + charWidth * 2 + 2, cell.y + lineHeight - 3), IM_COL32(120, 70, 20, 255), 3);
                    else if (marked) draw->AddRectFilled(ImVec2(cell.x - 2, cell.y - 1), ImVec2(cell.x + charWidth * 2 + 2, cell.y + lineHeight - 3), IM_COL32(40, 70, 130, 255), 3);
                    const auto text = std::format("{:02X}", value);
                    const ImU32 tint = atCursor ? IM_COL32(15, 20, 25, 255) : value == 0 ? ui::color::TextDim : ui::color::Text;
                    draw->AddText(cell, tint, text.c_str());

                    const char printable = value >= 0x20 && value < 0x7F ? static_cast<char>(value) : '.';
                    const char glyph[2] = {printable, 0};
                    const ImVec2 asciiCell(origin.x + offsetWidth + kColumns * byteWidth + charWidth * 2 + column * asciiWidth, origin.y);
                    if (atCursor) draw->AddRectFilled(asciiCell, ImVec2(asciiCell.x + asciiWidth, asciiCell.y + lineHeight - 3), ui::color::AccentDim);
                    const float glyphOffset = (asciiWidth - ImGui::CalcTextSize(glyph).x) * 0.5f;
                    draw->AddText(ImVec2(asciiCell.x + glyphOffset, asciiCell.y), differs ? ui::color::Modified : ui::color::TextDim, glyph);
                }
                ImGui::Dummy(ImVec2(offsetWidth + kColumns * byteWidth + charWidth * 3 + asciiWidth * kColumns, lineHeight));
            }
        }
    }

    void drawInspector(const st::ByteBuffer& buffer) {
        ui::sectionTitle(std::format("Offset 0x{:X}", cursor_));
        if (const auto* field = markedField(cursor_)) {
            ImGui::PushStyleColor(ImGuiCol_Text, ui::color::Accent);
            ImGui::TextUnformatted((field->label + " (" + field->type + ")").c_str());
            ImGui::PopStyleColor();
        }
        auto line = [&](const char* name, const std::string& value) {
            ImGui::PushStyleColor(ImGuiCol_Text, ui::color::TextDim);
            ImGui::TextUnformatted(name);
            ImGui::PopStyleColor();
            ImGui::SameLine(ImGui::GetFontSize() * 4.2f);
            ImGui::TextUnformatted(value.c_str());
        };
        auto read = [&]<typename T>(st::Endian endian) -> std::string {
            if (cursor_ + sizeof(T) > buffer.size()) return "-";
            return std::to_string(buffer.read<T>(cursor_, endian));
        };
        const auto little = st::Endian::Little;
        line("u8", read.operator()<std::uint8_t>(little));
        line("s8", read.operator()<std::int8_t>(little));
        line("u16", read.operator()<std::uint16_t>(little));
        line("s16", read.operator()<std::int16_t>(little));
        line("u32", read.operator()<std::uint32_t>(little));
        line("s32", read.operator()<std::int32_t>(little));
        line("u64", read.operator()<std::uint64_t>(little));
        line("f32", cursor_ + 4 <= buffer.size() ? std::format("{:.6g}", buffer.readFloat(cursor_)) : "-");
        line("f64", cursor_ + 8 <= buffer.size() ? std::format("{:.6g}", buffer.readDouble(cursor_)) : "-");
        ImGui::Spacing();
        ui::dimText("Big endian");
        line("u16", read.operator()<std::uint16_t>(st::Endian::Big));
        line("u32", read.operator()<std::uint32_t>(st::Endian::Big));
        ImGui::Spacing();
        line("bits", cursor_ < buffer.size() ? std::format("{:08b}", buffer.bytes()[cursor_]) : "-");
        std::string preview;
        for (std::size_t i = cursor_; i < buffer.size() && i < cursor_ + 16; ++i) {
            const auto value = buffer.bytes()[i];
            preview += value >= 0x20 && value < 0x7F ? static_cast<char>(value) : '.';
        }
        line("text", preview);
        if (compare_ && cursor_ < compare_->size())
            line("backup", std::format("{:02X}", (*compare_)[cursor_]));
    }

    void drawMenu(App& app) {
        ImGui::SetNextWindowPos(ImGui::GetIO().DisplaySize * 0.5f, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 20, 0));
        if (!ImGui::BeginPopup("##hexmenu")) return;
        auto item = [&](const char* id, const std::string& label, const std::string& value = {}) {
            const bool chosen = ui::row(id, label, value).activated;
            if (chosen) ImGui::CloseCurrentPopup();
            return chosen;
        };
        if (item("##goto", "Go to offset")) {
            app.promptText({"Offset (hex)", std::format("{:X}", cursor_), TextKind::Hex, 16}, [this](App& app, std::string text) {
                try {
                    const auto offset = std::stoull(text, nullptr, 16);
                    if (offset >= document_->file(file()).size()) return app.toast("Offset is past the end of the file", true);
                    cursor_ = offset;
                    followCursor_ = true;
                } catch (const std::exception&) {
                    app.toast("Not a hex number", true);
                }
            });
        }
        if (item("##findhex", "Find bytes (hex)")) search(app, true);
        if (item("##findtext", "Find text")) search(app, false);
        if (files_.size() > 1 && item("##nextfile", "Next file", files_[(fileIndex_ + 1) % files_.size()])) {
            fileIndex_ = (fileIndex_ + 1) % files_.size();
            cursor_ = 0;
            compare_.reset();
            differences_.clear();
        }
        if (item("##mark", "Mark field here")) markField(app);
        if (item("##signature", "Use bytes here as save signature")) useAsSignature(app);
        if (item("##draft", "Editor creator", std::format("{} fields", editorDraft(app, target_.save.titleId).fields.size())))
            app.push(makeDraftScreen(target_.save.titleId));
        if (item("##compare", "Compare with latest backup")) compareWithBackup(app);
        if (compare_) {
            if (item("##nextdiff", "Next difference")) jumpToDifference(true);
            if (item("##prevdiff", "Previous difference")) jumpToDifference(false);
            if (item("##stopcompare", "Stop comparing")) {
                compare_.reset();
                differences_.clear();
            }
        }
        ImGui::EndPopup();
    }

    void collectMarkedFields(App& app) {
        marked_.clear();
        for (const auto& field : editorDraft(app, target_.save.titleId).fields)
            if (field.file == file()) marked_.push_back(field);
    }

    const st::TemplateField* markedField(std::size_t index) const {
        for (const auto& field : marked_)
            if (index >= field.offset && index < field.offset + st::templateFieldSize(field)) return &field;
        return nullptr;
    }

    void markField(App& app) {
        const auto& fields = editorDraft(app, target_.save.titleId).fields;
        for (std::size_t i = 0; i < fields.size(); ++i) {
            const auto& field = fields[i];
            if (field.file == file() && cursor_ >= field.offset && cursor_ < field.offset + st::templateFieldSize(field))
                return app.push(makeFieldScreen(target_.save.titleId, field, document_->file(file()).bytes(), i));
        }
        st::TemplateField field;
        field.file = file();
        field.offset = cursor_;
        app.push(makeFieldScreen(target_.save.titleId, std::move(field), document_->file(file()).bytes()));
    }

    void useAsSignature(App& app) {
        app.promptText({"Signature length in bytes", "4", TextKind::Number, 3}, [this](App& app, std::string text) {
            std::size_t length = 0;
            try {
                length = std::stoul(text);
            } catch (const std::exception&) {
            }
            const auto& buffer = document_->file(file());
            if (length == 0 || length > 64 || cursor_ + length > buffer.size()) return app.toast("Use 1 to 64 bytes inside the file", true);
            auto& draft = editorDraft(app, target_.save.titleId);
            draft.detectFile = file();
            draft.magicOffset = cursor_;
            draft.magic = buffer.readBytes(cursor_, length);
            storeEditorDraft(app, target_.save.titleId);
            app.toast("Signature set to " + st::toHex(draft.magic));
        });
    }

    void editAtCursor(App& app) {
        const auto& buffer = document_->file(file());
        if (cursor_ >= buffer.size()) return;
        app.promptText({std::format("Bytes at 0x{:X} (hex, several allowed)", cursor_), std::format("{:02X}", buffer.bytes()[cursor_]),
                        TextKind::Hex, 64},
                       [this](App& app, std::string text) {
                           try {
                               const auto bytes = st::fromHex(text);
                               if (bytes.empty()) return;
                               if (cursor_ + bytes.size() > document_->file(file()).size())
                                   return app.toast("Not enough room before the end of the file", true);
                               document_->beginTransaction(std::format("Bytes at 0x{:X}", cursor_));
                               document_->write(file(), cursor_, bytes);
                               document_->commitTransaction();
                               cursor_ = std::min(cursor_ + bytes.size(), document_->file(file()).size() - 1);
                               refreshDifferences();
                           } catch (const std::exception&) {
                               app.toast("Use hex digits like 0A FF", true);
                           }
                       });
    }

    void search(App& app, bool hex) {
        app.promptText({hex ? "Find bytes (hex)" : "Find text", lastSearchText_, hex ? TextKind::Hex : TextKind::Text, 128},
                       [this, hex](App& app, std::string text) {
                           try {
                               lastSearch_ = hex ? st::fromHex(text) : st::Bytes(text.begin(), text.end());
                               lastSearchText_ = text;
                           } catch (const std::exception&) {
                               return app.toast("Use hex digits like 0A FF", true);
                           }
                           findNext(app, true);
                       });
    }

    void findNext(App& app, bool fromCursor = false) {
        if (lastSearch_.empty()) return app.toast("Search first from the menu", true);
        const auto& buffer = document_->file(file());
        const std::size_t start = fromCursor ? cursor_ : cursor_ + 1;
        auto found = buffer.find(lastSearch_, start);
        if (!found) found = buffer.find(lastSearch_, 0);
        if (!found) return app.toast("Not found", true);
        cursor_ = *found;
        followCursor_ = true;
    }

    void compareWithBackup(App& app) {
        const auto records = app.backups().list(target_.save.titleId, target_.save.slotName());
        if (records.empty()) return app.toast("This save has no backups yet", true);
        const auto path = records.front().directory / "files" / file();
        std::ifstream stream(path, std::ios::binary);
        if (!stream) return app.toast("The latest backup does not contain " + file(), true);
        compare_ = st::Bytes(std::istreambuf_iterator<char>(stream), {});
        refreshDifferences();
        app.toast(std::format("{} differences from {}", differences_.size(), records.front().createdAt));
    }

    void refreshDifferences() {
        if (!compare_) return;
        differences_ = st::diffBytes(document_->file(file()).view(), *compare_);
    }

    bool isDifferent(std::size_t index) const {
        if (!compare_) return false;
        return std::ranges::any_of(differences_, [index](const st::ByteRange& r) { return index >= r.offset && index < r.offset + r.length; });
    }

    void jumpToDifference(bool forward) {
        if (differences_.empty()) return;
        if (forward) {
            auto next = std::ranges::find_if(differences_, [this](const st::ByteRange& r) { return r.offset > cursor_; });
            cursor_ = next == differences_.end() ? differences_.front().offset : next->offset;
        } else {
            auto previous = std::ranges::find_if(differences_ | std::views::reverse, [this](const st::ByteRange& r) { return r.offset < cursor_; });
            cursor_ = previous == (differences_ | std::views::reverse).end() ? differences_.back().offset : previous->offset;
        }
        followCursor_ = true;
    }

    void write(App& app) {
        auto written = writeSave(app, target_, *document_, "before hex edit");
        if (!written) return app.showMessage("Save not written", written.error());
        document_->markSaved();
        app.reloadTitles();
        app.toast("Save written");
    }

    SaveTarget target_;
    std::unique_ptr<st::Document> document_;
    std::vector<std::string> files_;
    std::size_t fileIndex_ = 0;
    std::size_t cursor_ = 0;
    std::size_t visibleRows_ = 16;
    bool followCursor_ = false;
    bool reviewing_ = false;
    std::string error_;
    st::Bytes lastSearch_;
    std::string lastSearchText_;
    std::optional<st::Bytes> compare_;
    std::vector<st::ByteRange> differences_;
    std::vector<st::TemplateField> marked_;
};

}

std::unique_ptr<Screen> makeHexScreen(App& app, const st::SaveInfo& save) {
    return std::make_unique<HexScreen>(app, save);
}

}

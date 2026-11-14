#include "savetinker/editor_template.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <map>
#include <nlohmann/json.hpp>

#include "savetinker/authoring.hpp"
#include "savetinker/checksum.hpp"

namespace st {

bool isTemplateType(std::string_view type) {
    return std::ranges::find(kTemplateTypes, type) != std::end(kTemplateTypes);
}

std::size_t templateFieldSize(const TemplateField& field) {
    const auto& type = field.type;
    if (type == "bit" || type == "u8" || type == "s8") return 1;
    if (type == "u16" || type == "s16") return 2;
    if (type == "u32" || type == "s32" || type == "f32") return 4;
    if (type == "u64" || type == "s64" || type == "f64") return 8;
    if (type == "utf8") return std::max<std::size_t>(field.length, 1);
    if (type == "utf16") return std::max<std::size_t>(field.length * 2, 2);
    return 1;
}

std::string validateTemplateField(const TemplateField& field) {
    if (field.file.empty()) return "field has no file";
    if (field.label.empty()) return "field has no label";
    if (!isTemplateType(field.type)) return "unknown type \"" + field.type + "\"";
    if (field.type == "bit" && (field.bit < 0 || field.bit > 7)) return "bit must be 0-7";
    if ((field.type == "utf8" || field.type == "utf16") && field.length == 0) return "text fields need a length";
    return {};
}

namespace {

std::string luaString(std::string_view text) {
    std::string out = "\"";
    for (unsigned char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20 || c == 0x7F) out += std::format("\\{:03}", c);
            else out += static_cast<char>(c);
        }
    }
    return out + "\"";
}

std::string identifierFor(std::string_view fileName) {
    std::string name;
    for (unsigned char c : fileName) name += std::isalnum(c) ? static_cast<char>(std::tolower(c)) : '_';
    if (name.empty() || std::isdigit(static_cast<unsigned char>(name.front()))) name = "file_" + name;
    return name;
}

std::string bindingFor(const TemplateField& field, const std::string& variable) {
    const auto offset = std::format("0x{:X}", field.offset);
    if (field.type == "bit") return std::format("{}:flag({}, {})", variable, offset, field.bit);
    if (field.type == "utf8") return std::format("{}:text({}, {}, \"utf8\")", variable, offset, field.length);
    if (field.type == "utf16")
        return std::format("{}:text({}, {}, \"{}\")", variable, offset, field.length, field.bigEndian ? "utf16be" : "utf16le");
    return std::format("{}:field({}, \"{}\"{})", variable, offset, field.type, field.bigEndian ? ", \"big\"" : "");
}

std::string widgetFor(const TemplateField& field) {
    if (field.type == "bit") return "toggle";
    if (field.type == "utf8" || field.type == "utf16") return "text";
    if (field.type == "f32" || field.type == "f64") return "float";
    return "int";
}

}

std::string generateEditorScript(const EditorTemplate& draft) {
    std::map<std::string, std::string> variables;
    std::vector<std::string> fileOrder;
    auto variableFor = [&](const std::string& file) -> const std::string& {
        if (auto found = variables.find(file); found != variables.end()) return found->second;
        std::string name = identifierFor(file);
        const std::string base = name;
        for (int suffix = 2; std::ranges::any_of(variables, [&](const auto& entry) { return entry.second == name; }); ++suffix)
            name = base + "_" + std::to_string(suffix);
        fileOrder.push_back(file);
        return variables[file] = name;
    };

    std::vector<std::string> pageOrder;
    std::map<std::string, std::vector<const TemplateField*>> pages;
    for (const auto& field : draft.fields) {
        variableFor(field.file);
        const std::string page = field.page.empty() ? "General" : field.page;
        if (!pages.contains(page)) pageOrder.push_back(page);
        pages[page].push_back(&field);
    }

    std::string out;
    if (!draft.detectFile.empty()) {
        const auto file = luaString(draft.detectFile);
        out += "function detect(save)\n";
        out += "    if not save:has(" + file + ") then return false end\n";
        if (!draft.magic.empty())
            out += std::format("    return save:file({}):bytes(0x{:X}, {}) == st.unhex(\"{}\")\n", file, draft.magicOffset,
                               draft.magic.size(), toHex(draft.magic));
        else
            out += "    return true\n";
        out += "end\n\n";
    }

    out += "function build(ui, save)\n";
    for (const auto& file : fileOrder)
        out += "    local " + variables[file] + " = save:file(" + luaString(file) + ")\n";
    if (!fileOrder.empty()) out += "\n";
    for (std::size_t i = 0; i < pageOrder.size(); ++i) {
        if (i) out += "\n";
        out += "    ui:page(" + luaString(pageOrder[i]) + ", function(page)\n";
        for (const auto* field : pages[pageOrder[i]]) {
            out += "        page:" + widgetFor(*field) + "{ label = " + luaString(field->label) +
                   ", bind = " + bindingFor(*field, variables[field->file]);
            if (field->hex && widgetFor(*field) == "int") out += ", format = \"hex\"";
            out += " }\n";
        }
        out += "    end)\n";
    }
    if (pageOrder.empty()) out += "    ui:note(\"No fields yet.\")\n";
    out += "end\n";
    return out;
}

std::vector<PackageFile> generateEditorFiles(const EditorTemplate& draft) {
    auto bytesOf = [](const std::string& text) { return Bytes(text.begin(), text.end()); };
    return {{"main.lua", bytesOf(generateEditorScript(draft))}, {"manifest.json", bytesOf(manifestToJson(draft.manifest))}};
}

std::string templateToJson(const EditorTemplate& draft) {
    nlohmann::ordered_json root;
    root["manifest"] = nlohmann::ordered_json::parse(manifestToJson(draft.manifest));
    if (!draft.detectFile.empty())
        root["detect"] = {{"file", draft.detectFile}, {"offset", draft.magicOffset}, {"magic", toHex(draft.magic)}};
    auto& fields = root["fields"] = nlohmann::ordered_json::array();
    for (const auto& field : draft.fields) {
        nlohmann::ordered_json item{{"file", field.file}, {"offset", field.offset}, {"type", field.type}, {"label", field.label}};
        if (!field.page.empty()) item["page"] = field.page;
        if (field.bigEndian) item["big_endian"] = true;
        if (field.type == "bit") item["bit"] = field.bit;
        if (field.length) item["length"] = field.length;
        if (field.hex) item["hex"] = true;
        fields.push_back(std::move(item));
    }
    return root.dump(2) + "\n";
}

std::expected<EditorTemplate, std::string> templateFromJson(std::string_view json) {
    try {
        const auto root = nlohmann::json::parse(json);
        if (!root.is_object() || !root.contains("manifest")) return std::unexpected("template needs a \"manifest\" object");
        EditorTemplate draft;
        auto manifest = parseManifest(root.at("manifest").dump());
        if (!manifest) return std::unexpected("manifest: " + manifest.error());
        draft.manifest = std::move(*manifest);
        if (auto detect = root.find("detect"); detect != root.end()) {
            draft.detectFile = detect->value("file", "");
            draft.magicOffset = detect->value("offset", std::uint64_t{0});
            draft.magic = fromHex(detect->value("magic", ""));
        }
        if (auto fields = root.find("fields"); fields != root.end()) {
            for (const auto& item : *fields) {
                TemplateField field;
                field.file = item.at("file").get<std::string>();
                field.offset = item.at("offset").get<std::uint64_t>();
                field.type = item.at("type").get<std::string>();
                field.label = item.at("label").get<std::string>();
                field.page = item.value("page", "");
                field.bigEndian = item.value("big_endian", false);
                field.bit = item.value("bit", 0);
                field.length = item.value("length", std::size_t{0});
                field.hex = item.value("hex", false);
                if (auto problem = validateTemplateField(field); !problem.empty())
                    return std::unexpected(field.label.empty() ? problem : field.label + ": " + problem);
                draft.fields.push_back(std::move(field));
            }
        }
        return draft;
    } catch (const std::exception& error) {
        return std::unexpected(std::string("invalid template: ") + error.what());
    }
}

}

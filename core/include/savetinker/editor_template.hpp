#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "savetinker/byte_buffer.hpp"
#include "savetinker/editor_store.hpp"
#include "savetinker/manifest.hpp"

namespace st {

// A field marked in the hex editor. `type` is a numeric type (u8..f64), "bit", "utf8" or "utf16".
struct TemplateField {
    std::string file;
    std::uint64_t offset = 0;
    std::string type = "u32";
    std::string label;
    std::string page;
    bool bigEndian = false;
    int bit = 0;
    std::size_t length = 0;
    bool hex = false;
};

// Starting point for a new editor, built from fields picked in the hex editor.
struct EditorTemplate {
    EditorManifest manifest;
    std::string detectFile;
    std::uint64_t magicOffset = 0;
    Bytes magic;
    std::vector<TemplateField> fields;
};

inline constexpr std::string_view kTemplateTypes[] = {"u8",  "u16", "u32", "u64", "s8",  "s16",  "s32",
                                                      "s64", "f32", "f64", "bit", "utf8", "utf16"};

bool isTemplateType(std::string_view type);
// Bytes covered by the field, at least one.
std::size_t templateFieldSize(const TemplateField& field);
std::string validateTemplateField(const TemplateField& field);

std::string generateEditorScript(const EditorTemplate& draft);
// manifest.json and main.lua, ready for EditorStore::install or a folder.
std::vector<PackageFile> generateEditorFiles(const EditorTemplate& draft);

std::string templateToJson(const EditorTemplate& draft);
std::expected<EditorTemplate, std::string> templateFromJson(std::string_view json);

}

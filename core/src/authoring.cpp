#include "savetinker/authoring.hpp"

#include <algorithm>
#include <format>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>

#include "lua_support.hpp"
#include "savetinker/checksum.hpp"
#include "savetinker/platform.hpp"

namespace st {

namespace fs = std::filesystem;

std::expected<EditorPackage, std::string> readEditorDirectory(const fs::path& directory) {
    std::error_code error;
    if (!fs::is_directory(directory, error)) return std::unexpected("not a folder: " + directory.string());

    EditorPackage package;
    for (auto it = fs::recursive_directory_iterator(directory); it != fs::recursive_directory_iterator(); ++it) {
        if (it->path().filename().string().starts_with('.')) {
            if (it->is_directory()) it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file()) continue;
        std::ifstream stream(it->path(), std::ios::binary);
        if (!stream) return std::unexpected("cannot read " + it->path().string());
        package.files.push_back({relativePath(it->path(), directory), Bytes(std::istreambuf_iterator<char>(stream), {})});
    }
    std::ranges::sort(package.files, {}, &PackageFile::path);

    const auto* manifestFile = package.find("manifest.json");
    if (!manifestFile) return std::unexpected("manifest.json not found in " + directory.string());
    auto manifest = parseManifest(manifestFile->text());
    if (!manifest) return std::unexpected("manifest.json: " + manifest.error());
    package.manifest = std::move(*manifest);
    return package;
}

namespace {

std::optional<std::string> luaSyntaxError(const PackageFile& file) {
    std::unique_ptr<lua_State, decltype(&lua_close)> state(luaL_newstate(), &lua_close);
    const auto code = file.text();
    const std::string chunkName = "@" + file.path;
    if (luaL_loadbufferx(state.get(), code.data(), code.size(), chunkName.c_str(), "t") == LUA_OK) return std::nullopt;
    return std::string(lua_tostring(state.get(), -1));
}

}

std::vector<ValidationIssue> validatePackage(const EditorPackage& package) {
    using Severity = ValidationIssue::Severity;
    std::vector<ValidationIssue> issues;
    const auto& manifest = package.manifest;
    const PackageLimits limits;

    if (!package.find(manifest.entry))
        issues.push_back({Severity::Error, manifest.entry, "entry script is missing"});
    for (const auto& game : manifest.games)
        if (game.platform != platforms::Switch && game.platform != platforms::N3ds)
            issues.push_back({Severity::Warning, "manifest.json", "unknown platform \"" + game.platform + "\""});
    if (manifest.source.empty())
        issues.push_back({Severity::Warning, "manifest.json", "no \"source\" link, users cannot find the original code"});
    if (manifest.description.empty())
        issues.push_back({Severity::Warning, "manifest.json", "no \"description\""});
    if (package.files.size() > limits.maxFiles)
        issues.push_back({Severity::Error, "", "too many files (limit " + std::to_string(limits.maxFiles) + ")"});

    std::uint64_t total = 0;
    for (const auto& file : package.files) {
        total += file.content.size();
        if (file.content.size() > limits.maxFileSize)
            issues.push_back({Severity::Error, file.path, "file is larger than the package limit"});
        if (file.path.ends_with(".lua")) {
            if (auto error = luaSyntaxError(file)) issues.push_back({Severity::Error, file.path, *error});
        } else if (file.path.ends_with(".json") && file.path != "manifest.json") {
            if (!nlohmann::json::accept(file.text()))
                issues.push_back({Severity::Error, file.path, "invalid JSON"});
        }
    }
    if (total > limits.maxTotalSize) issues.push_back({Severity::Error, "", "package is larger than the size limit"});
    return issues;
}

PackedEditor packEditor(const EditorPackage& package, std::string_view downloadFolder) {
    const auto& manifest = package.manifest;
    PackedEditor packed;
    packed.fileName = manifest.id + "-" + manifest.version + ".zip";
    packed.zip = writeZip(package.files);

    auto& entry = packed.entry;
    entry.id = manifest.id;
    entry.name = manifest.name;
    entry.description = manifest.description;
    entry.version = manifest.version;
    entry.authors = manifest.authors;
    entry.source = manifest.source;
    entry.games = manifest.games;
    entry.downloadUrl = downloadFolder.empty() ? packed.fileName : std::string(downloadFolder) + "/" + packed.fileName;
    entry.sha256 = toHex(sha256(packed.zip));
    entry.size = packed.zip.size();
    return packed;
}

namespace {

using Json = nlohmann::ordered_json;

void putIfSet(Json& object, const char* key, const std::string& value) {
    if (!value.empty()) object[key] = value;
}

void putIfSet(Json& object, const char* key, const std::vector<std::string>& values) {
    if (!values.empty()) object[key] = values;
}

Json gamesToJson(const std::vector<GameTarget>& games) {
    Json list = Json::array();
    for (const auto& game : games) {
        Json item{{"platform", game.platform}, {"title_id", game.titleId}};
        putIfSet(item, "name", game.name);
        putIfSet(item, "versions", game.versions);
        list.push_back(std::move(item));
    }
    return list;
}

}

std::string manifestToJson(const EditorManifest& manifest) {
    Json root{{"id", manifest.id}, {"name", manifest.name}};
    putIfSet(root, "description", manifest.description);
    root["version"] = manifest.version;
    root["api_version"] = manifest.apiVersion;
    putIfSet(root, "authors", manifest.authors);
    putIfSet(root, "source", manifest.source);
    putIfSet(root, "update_url", manifest.updateUrl);
    if (manifest.entry != "main.lua") root["entry"] = manifest.entry;
    root["games"] = gamesToJson(manifest.games);
    return root.dump(2) + "\n";
}

std::string repoIndexToJson(const RepoIndex& index) {
    Json root{{"name", index.name}};
    putIfSet(root, "maintainer", index.maintainer);
    putIfSet(root, "homepage", index.homepage);
    putIfSet(root, "description", index.description);
    root["api_version"] = index.apiVersion;
    putIfSet(root, "platforms", index.platforms);
    Json editors = Json::array();
    for (const auto& editor : index.editors) {
        Json item{{"id", editor.id}, {"name", editor.name}};
        putIfSet(item, "description", editor.description);
        item["version"] = editor.version;
        putIfSet(item, "authors", editor.authors);
        putIfSet(item, "source", editor.source);
        item["games"] = gamesToJson(editor.games);
        item["download"] = editor.downloadUrl;
        putIfSet(item, "sha256", editor.sha256);
        if (editor.size) item["size"] = editor.size;
        putIfSet(item, "changelog", editor.changelog);
        putIfSet(item, "updated", editor.updated);
        editors.push_back(std::move(item));
    }
    root["editors"] = std::move(editors);
    return root.dump(2) + "\n";
}

namespace {

class UiDumper {
public:
    UiDumper(EditorSession& session, const UiDumpOptions& options) : session_(session), options_(options) {}

    std::expected<std::string, std::string> run() {
        const auto rootNodes = session_.root().nodes;
        const auto links = session_.topLevelLinks();
        if (auto result = nodes(rootNodes, 0, 0, &links); !result) return std::unexpected(result.error());
        for (const auto& link : links) {
            out_ += "[" + link.label + "]\n";
            if (auto opened = session_.openTopLevel(link.pageId); !opened) return std::unexpected(opened.error());
            const auto pageNodes = session_.currentPage().nodes;
            if (auto result = nodes(pageNodes, 1, 1, nullptr); !result) return std::unexpected(result.error());
        }
        return out_;
    }

private:
    static bool isTopLink(const UiNode& node, const std::vector<UiNode>* links) {
        return links && std::ranges::any_of(*links, [&](const UiNode& link) { return link.pageId == node.pageId; });
    }

    static std::string value(const UiNode& node) {
        if (node.hex)
            if (auto number = std::get_if<std::int64_t>(&node.value)) return std::format("0x{:X}", *number);
        return uiValueToString(node.value);
    }

    static std::string describe(const UiNode& node) {
        switch (node.kind) {
        case WidgetKind::Toggle: return node.toggleState() ? "on" : "off";
        case WidgetKind::Choice:
        case WidgetKind::Segmented:
            if (auto selected = node.selectedOption()) return node.options[*selected].label;
            return value(node) + " (no matching option)";
        case WidgetKind::Flags: {
            std::string set;
            for (std::size_t i = 0; i < node.options.size(); ++i)
                if (node.flagSet(i)) set += (set.empty() ? "" : ", ") + node.options[i].label;
            return set.empty() ? "none" : set;
        }
        case WidgetKind::Text: return "\"" + value(node) + "\"";
        default: return node.display.empty() ? value(node) : node.display;
        }
    }

    void line(int indent, const std::string& text, const UiNode& node) {
        out_.append(static_cast<std::size_t>(indent) * 2, ' ');
        out_ += text;
        if (node.readOnly && node.bindingRef) out_ += " (read only)";
        for (const auto& action : node.actions) out_ += " [" + action.label + "]";
        out_ += "\n";
    }

    std::expected<void, std::string> nodes(const std::vector<UiNode>& list, int indent, int depth,
                                           const std::vector<UiNode>* topLinks) {
        for (const auto& node : list) {
            switch (node.kind) {
            case WidgetKind::PageLink:
                if (isTopLink(node, topLinks)) break;
                line(indent, "> " + node.label, node);
                if (options_.openPages && depth < options_.maxDepth) {
                    if (auto opened = session_.openPage(node.pageId); !opened) return opened;
                    const auto pageNodes = session_.currentPage().nodes;
                    if (auto result = nodes(pageNodes, indent + 1, depth + 1, nullptr); !result) return result;
                    if (auto closed = session_.back(); !closed) return closed;
                }
                break;
            case WidgetKind::Section:
            case WidgetKind::Popup:
                line(indent, node.label + (node.kind == WidgetKind::Popup ? " (popup)" : "") + ":", node);
                if (auto result = nodes(node.children, indent + 1, depth, nullptr); !result) return result;
                break;
            case WidgetKind::Button: line(indent, "[" + node.label + "]", node); break;
            case WidgetKind::Note: line(indent, "note: " + node.label, node); break;
            case WidgetKind::Warning: line(indent, "warning: " + node.label, node); break;
            case WidgetKind::Separator: line(indent, "---", node); break;
            case WidgetKind::Label: line(indent, node.label + ": " + describe(node), node); break;
            default: line(indent, node.label + " = " + describe(node), node); break;
            }
        }
        return {};
    }

    EditorSession& session_;
    UiDumpOptions options_;
    std::string out_;
};

}

std::expected<std::string, std::string> dumpUi(EditorSession& session, const UiDumpOptions& options) {
    if (auto built = session.rebuild(); !built) return std::unexpected(built.error());
    return UiDumper(session, options).run();
}

}

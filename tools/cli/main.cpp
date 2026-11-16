#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "savetinker/authoring.hpp"
#include "savetinker/byte_buffer.hpp"
#include "savetinker/checksum.hpp"
#include "savetinker/code_inspect.hpp"
#include "savetinker/editor_session.hpp"
#include "savetinker/editor_template.hpp"
#include "savetinker/platform.hpp"
#include "savetinker/script_host.hpp"

namespace fs = std::filesystem;

namespace {

constexpr const char* kUsage = R"(savetinker-cli )" SAVETINKER_VERSION R"(

Usage:
  savetinker-cli validate <editor folder>...
  savetinker-cli run <editor folder> <save file or folder> [options]
      --title <id>          title id to run as (default: first game in the manifest)
      --platform <id>       switch or 3ds
      --game-version <v>    game version passed to the editor
      --lang <code>         label language
      --set <Page/Label=v>  change a field through the editor UI, repeatable
      --lua <code>          run Lua against the save, repeatable
      --game-data <folder>  extracted game files for st.game_data
      --out <folder>        write the edited save files (runs before_save)
  savetinker-cli pack <editors folder> <repo folder> [--name <n>] [--maintainer <m>]
                      [--homepage <url>] [--description <text>]
  savetinker-cli new <folder> --id <id> --name <name> --title <title id> [--platform <id>]
                     [--template <file.json>]
  savetinker-cli diff <file or folder> <file or folder>
  savetinker-cli inspect <file.lua>...
)";

struct UsageError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class Arguments {
public:
    explicit Arguments(std::span<char*> args) {
        for (std::size_t i = 0; i < args.size(); ++i) {
            std::string arg = args[i];
            if (arg.starts_with("--")) {
                if (i + 1 >= args.size()) throw UsageError("missing value for " + arg);
                options_.emplace_back(arg.substr(2), args[++i]);
            } else {
                positional_.push_back(std::move(arg));
            }
        }
    }

    const std::vector<std::string>& positional() const { return positional_; }

    std::optional<std::string> option(std::string_view name) const {
        for (const auto& [key, value] : options_)
            if (key == name) return value;
        return std::nullopt;
    }

    std::vector<std::string> all(std::string_view name) const {
        std::vector<std::string> values;
        for (const auto& [key, value] : options_)
            if (key == name) values.push_back(value);
        return values;
    }

    void allowOnly(std::initializer_list<std::string_view> names) const {
        for (const auto& [key, value] : options_)
            if (std::ranges::find(names, key) == names.end()) throw UsageError("unknown option --" + key);
    }

private:
    std::vector<std::string> positional_;
    std::vector<std::pair<std::string, std::string>> options_;
};

std::string readText(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot read " + path.string());
    return std::string(std::istreambuf_iterator<char>(stream), {});
}

void writeBytes(const fs::path& path, st::ByteView data) {
    if (path.has_parent_path()) fs::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!stream) throw std::runtime_error("cannot write " + path.string());
}

void writeText(const fs::path& path, std::string_view text) {
    writeBytes(path, st::ByteView(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
}

st::Document loadSave(const fs::path& path) {
    if (fs::is_directory(path)) {
        st::DirectorySaveMount mount(path);
        return st::loadDocument(mount);
    }
    const auto content = readText(path);
    st::Document document;
    document.addFile(path.filename().string(), st::Bytes(content.begin(), content.end()));
    return document;
}

const char* severityName(st::ValidationIssue::Severity severity) {
    return severity == st::ValidationIssue::Severity::Error ? "error" : "warning";
}

int validate(const Arguments& args) {
    args.allowOnly({});
    if (args.positional().empty()) throw UsageError("validate needs at least one editor folder");
    int errors = 0;
    for (const auto& folder : args.positional()) {
        auto package = st::readEditorDirectory(folder);
        if (!package) {
            std::println("{}: error: {}", folder, package.error());
            ++errors;
            continue;
        }
        const auto issues = st::validatePackage(*package);
        for (const auto& issue : issues) {
            if (issue.severity == st::ValidationIssue::Severity::Error) ++errors;
            std::println("{}/{}: {}: {}", folder, issue.file, severityName(issue.severity), issue.message);
        }
        if (issues.empty()) std::println("{}: {} {} ok", folder, package->manifest.id, package->manifest.version);
    }
    return errors ? 1 : 0;
}

const st::UiNode* findNode(const std::vector<st::UiNode>& nodes, std::string_view label, bool pageLink) {
    for (const auto& node : nodes) {
        const bool isLink = node.kind == st::WidgetKind::PageLink;
        // Page titles often end in a detail such as "  (3 fields)"; the part before it names the page too.
        const auto detail = node.label.find("  (");
        const bool named = node.label == label || (pageLink && detail != std::string::npos && node.label.substr(0, detail) == label);
        if (named && isLink == pageLink) return &node;
        if (node.kind == st::WidgetKind::Section || node.kind == st::WidgetKind::Popup)
            if (auto* found = findNode(node.children, label, pageLink)) return found;
    }
    return nullptr;
}

std::vector<std::string> splitPath(std::string_view path) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (true) {
        const auto slash = path.find('/', start);
        parts.emplace_back(path.substr(start, slash - start));
        if (slash == std::string_view::npos) break;
        start = slash + 1;
    }
    return parts;
}

void check(const std::expected<void, std::string>& result) {
    if (!result) throw std::runtime_error(result.error());
}

std::optional<st::UiValue> parseValue(const st::UiNode& node, const std::string& text) {
    switch (node.kind) {
    case st::WidgetKind::Integer: return std::int64_t{std::stoll(text, nullptr, 0)};
    case st::WidgetKind::Float: return std::stod(text);
    case st::WidgetKind::Text: return text;
    case st::WidgetKind::Toggle:
        if (text == "on" || text == "true" || text == "1") return node.onValue;
        if (text == "off" || text == "false" || text == "0") return node.offValue;
        throw std::runtime_error("toggle values are on or off");
    case st::WidgetKind::Choice:
    case st::WidgetKind::Segmented:
        for (const auto& option : node.options)
            if (option.label == text || st::uiValueToString(option.value) == text) return option.value;
        throw std::runtime_error("no option \"" + text + "\" for " + node.label);
    default: return std::nullopt;
    }
}

void applySet(st::EditorSession& session, const std::string& assignment) {
    const auto equals = assignment.find('=');
    if (equals == std::string::npos) throw UsageError("--set expects Page/Label=value");
    const auto parts = splitPath(assignment.substr(0, equals));
    const std::string text = assignment.substr(equals + 1);

    check(session.openTopLevel(""));
    for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
        const auto& nodes = i == 0 ? session.root().nodes : session.currentPage().nodes;
        const auto* link = findNode(nodes, parts[i], true);
        if (!link) throw std::runtime_error("page not found: " + parts[i]);
        const auto pageId = link->pageId;
        check(i == 0 ? session.openTopLevel(pageId) : session.openPage(pageId));
    }
    const auto* found = findNode(session.currentPage().nodes, parts.back(), false);
    if (!found) throw std::runtime_error("field not found: " + parts.back());
    const st::UiNode node = *found;
    if (!node.editable()) throw std::runtime_error(node.label + " cannot be edited");

    if (node.kind == st::WidgetKind::Flags) {
        std::vector<std::string> wanted;
        for (std::size_t start = 0; start <= text.size();) {
            const auto comma = std::min(text.find(',', start), text.size());
            if (comma > start) wanted.push_back(text.substr(start, comma - start));
            start = comma + 1;
        }
        for (std::size_t i = 0; i < node.options.size(); ++i) {
            const bool enable = std::ranges::find(wanted, node.options[i].label) != wanted.end();
            const auto current = std::get<std::int64_t>(node.value);
            const auto bit = std::get<std::int64_t>(node.options[i].value);
            if (static_cast<bool>((current >> bit) & 1) != enable) {
                const auto* fresh = findNode(session.currentPage().nodes, parts.back(), false);
                check(session.setFlag(*fresh, i, enable));
            }
        }
        return;
    }
    const auto value = parseValue(node, text);
    if (!value) throw std::runtime_error(node.label + " is not a value field");
    check(session.setValue(node, *value));
}

// Dependencies are looked up among the folders next to the editor, the way a repository keeps them.
std::vector<fs::path> siblingDependencies(const fs::path& editorDir, const st::EditorManifest& manifest) {
    std::vector<std::pair<std::string, fs::path>> available;
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(fs::absolute(editorDir).parent_path(), error))
        if (entry.is_directory())
            if (auto other = st::readEditorDirectory(entry.path())) available.emplace_back(other->manifest.id, entry.path());
    std::vector<fs::path> folders;
    std::vector<std::string> seen{manifest.id};
    std::vector<st::Dependency> pending = manifest.dependencies;
    while (!pending.empty()) {
        const auto dependency = pending.front();
        pending.erase(pending.begin());
        if (std::ranges::find(seen, dependency.id) != seen.end()) continue;
        seen.push_back(dependency.id);
        auto found = std::ranges::find(available, dependency.id, &std::pair<std::string, fs::path>::first);
        if (found == available.end()) {
            std::println(stderr, "warning: dependency {} not found next to the editor", dependency.id);
            continue;
        }
        folders.push_back(found->second);
        if (auto library = st::readEditorDirectory(found->second))
            for (const auto& more : library->manifest.dependencies) pending.push_back(more);
    }
    return folders;
}

int run(const Arguments& args) {
    args.allowOnly({"title", "platform", "game-version", "lang", "set", "lua", "out", "game-data"});
    if (args.positional().size() != 2) throw UsageError("run needs an editor folder and a save");
    const fs::path editorDir = args.positional()[0];
    auto package = st::readEditorDirectory(editorDir);
    if (!package) throw std::runtime_error(package.error());
    const auto& manifest = package->manifest;

    st::ScriptContext context;
    context.editor = manifest;
    context.platform = args.option("platform").value_or(manifest.games.front().platform);
    context.titleId = st::normalizeTitleId(args.option("title").value_or(manifest.games.front().titleId));
    if (const auto* game = manifest.findGame(context.platform, context.titleId)) context.gameName = game->name;
    else std::println(stderr, "warning: editor does not list {} {}", context.platform, context.titleId);
    context.gameVersion = args.option("game-version").value_or("");
    context.language = args.option("lang").value_or("en");

    auto document = loadSave(args.positional()[1]);
    st::ScriptHost host(document, context,
                        st::packageModuleLoader(editorDir, siblingDependencies(editorDir, manifest), editorDir.parent_path() / "_lib"));
    host.setDataLoader(st::directoryDataLoader(editorDir));
    if (auto gameData = args.option("game-data")) {
        host.setGameDataOpener([folder = fs::path(*gameData)]() -> std::expected<std::shared_ptr<st::GameData>, std::string> {
            return std::make_shared<st::DirectoryGameData>(folder);
        });
    }
    check(host.loadEntry());

    auto detected = host.detect();
    if (!detected) throw std::runtime_error("detect failed: " + detected.error());
    if (!*detected) std::println(stderr, "warning: detect() does not recognise this save");
    if (auto version = host.saveVersion(); !version) {
        throw std::runtime_error("save_version failed: " + version.error());
    } else if (*version) {
        const auto* game = manifest.findGame(context.platform, context.titleId);
        std::println(stderr, "save version {}", **version);
        if (game && !game->supportsSaveVersion(**version))
            std::println(stderr, "warning: the editor lists save versions it supports and {} is not one of them", **version);
    }

    for (const auto& code : args.all("lua")) check(host.run(code, "--lua"));

    std::optional<st::EditorSession> session;
    if (host.hasFunction("build")) {
        session.emplace(host);
        check(session->rebuild());
        for (const auto& assignment : args.all("set")) applySet(*session, assignment);
        auto dump = st::dumpUi(*session);
        if (!dump) throw std::runtime_error("build failed: " + dump.error());
        std::print("{}", *dump);
    }

    if (auto out = args.option("out")) {
        check(host.beforeSave());
        for (const auto& name : document.fileNames()) writeBytes(fs::path(*out) / name, document.file(name).bytes());
        for (const auto& change : document.appliedLabels()) std::println(stderr, "changed: {}", change);
        std::println(stderr, "wrote {}", *out);
    }

    for (const auto& entry : host.log())
        std::println(stderr, "{}{}", entry.level == st::LogEntry::Level::Warning ? "warn: " : "log: ", entry.message);
    return 0;
}

int pack(const Arguments& args) {
    args.allowOnly({"name", "maintainer", "homepage", "description"});
    if (args.positional().size() != 2) throw UsageError("pack needs an editors folder and a repo folder");
    const fs::path editorsDir = args.positional()[0];
    const fs::path repoDir = args.positional()[1];

    st::RepoIndex index;
    std::map<std::string, st::RepoEditorEntry> previous;
    if (fs::exists(repoDir / "repo.json")) {
        auto existing = st::parseRepoIndex(readText(repoDir / "repo.json"));
        if (!existing) throw std::runtime_error("repo.json: " + existing.error());
        index = *existing;
        for (auto& editor : existing->editors) previous[editor.id] = editor;
        index.editors.clear();
        index.platforms.clear();
    }
    if (auto value = args.option("name")) index.name = *value;
    if (auto value = args.option("maintainer")) index.maintainer = *value;
    if (auto value = args.option("homepage")) index.homepage = *value;
    if (auto value = args.option("description")) index.description = *value;
    if (index.name.empty()) throw UsageError("new repositories need --name");

    std::vector<fs::path> folders;
    for (const auto& entry : fs::directory_iterator(editorsDir))
        if (entry.is_directory() && fs::exists(entry.path() / "manifest.json")) folders.push_back(entry.path());
    std::ranges::sort(folders);

    int errors = 0;
    for (const auto& folder : folders) {
        auto package = st::readEditorDirectory(folder);
        if (!package) {
            std::println("{}: error: {}", folder.string(), package.error());
            ++errors;
            continue;
        }
        bool broken = false;
        for (const auto& issue : st::validatePackage(*package)) {
            if (issue.severity != st::ValidationIssue::Severity::Error) continue;
            std::println("{}/{}: error: {}", folder.string(), issue.file, issue.message);
            broken = true;
        }
        if (broken) {
            ++errors;
            continue;
        }
        auto packed = st::packEditor(*package);
        if (auto old = previous.find(packed.entry.id); old != previous.end() && old->second.version == packed.entry.version) {
            packed.entry.changelog = old->second.changelog;
            packed.entry.updated = old->second.updated;
        }
        writeBytes(repoDir / packed.entry.downloadUrl, packed.zip);
        for (const auto& game : packed.entry.games)
            if (std::ranges::find(index.platforms, game.platform) == index.platforms.end()) index.platforms.push_back(game.platform);
        std::println("{} {} -> {} ({} bytes)", packed.entry.id, packed.entry.version, packed.entry.downloadUrl, packed.zip.size());
        index.editors.push_back(std::move(packed.entry));
    }
    if (errors) {
        std::println("{} editor(s) failed, repo.json not written", errors);
        return 1;
    }
    writeText(repoDir / "repo.json", st::repoIndexToJson(index));
    std::println("wrote {} with {} editor(s)", (repoDir / "repo.json").string(), index.editors.size());
    return 0;
}

int create(const Arguments& args) {
    args.allowOnly({"id", "name", "title", "platform", "template"});
    if (args.positional().size() != 1) throw UsageError("new needs a target folder");
    const fs::path target = args.positional()[0];
    if (fs::exists(target / "manifest.json")) throw std::runtime_error(target.string() + " already has an editor");

    st::EditorTemplate draft;
    if (auto templateFile = args.option("template")) {
        auto loaded = st::templateFromJson(readText(*templateFile));
        if (!loaded) throw std::runtime_error(loaded.error());
        draft = std::move(*loaded);
    } else {
        const auto id = args.option("id");
        const auto name = args.option("name");
        const auto title = args.option("title");
        if (!id || !name || !title) throw UsageError("new needs --id, --name and --title (or --template)");
        const auto platform = args.option("platform").value_or("switch");
        const nlohmann::json manifest = {{"id", *id}, {"name", *name}, {"version", "0.1.0"},
                                         {"games", {{{"platform", platform}, {"title_id", *title}}}}};
        auto parsed = st::parseManifest(manifest.dump());
        if (!parsed) throw std::runtime_error(parsed.error());
        draft.manifest = std::move(*parsed);
    }
    for (const auto& file : st::generateEditorFiles(draft)) writeBytes(target / file.path, file.content);
    std::println("created {} in {}", draft.manifest.id, target.string());
    return 0;
}

std::string hexPreview(st::ByteView data, std::size_t offset, std::size_t length) {
    if (offset >= data.size()) return "-";
    const auto shown = std::min({length, data.size() - offset, std::size_t{16}});
    auto text = st::toHex(data.subspan(offset, shown));
    if (shown < length) text += "...";
    return text;
}

int diffFiles(const std::string& name, st::ByteView a, st::ByteView b) {
    const auto ranges = st::diffBytes(a, b);
    if (ranges.empty()) return 0;
    std::println("{}: {} change(s)", name, ranges.size());
    if (a.size() != b.size()) std::println("  size {} -> {}", a.size(), b.size());
    for (const auto& range : ranges)
        std::println("  0x{:08X} +{}: {} -> {}", range.offset, range.length, hexPreview(a, range.offset, range.length),
                     hexPreview(b, range.offset, range.length));
    return 1;
}

int diff(const Arguments& args) {
    args.allowOnly({});
    if (args.positional().size() != 2) throw UsageError("diff needs two files or folders");
    const auto left = loadSave(args.positional()[0]);
    const auto right = loadSave(args.positional()[1]);
    int changed = 0;
    if (!fs::is_directory(args.positional()[0]) && !fs::is_directory(args.positional()[1])) {
        const auto& a = left.file(left.fileNames().front()).bytes();
        const auto& b = right.file(right.fileNames().front()).bytes();
        return diffFiles(args.positional()[0], a, b);
    }
    for (const auto& name : left.fileNames()) {
        if (!right.hasFile(name)) {
            std::println("{}: only in {}", name, args.positional()[0]);
            changed = 1;
            continue;
        }
        changed |= diffFiles(name, left.file(name).bytes(), right.file(name).bytes());
    }
    for (const auto& name : right.fileNames()) {
        if (left.hasFile(name)) continue;
        std::println("{}: only in {}", name, args.positional()[1]);
        changed = 1;
    }
    return changed;
}

const char* findingName(st::CodeFinding::Kind kind) {
    switch (kind) {
    case st::CodeFinding::Kind::Writes: return "writes";
    case st::CodeFinding::Kind::Requires: return "requires";
    case st::CodeFinding::Kind::ReadsData: return "reads";
    case st::CodeFinding::Kind::ReadsGameData: return "reads game file";
    default: return "note";
    }
}

int inspect(const Arguments& args) {
    args.allowOnly({});
    if (args.positional().empty()) throw UsageError("inspect needs at least one Lua file");
    for (const auto& file : args.positional())
        for (const auto& finding : st::summarizeScript(readText(file)))
            std::println("{}:{}: {}: {}", file, finding.line, findingName(finding.kind), finding.detail);
    return 0;
}

}

int main(int argc, char** argv) {
    if (argc < 2 || std::string_view(argv[1]) == "--help" || std::string_view(argv[1]) == "help") {
        std::print("{}", kUsage);
        return argc < 2 ? 2 : 0;
    }
    const std::string command = argv[1];
    try {
        const Arguments args(std::span(argv + 2, static_cast<std::size_t>(argc - 2)));
        if (command == "validate") return validate(args);
        if (command == "run") return run(args);
        if (command == "pack") return pack(args);
        if (command == "new") return create(args);
        if (command == "diff") return diff(args);
        if (command == "inspect") return inspect(args);
        throw UsageError("unknown command " + command);
    } catch (const UsageError& error) {
        std::println(stderr, "{}\n\n{}", error.what(), kUsage);
        return 2;
    } catch (const std::exception& error) {
        std::println(stderr, "error: {}", error.what());
        return 1;
    }
}

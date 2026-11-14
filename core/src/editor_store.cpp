#include "savetinker/editor_store.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>

#include <miniz.h>

#include "game_target_json.hpp"
#include "savetinker/checksum.hpp"
#include "savetinker/platform.hpp"

namespace st {

namespace fs = std::filesystem;

namespace {

constexpr const char* kOriginFile = ".savetinker.json";

bool isSafePackagePath(std::string_view path) {
    if (path.empty() || path.front() == '/' || path.find('\\') != std::string_view::npos ||
        path.find(':') != std::string_view::npos)
        return false;
    const fs::path normalized = fs::path(path).lexically_normal();
    return !normalized.empty() && *normalized.begin() != ".." && normalized.generic_string() == path;
}

Bytes readFile(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot read " + path.string());
    return Bytes(std::istreambuf_iterator<char>(stream), {});
}

void writeFile(const fs::path& path, ByteView data) {
    fs::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!stream) throw std::runtime_error("cannot write " + path.string());
}

std::string stripCommonFolder(std::vector<PackageFile>& files) {
    if (files.empty()) return {};
    const auto slash = files.front().path.find('/');
    if (slash == std::string::npos) return {};
    const std::string prefix = files.front().path.substr(0, slash + 1);
    const bool shared = std::ranges::all_of(files, [&](const PackageFile& f) { return f.path.starts_with(prefix); });
    if (!shared) return {};
    for (auto& file : files) file.path.erase(0, prefix.size());
    return prefix;
}

}

const PackageFile* EditorPackage::find(std::string_view path) const {
    auto found = std::ranges::find(files, path, &PackageFile::path);
    return found == files.end() ? nullptr : &*found;
}

std::expected<std::vector<PackageFile>, std::string> readZip(ByteView zip, const PackageLimits& limits) {
    mz_zip_archive archive{};
    if (!mz_zip_reader_init_mem(&archive, zip.data(), zip.size(), 0)) return std::unexpected("not a valid zip file");

    std::vector<PackageFile> files;
    std::string error;
    std::uint64_t total = 0;
    const mz_uint count = mz_zip_reader_get_num_files(&archive);
    for (mz_uint i = 0; i < count && error.empty(); ++i) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&archive, i, &stat)) { error = "corrupt zip entry"; break; }
        if (stat.m_is_directory) continue;
        const std::string path = stat.m_filename;
        if (!isSafePackagePath(path)) { error = "unsafe path in package: " + path; break; }
        if (files.size() >= limits.maxFiles) { error = "package has too many files"; break; }
        if (stat.m_uncomp_size > limits.maxFileSize) { error = "file too large: " + path; break; }
        total += stat.m_uncomp_size;
        if (total > limits.maxTotalSize) { error = "package too large"; break; }

        Bytes content(static_cast<std::size_t>(stat.m_uncomp_size));
        if (!mz_zip_reader_extract_to_mem(&archive, i, content.data(), content.size(), 0)) {
            error = "cannot extract " + path;
            break;
        }
        files.push_back({path, std::move(content)});
    }
    mz_zip_reader_end(&archive);
    if (!error.empty()) return std::unexpected(error);
    std::ranges::sort(files, {}, &PackageFile::path);
    return files;
}

Bytes writeZip(const std::vector<PackageFile>& files) {
    mz_zip_archive archive{};
    if (!mz_zip_writer_init_heap(&archive, 0, 0)) throw std::runtime_error("cannot create zip");
    for (const auto& file : files) {
        if (!mz_zip_writer_add_mem(&archive, file.path.c_str(), file.content.data(), file.content.size(), MZ_DEFAULT_COMPRESSION)) {
            mz_zip_writer_end(&archive);
            throw std::runtime_error("cannot add " + file.path);
        }
    }
    void* buffer = nullptr;
    std::size_t size = 0;
    mz_zip_writer_finalize_heap_archive(&archive, &buffer, &size);
    Bytes result(static_cast<std::uint8_t*>(buffer), static_cast<std::uint8_t*>(buffer) + size);
    mz_zip_writer_end(&archive);
    return result;
}

std::expected<EditorPackage, std::string> openPackage(ByteView zip, const PackageLimits& limits) {
    auto files = readZip(zip, limits);
    if (!files) return std::unexpected(files.error());
    EditorPackage package;
    package.files = std::move(*files);
    if (!package.find("manifest.json")) stripCommonFolder(package.files);
    const auto* manifestFile = package.find("manifest.json");
    if (!manifestFile) return std::unexpected("package has no manifest.json");
    auto manifest = parseManifest(manifestFile->text());
    if (!manifest) return std::unexpected("manifest.json: " + manifest.error());
    package.manifest = std::move(*manifest);
    if (!package.find(package.manifest.entry))
        return std::unexpected("entry script '" + package.manifest.entry + "' is missing");
    return package;
}

EditorStore::EditorStore(fs::path root) : root_(std::move(root)) {}

std::vector<InstalledEditorInfo> EditorStore::list(std::vector<BrokenEditor>* broken) const {
    std::vector<InstalledEditorInfo> editors;
    std::error_code error;
    if (!fs::is_directory(root_, error)) return editors;
    for (const auto& entry : fs::directory_iterator(root_)) {
        const auto name = entry.path().filename().string();
        if (!entry.is_directory() || name.starts_with('_') || name.starts_with('.')) continue;
        try {
            const auto manifestBytes = readFile(entry.path() / "manifest.json");
            auto manifest = parseManifest(std::string_view(reinterpret_cast<const char*>(manifestBytes.data()), manifestBytes.size()));
            if (!manifest) throw std::runtime_error(manifest.error());
            InstalledEditorInfo info{entry.path(), std::move(*manifest), {}, {}, {}};
            std::ifstream originStream(entry.path() / kOriginFile);
            if (originStream) {
                const auto origin = nlohmann::json::parse(originStream, nullptr, false);
                if (origin.is_object()) {
                    info.repoUrl = origin.value("repo_url", "");
                    info.packageSha256 = origin.value("sha256", "");
                    info.installedAt = origin.value("installed_at", "");
                }
            }
            editors.push_back(std::move(info));
        } catch (const std::exception& failure) {
            if (broken) broken->push_back({entry.path(), failure.what()});
        }
    }
    std::ranges::sort(editors, {}, [](const InstalledEditorInfo& e) { return e.manifest.name; });
    return editors;
}

std::vector<InstalledEditorInfo> EditorStore::forGame(std::string_view platform, std::string_view titleId) const {
    auto editors = list();
    std::erase_if(editors, [&](const InstalledEditorInfo& e) { return !e.manifest.findGame(platform, titleId); });
    return editors;
}

std::optional<InstalledEditorInfo> EditorStore::find(std::string_view id) const {
    for (auto& editor : list())
        if (editor.manifest.id == id) return editor;
    return std::nullopt;
}

std::expected<EditorPackage, std::string> EditorStore::load(std::string_view id) const {
    const auto editor = find(id);
    if (!editor) return std::unexpected("editor not installed: " + std::string(id));
    EditorPackage package{editor->manifest, {}};
    for (const auto& entry : fs::recursive_directory_iterator(editor->directory)) {
        if (!entry.is_regular_file()) continue;
        const auto relative = relativePath(entry.path(), editor->directory);
        if (relative == kOriginFile) continue;
        package.files.push_back({relative, readFile(entry.path())});
    }
    std::ranges::sort(package.files, {}, &PackageFile::path);
    return package;
}

std::expected<InstalledEditorInfo, std::string> EditorStore::install(const EditorPackage& package, const std::string& repoUrl,
                                                                     const std::string& packageSha256) {
    const auto& id = package.manifest.id;
    if (!isValidEditorId(id) || id.starts_with('_') || id.starts_with('.')) return std::unexpected("invalid editor id");
    const fs::path staging = root_ / (".staging-" + id);
    const fs::path target = root_ / id;
    std::error_code ignored;
    try {
        fs::remove_all(staging);
        for (const auto& file : package.files) {
            if (!isSafePackagePath(file.path)) throw std::runtime_error("unsafe path: " + file.path);
            writeFile(staging / file.path, file.content);
        }
        const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
        const nlohmann::json origin = {{"repo_url", repoUrl}, {"sha256", packageSha256},
                                       {"version", package.manifest.version},
                                       {"installed_at", std::format("{:%Y-%m-%dT%H:%M:%SZ}", now)}};
        const auto originText = origin.dump(2);
        writeFile(staging / kOriginFile, ByteView(reinterpret_cast<const std::uint8_t*>(originText.data()), originText.size()));
        fs::remove_all(target);
        fs::rename(staging, target);
    } catch (const std::exception& error) {
        fs::remove_all(staging, ignored);
        return std::unexpected(std::string("install failed: ") + error.what());
    }
    auto installed = find(id);
    if (!installed) return std::unexpected("installed editor could not be read back");
    return *installed;
}

std::expected<void, std::string> EditorStore::remove(std::string_view id) {
    const auto editor = find(id);
    if (!editor) return std::unexpected("editor not installed");
    std::error_code error;
    fs::remove_all(editor->directory, error);
    if (error) return std::unexpected("cannot remove: " + error.message());
    return {};
}

std::vector<InstalledEditor> EditorStore::installedVersions() const {
    std::vector<InstalledEditor> result;
    for (const auto& editor : list()) result.push_back({editor.manifest.id, editor.manifest.version, editor.repoUrl});
    return result;
}

}

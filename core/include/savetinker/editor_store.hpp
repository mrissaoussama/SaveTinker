#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "savetinker/byte_buffer.hpp"
#include "savetinker/manifest.hpp"
#include "savetinker/repository.hpp"

namespace st {

struct PackageFile {
    std::string path;
    Bytes content;

    std::string_view text() const { return {reinterpret_cast<const char*>(content.data()), content.size()}; }
};

struct EditorPackage {
    EditorManifest manifest;
    std::vector<PackageFile> files;

    const PackageFile* find(std::string_view path) const;
};

struct PackageLimits {
    std::size_t maxFiles = 512;
    std::uint64_t maxFileSize = 16u << 20;
    std::uint64_t maxTotalSize = 32u << 20;
};

std::expected<std::vector<PackageFile>, std::string> readZip(ByteView zip, const PackageLimits& limits = {});
Bytes writeZip(const std::vector<PackageFile>& files);
// Validates a zip and its manifest. A single top-level folder inside the zip is stripped.
std::expected<EditorPackage, std::string> openPackage(ByteView zip, const PackageLimits& limits = {});

struct InstalledEditorInfo {
    std::filesystem::path directory;
    EditorManifest manifest;
    std::string repoUrl;
    std::string packageSha256;
    std::string installedAt;
};

struct BrokenEditor {
    std::filesystem::path directory;
    std::string error;
};

// Editors live in <root>/<editor id>/; shared Lua libraries live in <root>/_lib/.
class EditorStore {
public:
    explicit EditorStore(std::filesystem::path root);

    const std::filesystem::path& root() const { return root_; }
    std::filesystem::path libraryDir() const { return root_ / "_lib"; }

    std::vector<InstalledEditorInfo> list(std::vector<BrokenEditor>* broken = nullptr) const;
    std::vector<InstalledEditorInfo> forGame(std::string_view platform, std::string_view titleId) const;
    std::optional<InstalledEditorInfo> find(std::string_view id) const;
    std::expected<EditorPackage, std::string> load(std::string_view id) const;

    std::expected<InstalledEditorInfo, std::string> install(const EditorPackage& package, const std::string& repoUrl,
                                                            const std::string& packageSha256);
    std::expected<void, std::string> remove(std::string_view id);

    std::vector<InstalledEditor> installedVersions() const;

private:
    std::filesystem::path root_;
};

}

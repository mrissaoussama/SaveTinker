#include "savetinker/platform.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>

namespace st {

namespace fs = std::filesystem;

namespace {

constexpr std::array<std::pair<SaveKind, std::string_view>, 5> kindNames{{
    {SaveKind::Account, "account"},
    {SaveKind::Device, "device"},
    {SaveKind::Bcat, "bcat"},
    {SaveKind::Cache, "cache"},
    {SaveKind::System, "system"},
}};

}

std::string_view saveKindName(SaveKind kind) {
    for (const auto& [value, name] : kindNames)
        if (value == kind) return name;
    return "unknown";
}

std::optional<SaveKind> parseSaveKind(std::string_view name) {
    for (const auto& [value, known] : kindNames)
        if (known == name) return value;
    return std::nullopt;
}

std::string SaveInfo::slotName() const {
    std::string name(saveKindName(kind));
    if (kind == SaveKind::Account && !userId.empty()) name += "-" + userId;
    return name;
}

DirectorySaveMount::DirectorySaveMount(fs::path root, CommitHook commitHook, std::uint64_t chunkSize)
    : root_(std::move(root)), commitHook_(std::move(commitHook)), chunkSize_(chunkSize) {}

fs::path DirectorySaveMount::resolve(const std::string& path) const {
    const fs::path relative = fs::path(path).lexically_normal();
    if (relative.is_absolute() || relative.empty() || *relative.begin() == "..")
        throw std::invalid_argument("path escapes save root: " + path);
    return root_ / relative;
}

std::vector<SaveFileEntry> DirectorySaveMount::listFiles() {
    std::vector<SaveFileEntry> entries;
    for (const auto& entry : fs::recursive_directory_iterator(root_)) {
        if (!entry.is_regular_file()) continue;
        entries.push_back({relativePath(entry.path(), root_), entry.file_size()});
    }
    std::ranges::sort(entries, {}, &SaveFileEntry::path);
    return entries;
}

Bytes DirectorySaveMount::read(const std::string& path) {
    std::ifstream stream(resolve(path), std::ios::binary);
    if (!stream) throw std::runtime_error("cannot open " + path);
    return Bytes(std::istreambuf_iterator<char>(stream), {});
}

void DirectorySaveMount::write(const std::string& path, ByteView data) {
    const auto target = resolve(path);
    fs::create_directories(target.parent_path());
    // Large writes are split and committed in between so the save journal does not overflow.
    const std::uint64_t step = chunkSize_ ? chunkSize_ : std::max<std::uint64_t>(data.size(), 1);
    std::uint64_t written = 0;
    do {
        const auto mode = std::ios::binary | (written == 0 ? std::ios::trunc : std::ios::app);
        std::ofstream stream(target, mode);
        if (!stream) throw std::runtime_error("cannot write " + path);
        const auto length = std::min<std::uint64_t>(step, data.size() - written);
        stream.write(reinterpret_cast<const char*>(data.data() + written), static_cast<std::streamsize>(length));
        stream.close();
        if (!stream) throw std::runtime_error("write failed for " + path);
        written += length;
        if (written < data.size() && commitHook_) commitHook_();
    } while (written < data.size());
}

void DirectorySaveMount::remove(const std::string& path) {
    fs::remove(resolve(path));
}

void DirectorySaveMount::commit() {
    if (commitHook_) commitHook_();
}

std::string relativePath(const fs::path& path, const fs::path& root) {
    const std::string full = path.generic_string();
    std::string prefix = root.generic_string();
    if (!prefix.empty() && prefix.back() != '/') prefix += '/';
    if (full.starts_with(prefix)) return full.substr(prefix.size());
    return path.lexically_relative(root).generic_string();
}

Document loadDocument(SaveMount& mount) {
    Document document;
    for (const auto& entry : mount.listFiles()) document.addFile(entry.path, mount.read(entry.path));
    return document;
}

void storeDocument(const Document& document, SaveMount& mount) {
    for (const auto& name : document.fileNames()) {
        const auto& content = document.file(name).bytes();
        if (mount.read(name) != content) mount.write(name, content);
    }
    mount.commit();
}

}

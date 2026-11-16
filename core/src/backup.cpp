#include "savetinker/backup.hpp"

#include <algorithm>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>

#include "savetinker/checksum.hpp"

namespace st {

namespace fs = std::filesystem;

namespace {

constexpr const char* kRecordFile = "backup.json";
constexpr const char* kFilesDir = "files";

Bytes readWholeFile(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot open " + path.string());
    return Bytes(std::istreambuf_iterator<char>(stream), {});
}

void writeWholeFile(const fs::path& path, ByteView data) {
    fs::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!stream) throw std::runtime_error("cannot write " + path.string());
}

}

std::uint64_t BackupRecord::totalSize() const {
    std::uint64_t total = 0;
    for (const auto& file : files) total += file.size;
    return total;
}

BackupManager::BackupManager(fs::path root, Clock clock) : root_(std::move(root)), clock_(std::move(clock)) {}

std::expected<BackupRecord, std::string> BackupManager::create(SaveMount& mount, const SaveInfo& save,
                                                               const std::string& titleName,
                                                               const std::string& reason) {
    const auto now = std::chrono::floor<std::chrono::seconds>(clock_());
    const auto stamp = std::format("{:%Y%m%d-%H%M%S}", now);
    const fs::path slotDir = root_ / save.titleId / save.slotName();

    fs::path directory = slotDir / stamp;
    for (int suffix = 2; fs::exists(directory); ++suffix) directory = slotDir / std::format("{}-{}", stamp, suffix);

    BackupRecord record;
    record.directory = directory;
    record.titleId = save.titleId;
    record.titleName = titleName;
    record.slot = save.slotName();
    record.createdAt = std::format("{:%Y-%m-%dT%H:%M:%SZ}", now);
    record.reason = reason;

    try {
        for (const auto& entry : mount.listFiles()) {
            const Bytes content = mount.read(entry.path);
            writeWholeFile(directory / kFilesDir / entry.path, content);
            record.files.push_back({entry.path, content.size(), toHex(sha256(content))});
        }

        nlohmann::json files = nlohmann::json::array();
        for (const auto& file : record.files)
            files.push_back({{"path", file.path}, {"size", file.size}, {"sha256", file.sha256}});
        const nlohmann::json meta = {
            {"title_id", record.titleId}, {"title_name", record.titleName}, {"slot", record.slot},
            {"created_at", record.createdAt}, {"reason", record.reason}, {"files", files}};
        const auto text = meta.dump(2);
        writeWholeFile(directory / kRecordFile, ByteView(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
    } catch (const std::exception& error) {
        std::error_code ignored;
        fs::remove_all(directory, ignored);
        return std::unexpected(std::string("backup failed: ") + error.what());
    }
    return record;
}

std::optional<BackupRecord> BackupManager::readRecord(const fs::path& directory) const {
    std::ifstream stream(directory / kRecordFile);
    if (!stream) return std::nullopt;
    const auto meta = nlohmann::json::parse(stream, nullptr, false);
    if (meta.is_discarded() || !meta.is_object()) return std::nullopt;

    BackupRecord record;
    record.directory = directory;
    record.titleId = meta.value("title_id", "");
    record.titleName = meta.value("title_name", "");
    record.slot = meta.value("slot", "");
    record.createdAt = meta.value("created_at", "");
    record.reason = meta.value("reason", "");
    if (auto files = meta.find("files"); files != meta.end() && files->is_array()) {
        for (const auto& file : *files)
            record.files.push_back({file.value("path", ""), file.value("size", std::uint64_t{0}), file.value("sha256", "")});
    }
    return record;
}

std::vector<BackupRecord> BackupManager::list(const std::string& titleId, const std::string& slot) const {
    std::vector<BackupRecord> records;
    const fs::path titleDir = root_ / titleId;
    std::error_code error;
    if (!fs::is_directory(titleDir, error)) return records;
    for (const auto& slotEntry : fs::directory_iterator(titleDir)) {
        if (!slotEntry.is_directory()) continue;
        if (!slot.empty() && slotEntry.path().filename() != slot) continue;
        for (const auto& backupEntry : fs::directory_iterator(slotEntry.path()))
            if (auto record = readRecord(backupEntry.path())) records.push_back(std::move(*record));
    }
    std::ranges::sort(records, std::ranges::greater{}, [](const BackupRecord& r) { return r.directory.filename().string(); });
    return records;
}

std::expected<void, std::string> BackupManager::verify(const BackupRecord& record) const {
    for (const auto& file : record.files) {
        const fs::path path = record.directory / kFilesDir / file.path;
        try {
            if (toHex(sha256(readWholeFile(path))) != file.sha256)
                return std::unexpected("checksum mismatch: " + file.path);
        } catch (const std::exception&) {
            return std::unexpected("missing file: " + file.path);
        }
    }
    return {};
}

std::expected<void, std::string> BackupManager::restore(const BackupRecord& record, SaveMount& mount) const {
    if (auto valid = verify(record); !valid) return valid;
    std::vector<std::pair<std::string, fs::path>> files;
    for (const auto& file : record.files) files.emplace_back(file.path, record.directory / kFilesDir / file.path);
    return writeOver(files, mount);
}

std::expected<void, std::string> BackupManager::importFolder(const fs::path& folder, SaveMount& mount) const {
    std::vector<std::pair<std::string, fs::path>> files;
    std::error_code error;
    if (!fs::is_directory(folder, error)) return std::unexpected("not a folder: " + folder.string());
    // A SaveTinker, Checkpoint or JKSM backup folder holds the save files directly; ours keeps them under files/.
    const fs::path root = fs::is_directory(folder / kFilesDir, error) && fs::exists(folder / kRecordFile, error) ? folder / kFilesDir : folder;
    // Folders the app cannot open (a drive's System Volume Information, say) are skipped, not fatal.
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, error);
    if (error) return std::unexpected("cannot read " + root.string() + ": " + error.message());
    for (; it != fs::recursive_directory_iterator(); it.increment(error)) {
        if (error) return std::unexpected("cannot read " + root.string() + ": " + error.message());
        std::error_code ignored;
        if (it->is_regular_file(ignored)) files.emplace_back(relativePath(it->path(), root), it->path());
    }
    if (error) return std::unexpected("cannot read " + root.string() + ": " + error.message());
    if (files.empty()) return std::unexpected("the folder has no files");
    return writeOver(files, mount);
}

std::expected<BackupRecord, std::string> BackupManager::exportTo(const BackupRecord& record, const fs::path& folder) const {
    try {
        for (const auto& file : record.files) {
            const fs::path target = folder / file.path;
            fs::create_directories(target.parent_path());
            fs::copy_file(record.directory / kFilesDir / file.path, target, fs::copy_options::overwrite_existing);
        }
    } catch (const std::exception& error) {
        return std::unexpected(std::string("export failed: ") + error.what());
    }
    return record;
}

std::expected<void, std::string> BackupManager::writeOver(const std::vector<std::pair<std::string, fs::path>>& files,
                                                          SaveMount& mount) const {
    try {
        const auto existing = mount.listFiles();
        if (mount.singleFile()) {
            // A one-file save (a cartridge image, a PC save file) takes whatever the file was called elsewhere.
            if (files.size() != 1)
                return std::unexpected(std::format("this save is a single file, but the source holds {} files", files.size()));
            if (existing.size() != 1) return std::unexpected(std::string("the save file is missing"));
            const Bytes content = readWholeFile(files.front().second);
            if (mount.read(existing.front().path) != content) mount.write(existing.front().path, content);
            mount.commit();
            return {};
        }
        // Write every file before deleting anything, so a failure part way leaves the save readable.
        for (const auto& [path, source] : files) {
            const Bytes content = readWholeFile(source);
            const bool present = std::ranges::any_of(existing, [&](const SaveFileEntry& e) { return e.path == path; });
            if (present && mount.read(path) == content) continue;
            mount.write(path, content);
        }
        for (const auto& entry : existing) {
            const bool kept = std::ranges::any_of(files, [&](const auto& file) { return file.first == entry.path; });
            if (!kept) mount.remove(entry.path);
        }
        mount.commit();
    } catch (const std::exception& error) {
        return std::unexpected(std::string("restore failed: ") + error.what());
    }
    return {};
}

bool BackupManager::remove(const BackupRecord& record) const {
    std::error_code error;
    return fs::remove_all(record.directory, error) > 0;
}

std::size_t BackupManager::prune(const std::string& titleId, const std::string& slot, std::size_t keep) const {
    auto records = list(titleId, slot);
    std::size_t removed = 0;
    for (std::size_t i = keep; i < records.size(); ++i)
        if (remove(records[i])) ++removed;
    return removed;
}

}

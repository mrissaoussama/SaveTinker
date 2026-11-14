#include "savetinker/document.hpp"

#include <algorithm>
#include <ranges>
#include <stdexcept>

namespace st {

void Document::addFile(const std::string& name, Bytes content) {
    originals_[name] = content;
    files_[name] = ByteBuffer(std::move(content));
}

std::vector<std::string> Document::fileNames() const {
    std::vector<std::string> names;
    for (const auto& [name, _] : files_) names.push_back(name);
    return names;
}

const ByteBuffer& Document::file(const std::string& name) const {
    auto found = files_.find(name);
    if (found == files_.end()) throw std::out_of_range("no such file: " + name);
    return found->second;
}

ByteBuffer& Document::mutableFile(const std::string& name) {
    auto found = files_.find(name);
    if (found == files_.end()) throw std::out_of_range("no such file: " + name);
    return found->second;
}

void Document::write(const std::string& file, std::size_t offset, ByteView data) {
    const auto& buffer = this->file(file);
    if (offset > buffer.size() || data.size() > buffer.size() - offset)
        throw std::out_of_range("write out of range in " + file);
    splice(file, offset, data.size(), data);
}

void Document::splice(const std::string& file, std::size_t offset, std::size_t length, ByteView data) {
    const auto& buffer = this->file(file);
    if (offset > buffer.size() || length > buffer.size() - offset)
        throw std::out_of_range("splice out of range in " + file);
    Patch patch{file, offset, buffer.readBytes(offset, length), Bytes(data.begin(), data.end())};
    if (patch.before == patch.after) return;
    apply(patch, true);
    record(std::move(patch));
    notify(file);
}

void Document::setContent(const std::string& file, Bytes content) {
    splice(file, 0, this->file(file).size(), content);
}

void Document::apply(const Patch& patch, bool forward) {
    auto& bytes = mutableFile(patch.file).bytes();
    const Bytes& removed = forward ? patch.before : patch.after;
    const Bytes& inserted = forward ? patch.after : patch.before;
    const auto start = bytes.begin() + static_cast<std::ptrdiff_t>(patch.offset);
    if (removed.size() == inserted.size()) {
        std::ranges::copy(inserted, start);
    } else {
        bytes.erase(start, start + static_cast<std::ptrdiff_t>(removed.size()));
        bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(patch.offset), inserted.begin(), inserted.end());
    }
}

void Document::record(Patch patch) {
    if (open_) {
        auto& patches = open_->patches;
        if (!patches.empty()) {
            auto& last = patches.back();
            const bool sameSpot = last.file == patch.file && last.offset == patch.offset &&
                                  last.after.size() == patch.before.size() &&
                                  last.before.size() == last.after.size();
            if (sameSpot) {
                last.after = std::move(patch.after);
                return;
            }
        }
        patches.push_back(std::move(patch));
        return;
    }
    pushTransaction(Transaction{"Edit", {std::move(patch)}});
}

void Document::pushTransaction(Transaction transaction) {
    if (transaction.patches.empty()) return;
    history_.erase(history_.begin() + static_cast<std::ptrdiff_t>(cursor_), history_.end());
    if (savedCursor_ && *savedCursor_ > cursor_) savedCursor_.reset();
    history_.push_back(std::move(transaction));
    cursor_ = history_.size();
    if (history_.size() > historyLimit_) {
        history_.erase(history_.begin());
        --cursor_;
        if (savedCursor_) {
            if (*savedCursor_ == 0) savedCursor_.reset();
            else --*savedCursor_;
        }
    }
}

void Document::beginTransaction(const std::string& label) {
    if (depth_++ == 0) open_ = Transaction{label, {}};
}

void Document::commitTransaction() {
    if (depth_ == 0) throw std::logic_error("no open transaction");
    if (--depth_ > 0) return;
    auto finished = std::move(*open_);
    open_.reset();
    pushTransaction(std::move(finished));
}

void Document::rollbackTransaction() {
    if (depth_ == 0) throw std::logic_error("no open transaction");
    if (--depth_ > 0) return;
    auto abandoned = std::move(*open_);
    open_.reset();
    for (const auto& patch : abandoned.patches | std::views::reverse) {
        apply(patch, false);
        notify(patch.file);
    }
}

bool Document::undo() {
    if (!canUndo() || inTransaction()) return false;
    const auto& transaction = history_[--cursor_];
    for (const auto& patch : transaction.patches | std::views::reverse) {
        apply(patch, false);
        notify(patch.file);
    }
    return true;
}

bool Document::redo() {
    if (!canRedo() || inTransaction()) return false;
    const auto& transaction = history_[cursor_++];
    for (const auto& patch : transaction.patches) {
        apply(patch, true);
        notify(patch.file);
    }
    return true;
}

std::optional<std::string> Document::undoLabel() const {
    if (!canUndo()) return std::nullopt;
    return history_[cursor_ - 1].label;
}

std::optional<std::string> Document::redoLabel() const {
    if (!canRedo()) return std::nullopt;
    return history_[cursor_].label;
}

std::vector<std::string> Document::appliedLabels() const {
    std::vector<std::string> labels;
    for (std::size_t i = 0; i < cursor_; ++i) labels.push_back(history_[i].label);
    return labels;
}

bool Document::isDirty() const {
    return !savedCursor_ || *savedCursor_ != cursor_;
}

void Document::markSaved() {
    savedCursor_ = cursor_;
}

std::vector<ChangedRange> Document::changesSinceLoad() const {
    std::vector<ChangedRange> ranges;
    for (const auto& [name, buffer] : files_) {
        const Bytes& current = buffer.bytes();
        const Bytes& original = originals_.at(name);
        const std::size_t common = std::min(current.size(), original.size());
        std::size_t i = 0;
        while (i < common) {
            if (current[i] == original[i]) { ++i; continue; }
            const std::size_t start = i;
            while (i < common && current[i] != original[i]) ++i;
            ranges.push_back({name, start, i - start});
        }
        if (current.size() != original.size()) {
            const std::size_t longer = std::max(current.size(), original.size());
            if (!ranges.empty() && ranges.back().file == name && ranges.back().offset + ranges.back().length == common)
                ranges.back().length = longer - ranges.back().offset;
            else
                ranges.push_back({name, common, longer - common});
        }
    }
    return ranges;
}

int Document::addListener(ChangeListener listener) {
    const int id = nextListenerId_++;
    listeners_[id] = std::move(listener);
    return id;
}

void Document::removeListener(int id) {
    listeners_.erase(id);
}

void Document::notify(const std::string& file) {
    for (const auto& [_, listener] : listeners_) listener(file);
}

}

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "savetinker/byte_buffer.hpp"

namespace st {

struct Patch {
    std::string file;
    std::size_t offset = 0;
    Bytes before;
    Bytes after;
};

struct Transaction {
    std::string label;
    std::vector<Patch> patches;
};

struct ChangedRange {
    std::string file;
    std::size_t offset = 0;
    std::size_t length = 0;
};

class Document {
public:
    using ChangeListener = std::function<void(const std::string& file)>;
    using FileLoader = std::function<Bytes()>;

    explicit Document(std::size_t historyLimit = 1000) : historyLimit_(historyLimit) {}

    void addFile(const std::string& name, Bytes content);
    // The content is read the first time something asks for it, so files an editor never opens cost no memory.
    void addLazyFile(const std::string& name, std::uint64_t size, FileLoader loader);
    bool hasFile(const std::string& name) const { return files_.contains(name) || pending_.contains(name); }
    bool isLoaded(const std::string& name) const { return files_.contains(name); }
    std::vector<std::string> fileNames() const;
    std::uint64_t fileSize(const std::string& name) const;
    // Goes up whenever the file's content changes (edits, undo, redo, the original-content view).
    std::uint64_t revision(const std::string& name) const;
    const ByteBuffer& file(const std::string& name) const;
    // Files that were edited at some point since loading, even if their content is back to the original.
    std::vector<std::string> touchedFiles() const;
    std::uint64_t touchedSize() const;
    // True when an edit made some file longer or shorter, so offsets worked out now no longer fit the loaded bytes.
    bool sizeChanged() const;

    // Runs `fn` with every file showing its content as loaded, then restores the edits. `fn` must not write.
    template <class Fn>
    decltype(auto) withOriginals(Fn&& fn) {
        struct Swap {
            Document& document;
            explicit Swap(Document& d) : document(d) { document.swapOriginals(); }
            ~Swap() { document.swapOriginals(); }
        } swap(*this);
        return fn();
    }

    void write(const std::string& file, std::size_t offset, ByteView data);
    void splice(const std::string& file, std::size_t offset, std::size_t length, ByteView data);
    void setContent(const std::string& file, Bytes content);

    template <std::integral T>
    void writeValue(const std::string& file, std::size_t offset, T value, Endian endian = Endian::Little) {
        ByteBuffer scratch(Bytes(sizeof(T)));
        scratch.write<T>(0, value, endian);
        write(file, offset, scratch.view());
    }

    void beginTransaction(const std::string& label);
    void commitTransaction();
    void rollbackTransaction();
    bool inTransaction() const { return depth_ > 0; }

    bool canUndo() const { return cursor_ > 0; }
    bool canRedo() const { return cursor_ < history_.size(); }
    bool undo();
    bool redo();
    std::optional<std::string> undoLabel() const;
    std::optional<std::string> redoLabel() const;
    const std::vector<Transaction>& history() const { return history_; }
    std::vector<std::string> appliedLabels() const;

    bool isDirty() const;
    void markSaved();

    std::vector<ChangedRange> changesSinceLoad() const;

    int addListener(ChangeListener listener);
    void removeListener(int id);

private:
    ByteBuffer& mutableFile(const std::string& name);
    void swapOriginals();
    void apply(const Patch& patch, bool forward);
    void record(Patch patch);
    void pushTransaction(Transaction transaction);
    void notify(const std::string& file);

    mutable std::map<std::string, ByteBuffer> files_;
    mutable std::map<std::string, std::pair<std::uint64_t, FileLoader>> pending_;
    std::map<std::string, std::uint64_t> revisions_;
    std::map<std::string, Bytes> originals_;
    std::vector<Transaction> history_;
    std::size_t cursor_ = 0;
    std::optional<std::size_t> savedCursor_ = 0;
    std::optional<Transaction> open_;
    int depth_ = 0;
    std::size_t historyLimit_;
    std::map<int, ChangeListener> listeners_;
    int nextListenerId_ = 1;
};

class TransactionScope {
public:
    TransactionScope(Document& document, const std::string& label) : document_(document) {
        document_.beginTransaction(label);
    }
    ~TransactionScope() {
        if (!finished_) document_.rollbackTransaction();
    }
    void commit() {
        document_.commitTransaction();
        finished_ = true;
    }
    TransactionScope(const TransactionScope&) = delete;
    TransactionScope& operator=(const TransactionScope&) = delete;

private:
    Document& document_;
    bool finished_ = false;
};

}

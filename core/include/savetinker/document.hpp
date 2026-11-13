#pragma once

#include <cstddef>
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

// Holds the editable files of one save. Every write is recorded so it can be undone.
class Document {
public:
    using ChangeListener = std::function<void(const std::string& file)>;

    explicit Document(std::size_t historyLimit = 1000) : historyLimit_(historyLimit) {}

    void addFile(const std::string& name, Bytes content);
    bool hasFile(const std::string& name) const { return files_.contains(name); }
    std::vector<std::string> fileNames() const;
    const ByteBuffer& file(const std::string& name) const;

    void write(const std::string& file, std::size_t offset, ByteView data);
    // Replaces `length` bytes at `offset` with `data`, which may change the file size.
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

    bool isDirty() const;
    void markSaved();

    std::vector<ChangedRange> changesSinceLoad() const;

    int addListener(ChangeListener listener);
    void removeListener(int id);

private:
    ByteBuffer& mutableFile(const std::string& name);
    void apply(const Patch& patch, bool forward);
    void record(Patch patch);
    void pushTransaction(Transaction transaction);
    void notify(const std::string& file);

    std::map<std::string, ByteBuffer> files_;
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

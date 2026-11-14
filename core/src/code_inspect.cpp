#include "savetinker/code_inspect.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <optional>
#include <regex>

namespace st {

std::vector<std::string> splitLines(std::string_view text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto end = text.find('\n', start);
        std::string line(text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(std::move(line));
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    if (!lines.empty() && lines.back().empty() && !text.empty() && text.back() == '\n') lines.pop_back();
    return lines;
}

std::vector<DiffLine> diffLines(std::string_view oldText, std::string_view newText) {
    const auto before = splitLines(oldText);
    const auto after = splitLines(newText);

    std::size_t prefix = 0;
    while (prefix < before.size() && prefix < after.size() && before[prefix] == after[prefix]) ++prefix;
    std::size_t suffix = 0;
    while (suffix < before.size() - prefix && suffix < after.size() - prefix &&
           before[before.size() - 1 - suffix] == after[after.size() - 1 - suffix])
        ++suffix;

    const std::size_t rows = before.size() - prefix - suffix;
    const std::size_t cols = after.size() - prefix - suffix;
    std::vector<DiffLine> result;
    for (std::size_t i = 0; i < prefix; ++i) result.push_back({DiffKind::Same, before[i], i + 1, i + 1});

    constexpr std::size_t kMaxCells = 4'000'000;
    if (rows * cols <= kMaxCells) {
        std::vector<std::uint32_t> lcs((rows + 1) * (cols + 1), 0);
        auto at = [&](std::size_t r, std::size_t c) -> std::uint32_t& { return lcs[r * (cols + 1) + c]; };
        for (std::size_t r = rows; r-- > 0;)
            for (std::size_t c = cols; c-- > 0;)
                at(r, c) = before[prefix + r] == after[prefix + c] ? at(r + 1, c + 1) + 1 : std::max(at(r + 1, c), at(r, c + 1));
        std::size_t r = 0, c = 0;
        while (r < rows || c < cols) {
            if (r < rows && c < cols && before[prefix + r] == after[prefix + c]) {
                result.push_back({DiffKind::Same, before[prefix + r], prefix + r + 1, prefix + c + 1});
                ++r, ++c;
            } else if (r < rows && (c == cols || at(r + 1, c) >= at(r, c + 1))) {
                result.push_back({DiffKind::Removed, before[prefix + r], prefix + r + 1, 0});
                ++r;
            } else {
                result.push_back({DiffKind::Added, after[prefix + c], 0, prefix + c + 1});
                ++c;
            }
        }
    } else {
        for (std::size_t r = 0; r < rows; ++r) result.push_back({DiffKind::Removed, before[prefix + r], prefix + r + 1, 0});
        for (std::size_t c = 0; c < cols; ++c) result.push_back({DiffKind::Added, after[prefix + c], 0, prefix + c + 1});
    }

    for (std::size_t i = 0; i < suffix; ++i) {
        const std::size_t oldIndex = before.size() - suffix + i;
        const std::size_t newIndex = after.size() - suffix + i;
        result.push_back({DiffKind::Same, before[oldIndex], oldIndex + 1, newIndex + 1});
    }
    return result;
}

namespace {

constexpr std::array<std::string_view, 22> kKeywords = {
    "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto", "if",
    "in", "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while"};

constexpr std::array<std::string_view, 12> kBuiltins = {
    "st", "require", "print", "pairs", "ipairs", "string", "table", "math", "utf8", "tostring", "tonumber", "error"};

bool isIdentifierChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

// Returns the level of a long bracket "[==[" starting at `position`, or nothing.
std::optional<std::size_t> longBracketLevel(std::string_view text, std::size_t position) {
    if (position >= text.size() || text[position] != '[') return std::nullopt;
    std::size_t i = position + 1;
    while (i < text.size() && text[i] == '=') ++i;
    if (i < text.size() && text[i] == '[') return i - position - 1;
    return std::nullopt;
}

std::size_t findLongClose(std::string_view text, std::size_t from, std::size_t level) {
    const std::string closing = "]" + std::string(level, '=') + "]";
    const auto found = text.find(closing, from);
    return found == std::string_view::npos ? std::string_view::npos : found + closing.size();
}

}

std::vector<Token> LuaHighlighter::line(std::string_view text) {
    std::vector<Token> tokens;
    std::size_t i = 0;
    auto push = [&](TokenKind kind, std::size_t end) {
        if (end > i) tokens.push_back({kind, text.substr(i, end - i)});
        i = end;
    };

    if (block_ != Block::None) {
        const auto end = findLongClose(text, 0, blockLevel_);
        const auto kind = block_ == Block::Comment ? TokenKind::Comment : TokenKind::String;
        if (end == std::string_view::npos) {
            push(kind, text.size());
            return tokens;
        }
        push(kind, end);
        block_ = Block::None;
    }

    while (i < text.size()) {
        const char c = text[i];
        if (c == '-' && i + 1 < text.size() && text[i + 1] == '-') {
            if (auto level = longBracketLevel(text, i + 2)) {
                const auto end = findLongClose(text, i + 4 + *level, *level);
                if (end == std::string_view::npos) {
                    block_ = Block::Comment;
                    blockLevel_ = *level;
                    push(TokenKind::Comment, text.size());
                } else {
                    push(TokenKind::Comment, end);
                }
            } else {
                push(TokenKind::Comment, text.size());
            }
        } else if (auto level = longBracketLevel(text, i)) {
            const auto end = findLongClose(text, i + 2 + *level, *level);
            if (end == std::string_view::npos) {
                block_ = Block::String;
                blockLevel_ = *level;
                push(TokenKind::String, text.size());
            } else {
                push(TokenKind::String, end);
            }
        } else if (c == '"' || c == '\'') {
            std::size_t end = i + 1;
            while (end < text.size() && text[end] != c) end += text[end] == '\\' ? 2 : 1;
            push(TokenKind::String, std::min(end + 1, text.size()));
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            std::size_t end = i + 1;
            while (end < text.size() && (isIdentifierChar(text[end]) || text[end] == '.')) ++end;
            push(TokenKind::Number, end);
        } else if (isIdentifierChar(c)) {
            std::size_t end = i + 1;
            while (end < text.size() && isIdentifierChar(text[end])) ++end;
            const auto word = text.substr(i, end - i);
            const bool afterDot = i > 0 && (text[i - 1] == '.' || text[i - 1] == ':');
            TokenKind kind = TokenKind::Plain;
            if (std::ranges::find(kKeywords, word) != kKeywords.end()) kind = TokenKind::Keyword;
            else if (!afterDot && std::ranges::find(kBuiltins, word) != kBuiltins.end()) kind = TokenKind::Builtin;
            push(kind, end);
        } else {
            std::size_t end = i + 1;
            while (end < text.size() && !isIdentifierChar(text[end]) && text[end] != '"' && text[end] != '\'' &&
                   text[end] != '-' && text[end] != '[')
                ++end;
            push(TokenKind::Plain, end);
        }
    }
    return tokens;
}

std::vector<CodeFinding> summarizeScript(std::string_view code) {
    static const std::regex writes(R"(:(set|setBit|setBytes|setStr|setContent|splice|field|flag|text)\s*\()");
    static const std::regex requires_(R"(require\s*\(?\s*["']([^"']+)["'])");
    static const std::regex data(R"(st\.(data|labels)\s*\(\s*["']([^"']+)["'])");
    static const std::regex gameData(R"(st\.game_data\.(read|list)\s*\(\s*["']?([^"',)]*))");

    std::vector<CodeFinding> findings;
    const auto lines = splitLines(code);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const auto& line = lines[i];
        const auto commentStart = line.find("--");
        const std::string codePart = line.substr(0, commentStart);
        std::smatch match;
        if (std::regex_search(codePart, match, writes))
            findings.push_back({CodeFinding::Kind::Writes, i + 1, match[1].str()});
        if (std::regex_search(codePart, match, requires_))
            findings.push_back({CodeFinding::Kind::Requires, i + 1, match[1].str()});
        if (std::regex_search(codePart, match, data))
            findings.push_back({CodeFinding::Kind::ReadsData, i + 1, match[2].str()});
        if (std::regex_search(codePart, match, gameData))
            findings.push_back({CodeFinding::Kind::ReadsGameData, i + 1, match[2].str()});
    }
    return findings;
}

}

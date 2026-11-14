#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace st {

enum class DiffKind { Same, Added, Removed };

struct DiffLine {
    DiffKind kind;
    std::string text;
    std::size_t oldLine = 0;
    std::size_t newLine = 0;
};

std::vector<std::string> splitLines(std::string_view text);
std::vector<DiffLine> diffLines(std::string_view oldText, std::string_view newText);

enum class TokenKind { Plain, Keyword, Builtin, String, Number, Comment };

struct Token {
    TokenKind kind;
    std::string_view text;
};

// Tokenizes code line by line, carrying long comments and strings across lines.
class LuaHighlighter {
public:
    std::vector<Token> line(std::string_view text);

private:
    enum class Block { None, Comment, String };
    Block block_ = Block::None;
    std::size_t blockLevel_ = 0;
};

struct CodeFinding {
    enum class Kind { Writes, Requires, ReadsData, ReadsGameData, Note };
    Kind kind;
    std::size_t line;
    std::string detail;
};

// Lists save writes, required modules and data files used by a script.
std::vector<CodeFinding> summarizeScript(std::string_view code);

}

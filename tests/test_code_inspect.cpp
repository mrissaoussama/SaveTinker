#include <doctest.h>

#include <algorithm>

#include "savetinker/code_inspect.hpp"

using st::DiffKind;
using st::TokenKind;

TEST_CASE("line diff marks added and removed lines") {
    const auto diff = st::diffLines("a\nb\nc\n", "a\nx\nc\nd\n");
    REQUIRE(diff.size() == 5);
    CHECK(diff[0].kind == DiffKind::Same);
    CHECK(diff[1].kind == DiffKind::Removed);
    CHECK(diff[1].text == "b");
    CHECK(diff[2].kind == DiffKind::Added);
    CHECK(diff[2].text == "x");
    CHECK(diff[3].kind == DiffKind::Same);
    CHECK(diff[4].kind == DiffKind::Added);
    CHECK(diff[4].newLine == 4);
}

TEST_CASE("identical texts diff to nothing but context") {
    const auto diff = st::diffLines("one\r\ntwo", "one\ntwo");
    CHECK(std::ranges::all_of(diff, [](const st::DiffLine& l) { return l.kind == DiffKind::Same; }));
}

TEST_CASE("highlighter recognises lua tokens") {
    st::LuaHighlighter highlighter;
    const auto tokens = highlighter.line(R"(local x = st.save:file("a") -- note)");
    REQUIRE(tokens.size() >= 6);
    CHECK(tokens[0].kind == TokenKind::Keyword);
    CHECK(tokens[0].text == "local");
    CHECK(std::ranges::any_of(tokens, [](const st::Token& t) { return t.kind == TokenKind::Builtin && t.text == "st"; }));
    CHECK(std::ranges::any_of(tokens, [](const st::Token& t) { return t.kind == TokenKind::String && t.text == "\"a\""; }));
    CHECK(tokens.back().kind == TokenKind::Comment);
}

TEST_CASE("highlighter carries long comments across lines") {
    st::LuaHighlighter highlighter;
    CHECK(highlighter.line("x = 1 --[[ start").back().kind == TokenKind::Comment);
    const auto middle = highlighter.line("still comment");
    REQUIRE(middle.size() == 1);
    CHECK(middle[0].kind == TokenKind::Comment);
    const auto end = highlighter.line("end ]] y = 2");
    CHECK(end[0].kind == TokenKind::Comment);
    CHECK(std::ranges::any_of(end, [](const st::Token& t) { return t.kind == TokenKind::Number; }));
}

TEST_CASE("script summary lists writes, modules and data files") {
    const auto findings = st::summarizeScript(R"(
local layout = require("layout")
local items = st.labels("labels/items.json")
file:set(0, "u32", 1)
-- file:setBytes(0, "x")
)");
    REQUIRE(findings.size() == 3);
    CHECK(findings[0].kind == st::CodeFinding::Kind::Requires);
    CHECK(findings[0].detail == "layout");
    CHECK(findings[1].kind == st::CodeFinding::Kind::ReadsData);
    CHECK(findings[1].detail == "labels/items.json");
    CHECK(findings[2].kind == st::CodeFinding::Kind::Writes);
    CHECK(findings[2].line == 4);
}

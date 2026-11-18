//
// SPDX-License-Identifier: LGPL-3.0-only
//
// Copyright (C) 2026 Corey Pennycuff
//
// This file is part of Ghoti.io Lang-ecmascript.
//
// Ghoti.io Lang-ecmascript is free software: you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public License version 3
// as published by the Free Software Foundation.
//
// Ghoti.io Lang-ecmascript is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser
// General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License

// Regular expression literals (clauses 13.2.7.1 and 22.2.1.1): the pattern and
// flags of a literal are an early error unless regex's ECMAScript front end
// accepts them, in either builder and in every place a literal can be.

#include <gtest/gtest.h>

#include <pthread.h>

#include <functional>

#include "../parse_test_support.h"
#include "../test_helpers.h"

using glecma_test::Parse;

namespace {

struct Case {
  const char * source;
  uint32_t rule;     // 0 for accepted
  size_t offset;     // where the failure is, when it is a refusal
};

void Check(const Case & c, uint32_t internal) {
  for (bool strict : {false, true}) {
    auto parsed = Parse(c.source, strict, internal);
    if (c.rule == 0) {
      EXPECT_EQ(parsed.result, GLECMA_OK)
          << c.source << ": " << (parsed.failure.message ? parsed.failure.message : "");
      continue;
    }
    ASSERT_EQ(parsed.result, GLECMA_ERR_SYNTAX) << c.source;
    EXPECT_EQ(parsed.failure.rule, c.rule)
        << c.source << ": " << glecma_rule_name(parsed.failure.rule);
    EXPECT_EQ(parsed.failure.location.offset, c.offset) << c.source;
  }
}

void RunCases(std::initializer_list<Case> cases) {
  for (const Case & c : cases) {
    Check(c, 0);
    Check(c, GLECMA_PARSE_INTERNAL_CHECKING);
  }
}

}  // namespace

TEST(RegexpLiterals, TheRulesNameTheirClauses) {
  EXPECT_STREQ(glecma_rule_clause(GLECMA_RULE_REGEXP_PATTERN), "22.2.1.1");
  EXPECT_STREQ(glecma_rule_clause(GLECMA_RULE_REGEXP_FLAG_SET), "13.2.7.1");
}

TEST(RegexpLiterals, APatternTheGrammarRefusesIsAnEarlyError) {
  const uint32_t r = GLECMA_RULE_REGEXP_PATTERN;
  RunCases({
    {"x = /(/;", r, 5},
    {"x = /)/;", r, 5},
    {"x = /[/;", GLECMA_RULE_UNTERMINATED_REGEXP, 4},
    {"x = /a{2,1}/;", r, 6},
    {"x = /a**/;", r, 7},
    {"x = /?/;", r, 5},
    {"x = /(?<a>x)(?<a>y)/;", r, 12},
    {"x = /(?<a-b>x)/;", r, 8},
    {"x = /[z-a]/;", r, 6},
    {"x = /\\k<a>/u;", r, 5},
    {"x = /\\1/u;", r, 5},
    {"x = /{/u;", r, 5},
    {"x = /]/u;", r, 5},
    {"x = /\\-/u;", r, 5},
    {"x = /\\c/u;", r, 5},
    {"x = /\\p{Nosuch}/u;", r, 5},
    {"x = /\\u{110000}/u;", r, 5},
    {"x = /(?=a)*/u;", r, 10},
    {"x = /(?i-i:a)/;", r, 5},
    {"x = /[a&&&b]/v;", r, 10},
    {"x = /(?<=a)+/u;", r, 11},
    // In a function body the checking builder never builds, and the pattern is still read.
    {"function f() { return /(/; }", r, 23},
    {"x = () => /[z-a]/;", r, 12},
    {"class C { static { /(/; } }", r, 20},
    {"x = { m() { return /a{3,2}/; } };", r, 21},
    // The tokenizer's own refusals keep their codes.
    {"x = /a\n/;", GLECMA_RULE_UNTERMINATED_REGEXP, 4},
  });
}

TEST(RegexpLiterals, APatternTheGrammarAllowsIsAccepted) {
  RunCases({
    {"x = /a+b*c?/;", 0, 0},
    {"x = /(?<year>\\d{4})-(?<month>\\d{2})/u;", 0, 0},
    {"x = /(?<a>x)|(?<a>y)/;", 0, 0},
    {"x = /\\k<a>(?<a>x)/;", 0, 0},
    {"x = /\\p{L}\\P{Lu}\\p{Script=Greek}\\p{Emoji}/u;", 0, 0},
    {"x = /[\\p{RGI_Emoji}--\\q{abc|d}]/v;", 0, 0},
    {"x = /[[a-z]&&[^aeiou]]/v;", 0, 0},
    {"x = /(?<=a)b(?<!c)d(?=e)f(?!g)/;", 0, 0},
    {"x = /(?i:a)b(?-i:c)/;", 0, 0},
    {"x = /\\u{1F600}/u;", 0, 0},
    {"x = /\\ud83d\\ude00/;", 0, 0},
    {"x = /[\\d-x]{,3}\\8\\a/;", 0, 0},   // Annex B, without the flag
    {"x = /{/; y = /]/; z = /\\-/; w = /a{1/;", 0, 0},
    {"x = /(?:a|b)*?c+?/dgimsuy;", 0, 0},
    {"x = /./dgimsvy;", 0, 0},
    {"x = /[/]/; y = /[\\]/]/;", 0, 0},
    {"x = a / b / c;", 0, 0},
    {"x = /a/ / /b/;", 0, 0},
    {"x = /(?:)/;", 0, 0},
    {"x = /^$\\b\\B/;", 0, 0},
  });
}

TEST(RegexpLiterals, TheFlagsAreCheckedAsAnAlphabetAndAsASet) {
  const uint32_t f = GLECMA_RULE_REGEXP_FLAG_SET;
  RunCases({
    {"x = /a/x;", f, 7},
    {"x = /a/gg;", f, 7},
    {"x = /a/yy;", f, 7},
    {"x = /a/ii;", f, 7},
    {"x = /a/uu;", f, 7},
    {"x = /a/uv;", f, 7},
    {"x = /a/vu;", f, 7},
    {"x = /a/dgimsuvyd;", f, 7},
    {"x = /a/gimsyd;", 0, 0},
    {"x = /a/v; y = /a/u;", 0, 0},
    {"x = /a/\\u0067;", GLECMA_RULE_REGEXP_FLAGS, 7},
    {"x = /a/\xc3\xa9;", f, 7},   // a letter outside the alphabet
  });
}

// A UTF-16 source can hold a lone surrogate, which a pattern may contain.
TEST(RegexpLiterals, ALoneSurrogateInAPatternIsNotAnError) {
  const char16_t units[] = {u'x', u'=', u'/', u'[', 0xD800, u'-', 0xDBFF, u']', u'/', u';'};
  const char16_t flagged[] = {u'x', u'=', u'/', 0xDC00, u'/', u'u', u';'};
  for (const auto & text : {std::u16string(units, 10), std::u16string(flagged, 7)}) {
    GLECMA_Source * source = nullptr;
    ASSERT_EQ(glecma_source_create_utf16(reinterpret_cast<const uint16_t *>(text.data()),
                  text.size(), nullptr, nullptr, nullptr, &source),
        GLECMA_OK);
    GLECMA_Tree * tree = nullptr;
    GLECMA_Failure failure = GLECMA_FAILURE_INIT;
    EXPECT_EQ(glecma_parse(source, nullptr, nullptr, &failure, &tree), GLECMA_OK)
        << failure.message;
    glecma_tree_destroy(tree);
    glecma_source_release(source);
  }
  // A pair is one code point: an astral character in a class range is a range of code points.
  EXPECT_EQ(Parse("x = /[\xf0\x9f\x98\x80-\xf0\x9f\x98\x81]/u;").result, GLECMA_OK);
  // Without the flag the same range is of code units and runs backwards (clause B.1.2).
  EXPECT_EQ(Parse("x = /[\xf0\x9f\x98\x80-\xf0\x9f\x98\x81]/;").result, GLECMA_ERR_SYNTAX);
}

TEST(RegexpLiterals, APatternDeeperThanTheFrontEndAllowsIsALimitAndNeverAFault) {
  const std::string deep = "x = /" + std::string(300, '(') + "a" + std::string(300, ')') + "/;";
  auto parsed = Parse(deep);
  EXPECT_EQ(parsed.result, GLECMA_ERR_LIMIT);
  EXPECT_STREQ(parsed.failure.limit, "nesting");
  const std::string ok = "x = /" + std::string(120, '(') + "a" + std::string(120, ')') + "/;";
  EXPECT_EQ(Parse(ok).result, GLECMA_OK);
  // On a thread with little stack the literal is refused for the stack, before the
  // front end runs, whatever the grammar's own cap.
  GLECMA_Limits limits = GLECMA_LIMITS_INIT;
  limits.native_stack_bytes = 100 * 1024;
  auto tight = Parse(ok, false, 0, &limits);
  EXPECT_EQ(tight.result, GLECMA_ERR_LIMIT);
  EXPECT_STREQ(tight.failure.limit, "native_stack_bytes");
}

TEST(RegexpLiterals, EveryAllocationOfAValidationMayFailWithNothingLeaked) {
  const std::string text = "x = /(?<a>b+)[c-e]\\k<a>/u; y = /z{2,3}/gi;";
  size_t total = 0;
  {
    glecma_test::CountingAllocator counting;
    GLECMA_Source * source = nullptr;
    ASSERT_EQ(glecma_source_create_utf8(text.data(), text.size(), nullptr, nullptr,
                  nullptr, &source),
        GLECMA_OK);
    GLECMA_Tree * tree = nullptr;
    GLECMA_Failure failure = GLECMA_FAILURE_INIT;
    ASSERT_EQ(glecma_parse_internal(source, nullptr, counting.get(), 0, &failure, &tree),
        GLECMA_OK);
    total = counting.calls();
    glecma_tree_destroy(tree);
    EXPECT_EQ(counting.live(), 0u);
    glecma_source_release(source);
  }
  GLECMA_Source * source = nullptr;
  ASSERT_EQ(glecma_source_create_utf8(text.data(), text.size(), nullptr, nullptr,
                nullptr, &source),
      GLECMA_OK);
  for (size_t i = 0; i < total; i++) {
    glecma_test::CountingAllocator counting;
    counting.fail_at(i);
    GLECMA_Tree * tree = nullptr;
    GLECMA_Failure failure = GLECMA_FAILURE_INIT;
    EXPECT_EQ(glecma_parse_internal(source, nullptr, counting.get(), 0, &failure, &tree),
        GLECMA_ERR_OOM)
        << "allocation " << i << " of " << total;
    EXPECT_EQ(counting.live(), 0u) << "allocation " << i;
  }
  glecma_source_release(source);
}

int main(int argc, char * * argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

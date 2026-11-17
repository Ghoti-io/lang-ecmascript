// SPDX-License-Identifier: LGPL-3.0-only
//
// The tokenizer's literals: numeric literals (with Annex B's legacy forms and
// the strict-mode rules), string literals and their escapes, templates with
// cooked and raw text, and regular expression literals. Every case runs in
// UTF-8 and UTF-16 (Lex). ECMA-262 clause numbers are the 16th edition's.

#include "../lex_test_support.h"

#include "../test_helpers.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace glecma_test;

namespace {

constexpr uint32_t kStrict = GLECMA_LEXF_STRICT;
constexpr uint32_t kModule = GLECMA_LEXF_MODULE;

uint32_t R(GLECMA_Rule rule) { return static_cast<uint32_t>(rule); }

struct NumberCase {
  const char * text;
  GLECMA_TokenKind kind;
  uint32_t flags;
};

struct BadCase {
  const char * text;
  GLECMA_Rule rule;
  size_t offset;  // Where the failure's location is.
};

}  // namespace

// 12.9.3: numeric literals.
TEST(Numbers, ValidLiterals) {
  const NumberCase cases[] = {
      {"0", GLECMA_TOK_NUMBER, 0}, {"1", GLECMA_TOK_NUMBER, 0},
      {"123", GLECMA_TOK_NUMBER, 0}, {"1_000", GLECMA_TOK_NUMBER, 0},
      {"1_0_0", GLECMA_TOK_NUMBER, 0}, {"0.5", GLECMA_TOK_NUMBER, 0},
      {".5", GLECMA_TOK_NUMBER, 0}, {"5.", GLECMA_TOK_NUMBER, 0},
      {"5.e3", GLECMA_TOK_NUMBER, 0}, {"1e10", GLECMA_TOK_NUMBER, 0},
      {"1E+10", GLECMA_TOK_NUMBER, 0}, {"1e-10", GLECMA_TOK_NUMBER, 0},
      {"1_0.0_1e1_0", GLECMA_TOK_NUMBER, 0}, {".5e-5", GLECMA_TOK_NUMBER, 0},
      {"0e0", GLECMA_TOK_NUMBER, 0}, {"0.0", GLECMA_TOK_NUMBER, 0},
      {"0x1F", GLECMA_TOK_NUMBER, 0}, {"0XaBc", GLECMA_TOK_NUMBER, 0},
      {"0x1_F", GLECMA_TOK_NUMBER, 0}, {"0b101", GLECMA_TOK_NUMBER, 0},
      {"0B1_0", GLECMA_TOK_NUMBER, 0}, {"0o17", GLECMA_TOK_NUMBER, 0},
      {"0O7_7", GLECMA_TOK_NUMBER, 0}, {"0x1e", GLECMA_TOK_NUMBER, 0},
      {"0n", GLECMA_TOK_BIGINT, 0}, {"123n", GLECMA_TOK_BIGINT, 0},
      {"1_0n", GLECMA_TOK_BIGINT, 0}, {"0x1Fn", GLECMA_TOK_BIGINT, 0},
      {"0b1n", GLECMA_TOK_BIGINT, 0}, {"0o7n", GLECMA_TOK_BIGINT, 0},
      // Annex B: legacy octal, and decimal with a leading zero.
      {"017", GLECMA_TOK_NUMBER, GLECMA_TOKF_LEGACY_OCTAL},
      {"00", GLECMA_TOK_NUMBER, GLECMA_TOKF_LEGACY_OCTAL},
      {"0777", GLECMA_TOK_NUMBER, GLECMA_TOKF_LEGACY_OCTAL},
      {"08", GLECMA_TOK_NUMBER, GLECMA_TOKF_NON_OCTAL_DECIMAL},
      {"089", GLECMA_TOK_NUMBER, GLECMA_TOKF_NON_OCTAL_DECIMAL},
      {"08.5", GLECMA_TOK_NUMBER, GLECMA_TOKF_NON_OCTAL_DECIMAL},
      {"09e1", GLECMA_TOK_NUMBER, GLECMA_TOKF_NON_OCTAL_DECIMAL},
      {"019.5_5", GLECMA_TOK_NUMBER, GLECMA_TOKF_NON_OCTAL_DECIMAL},
  };
  for (const NumberCase & c : cases) {
    Lexed lexed = Lex(c.text);
    ASSERT_EQ(lexed.result, GLECMA_OK) << c.text;
    ASSERT_EQ(lexed.tokens.size(), 2u) << c.text;
    EXPECT_EQ(lexed.tokens[0].kind, c.kind) << c.text;
    EXPECT_EQ(lexed.tokens[0].flags, c.flags) << c.text;
    EXPECT_EQ(lexed.tokens[0].text, c.text);
  }
}

// 12.9.3: where a literal ends.
TEST(Numbers, WhereALiteralEnds) {
  // The legacy octal has no fraction: `.5` is a second literal, and a member
  // access reads as one.
  EXPECT_EQ(Sig(Lex("07.5")), "NUMBER NUMBER");
  EXPECT_EQ(Sig(Lex("07.toString")), "NUMBER . NAME");
  EXPECT_EQ(Sig(Lex("1..toString()")), "NUMBER . NAME ( )");
  EXPECT_EQ(Sig(Lex("1.5.toFixed")), "NUMBER . NAME");
  EXPECT_EQ(Sig(Lex("0x1.5")), "NUMBER NUMBER");
  EXPECT_EQ(Sig(Lex("1-2")), "NUMBER - NUMBER");
  EXPECT_EQ(Sig(Lex("1e3+2")), "NUMBER + NUMBER");
  EXPECT_EQ(Sig(Lex("0x1e+2")), "NUMBER + NUMBER");
}

TEST(Numbers, InvalidLiterals) {
  const BadCase cases[] = {
      {"0x", GLECMA_RULE_NUMERIC_EXPECTED_DIGIT, 2},
      {"0b", GLECMA_RULE_NUMERIC_EXPECTED_DIGIT, 2},
      {"0o", GLECMA_RULE_NUMERIC_EXPECTED_DIGIT, 2},
      {"0b2", GLECMA_RULE_NUMERIC_EXPECTED_DIGIT, 2},
      {"0o8", GLECMA_RULE_NUMERIC_EXPECTED_DIGIT, 2},
      {"0xg", GLECMA_RULE_NUMERIC_EXPECTED_DIGIT, 2},
      {"1e", GLECMA_RULE_NUMERIC_EXPECTED_DIGIT, 2},
      {"1e+", GLECMA_RULE_NUMERIC_EXPECTED_DIGIT, 3},
      {"1E-a", GLECMA_RULE_NUMERIC_EXPECTED_DIGIT, 3},
      {".5e", GLECMA_RULE_NUMERIC_EXPECTED_DIGIT, 3},
      {"5.e", GLECMA_RULE_NUMERIC_EXPECTED_DIGIT, 3},
      {"0b12", GLECMA_RULE_NUMERIC_TRAILING, 3},
      {"3in", GLECMA_RULE_NUMERIC_TRAILING, 1},
      {"3a", GLECMA_RULE_NUMERIC_TRAILING, 1},
      {"0x1g", GLECMA_RULE_NUMERIC_TRAILING, 3},
      {"1.e3a", GLECMA_RULE_NUMERIC_TRAILING, 4},
      {"3\\u0061", GLECMA_RULE_NUMERIC_TRAILING, 1},
      {"3\xCE\xB1", GLECMA_RULE_NUMERIC_TRAILING, 1},
      {"1.toString", GLECMA_RULE_NUMERIC_TRAILING, 2},
      {"3_0a", GLECMA_RULE_NUMERIC_TRAILING, 3},
      {"0b1n2", GLECMA_RULE_NUMERIC_TRAILING, 4},
      {"1_", GLECMA_RULE_NUMERIC_SEPARATOR, 1},
      {"1__0", GLECMA_RULE_NUMERIC_SEPARATOR, 1},
      {"0x_1", GLECMA_RULE_NUMERIC_SEPARATOR, 2},
      {"0b_1", GLECMA_RULE_NUMERIC_SEPARATOR, 2},
      {"1_.5", GLECMA_RULE_NUMERIC_SEPARATOR, 1},
      {"1._5", GLECMA_RULE_NUMERIC_SEPARATOR, 2},
      {"1_e5", GLECMA_RULE_NUMERIC_SEPARATOR, 1},
      {"1e_5", GLECMA_RULE_NUMERIC_SEPARATOR, 2},
      {"1e5_", GLECMA_RULE_NUMERIC_SEPARATOR, 3},
      {"1_n", GLECMA_RULE_NUMERIC_SEPARATOR, 1},
      {"0_1", GLECMA_RULE_NUMERIC_SEPARATOR_LEGACY, 1},
      {"01_1", GLECMA_RULE_NUMERIC_SEPARATOR_LEGACY, 2},
      {"08_9", GLECMA_RULE_NUMERIC_SEPARATOR_LEGACY, 2},
      {"1.5n", GLECMA_RULE_NUMERIC_BIGINT, 3},
      {"1e3n", GLECMA_RULE_NUMERIC_BIGINT, 3},
      {".5n", GLECMA_RULE_NUMERIC_BIGINT, 2},
      {"01n", GLECMA_RULE_NUMERIC_BIGINT, 2},
      {"00n", GLECMA_RULE_NUMERIC_BIGINT, 2},
      {"08n", GLECMA_RULE_NUMERIC_BIGINT, 2},
      {"0.0n", GLECMA_RULE_NUMERIC_BIGINT, 3},
  };
  for (const BadCase & c : cases) {
    Lexed lexed = Lex(c.text);
    EXPECT_EQ(lexed.result, GLECMA_ERR_SYNTAX) << c.text;
    EXPECT_EQ(lexed.failure.rule, R(c.rule)) << c.text;
    EXPECT_EQ(lexed.failure.location.offset, c.offset) << c.text;
    EXPECT_STRNE(lexed.failure.message == nullptr ? "" : lexed.failure.message,
        "")
        << c.text;
  }
}

// 12.9.3: legacy octal and leading-zero decimals are errors in strict code,
// the Script goal's directive and the Module goal alike.
TEST(Numbers, StrictModeRefusesTheLegacyForms) {
  for (uint32_t mode : {kStrict, kModule}) {
    for (const char * text : {"017", "00", "0777"}) {
      Lexed lexed = Lex(text, mode);
      EXPECT_EQ(lexed.failure.rule, R(GLECMA_RULE_NUMERIC_LEGACY_OCTAL_STRICT))
          << text;
      EXPECT_EQ(lexed.failure.location.offset, 0u);
    }
    for (const char * text : {"08", "089", "08.5", "09e1"}) {
      Lexed lexed = Lex(text, mode);
      EXPECT_EQ(lexed.failure.rule,
          R(GLECMA_RULE_NUMERIC_NON_OCTAL_DECIMAL_STRICT))
          << text;
    }
    // Everything else is as in sloppy code.
    for (const char * text : {"0", "0.5", "0n", "0x7", "10", "1_0", "0e1"}) {
      EXPECT_EQ(Lex(text, mode).result, GLECMA_OK) << text;
    }
  }
  // The error is at the literal, after other tokens too.
  Lexed later = Lex("a + 017", kStrict);
  EXPECT_EQ(later.failure.location.offset, 4u);
  EXPECT_EQ(later.failure.location.column, 4u);
}

// The strictness of the code can change after a token is read: a directive
// prologue makes a function strict.
TEST(Numbers, TheParserCanMakeTheLexerStrict) {
  GLECMA_Source * source = nullptr;
  ASSERT_EQ(glecma_source_create_utf8("017 017", 7, nullptr, nullptr, nullptr,
                &source),
      GLECMA_OK);
  GLECMA_Lexer lexer;
  ASSERT_EQ(glecma_lexer_open(&lexer, source, nullptr, 0, nullptr), GLECMA_OK);
  GLECMA_Token t;
  ASSERT_EQ(glecma_lexer_next(&lexer, GLECMA_GOAL_DIV, &t), GLECMA_OK);
  EXPECT_NE(t.flags & GLECMA_TOKF_LEGACY_OCTAL, 0u);
  glecma_lexer_set_strict(&lexer, true);
  EXPECT_EQ(glecma_lexer_next(&lexer, GLECMA_GOAL_DIV, &t), GLECMA_ERR_SYNTAX);
  glecma_source_release(source);
  // A module stays strict whatever the parser says.
  ASSERT_EQ(glecma_source_create_utf8("017", 3, nullptr, nullptr, nullptr,
                &source),
      GLECMA_OK);
  ASSERT_EQ(glecma_lexer_open(&lexer, source, nullptr, kModule, nullptr),
      GLECMA_OK);
  glecma_lexer_set_strict(&lexer, false);
  EXPECT_EQ(glecma_lexer_next(&lexer, GLECMA_GOAL_DIV, &t), GLECMA_ERR_SYNTAX);
  glecma_source_release(source);
}

// 12.9.4: string literals and their values.
TEST(Strings, Values) {
  struct Case {
    const char * text;
    const char * value;
  };
  const Case cases[] = {
      {"'a'", "a"}, {"\"a\"", "a"}, {"''", ""}, {"\"\"", ""},
      {"'\\''", "'"}, {"\"\\\"\"", "\""}, {"'\\\\'", "\\"},
      {"\"\\n\\t\\r\\b\\f\\v\"", "\n\t\r\b\f\v"},
      {"'\"'", "\""}, {"\"'\"", "'"},
      {"'\\x41'", "A"}, {"'\\u0041'", "A"}, {"'\\u{41}'", "A"},
      {"'\\u{0000041}'", "A"},
      {"'\\u{1F600}'", "\xF0\x9F\x98\x80"},
      {"'\\uD83D\\uDE00'", "\xF0\x9F\x98\x80"},
      {"'\\uD800'", "\\uD800"}, {"'\\uDC00'", "\\uDC00"},
      {"'\\u{D800}'", "\\uD800"},
      {"'a\\\nb'", "ab"}, {"'a\\\r\nb'", "ab"}, {"'a\\\rb'", "ab"},
      {"'a\\\xE2\x80\xA8" "b'", "ab"}, {"'a\\\xE2\x80\xA9" "b'", "ab"},
      {"'a\\qb'", "aqb"}, {"'\\\xC3\xA9'", "\xC3\xA9"},
      {"'\\\xF0\x9F\x98\x80'", "\xF0\x9F\x98\x80"},
      {"'\xE2\x80\xA8'", "\xE2\x80\xA8"},  // LS and PS are allowed unescaped
      {"'\xE2\x80\xA9'", "\xE2\x80\xA9"},
      {"'\xC3\xA9\xF0\x9F\x98\x80'", "\xC3\xA9\xF0\x9F\x98\x80"},
  };
  for (const Case & c : cases) {
    Lexed lexed = Lex(c.text);
    ASSERT_EQ(lexed.result, GLECMA_OK) << c.text;
    ASSERT_EQ(lexed.tokens[0].kind, GLECMA_TOK_STRING) << c.text;
    EXPECT_EQ(ValueOf(c.text, 0), c.value) << c.text;
  }
  // \0 is a NUL, which std::string(c_str) cannot carry above.
  EXPECT_EQ(ValueOf("'\\0'", 0), std::string("\0", 1));
  EXPECT_EQ(ValueOf("'a\\0b'", 0), std::string("a\0b", 3));
}

// B.1.2: legacy octal escapes and \8 \9 in sloppy code.
TEST(Strings, LegacyEscapesInSloppyCode) {
  struct Case {
    const char * text;
    std::string value;
    uint32_t flags;
  };
  const Case cases[] = {
      {"'\\7'", "\x07", GLECMA_TOKF_LEGACY_OCTAL},
      {"'\\07'", "\x07", GLECMA_TOKF_LEGACY_OCTAL},
      {"'\\007'", "\x07", GLECMA_TOKF_LEGACY_OCTAL},
      {"'\\101'", "A", GLECMA_TOKF_LEGACY_OCTAL},
      {"'\\377'", "\xC3\xBF", GLECMA_TOKF_LEGACY_OCTAL},
      {"'\\400'", " 0", GLECMA_TOKF_LEGACY_OCTAL},  // \40 then "0"
      {"'\\00'", std::string("\0", 1), GLECMA_TOKF_LEGACY_OCTAL},
      {"'\\08'", std::string("\0" "8", 2), GLECMA_TOKF_LEGACY_OCTAL},
      {"'\\8'", "8", GLECMA_TOKF_NON_OCTAL_DECIMAL},
      {"'\\9'", "9", GLECMA_TOKF_NON_OCTAL_DECIMAL},
      {"'\\18'", std::string("\x01" "8"), GLECMA_TOKF_LEGACY_OCTAL},
      {"'\\0'", std::string("\0", 1), 0},
  };
  for (const Case & c : cases) {
    Lexed lexed = Lex(c.text);
    ASSERT_EQ(lexed.result, GLECMA_OK) << c.text;
    EXPECT_EQ(lexed.tokens[0].flags, c.flags) << c.text;
    EXPECT_EQ(ValueOf(c.text, 0), c.value) << c.text;
  }
}

TEST(Strings, StrictCodeRefusesTheLegacyEscapes) {
  for (uint32_t mode : {kStrict, kModule}) {
    for (const char * text : {"'\\7'", "'\\07'", "'\\00'", "'\\08'", "'a\\1'"}) {
      Lexed lexed = Lex(text, mode);
      EXPECT_EQ(lexed.failure.rule,
          R(GLECMA_RULE_ESCAPE_LEGACY_OCTAL_STRICT))
          << text;
    }
    for (const char * text : {"'\\8'", "'\\9'"}) {
      Lexed lexed = Lex(text, mode);
      EXPECT_EQ(lexed.failure.rule,
          R(GLECMA_RULE_ESCAPE_NON_OCTAL_DECIMAL_STRICT))
          << text;
    }
    // \0 not followed by a digit is a plain NUL in strict code too.
    EXPECT_EQ(Lex("'\\0'", mode).result, GLECMA_OK);
    EXPECT_EQ(Lex("'\\0a'", mode).result, GLECMA_OK);
  }
  // The error is at the backslash.
  Lexed lexed = Lex("'ab\\7'", kStrict);
  EXPECT_EQ(lexed.failure.location.offset, 3u);
  EXPECT_EQ(lexed.failure.location.column, 3u);
}

TEST(Strings, InvalidStrings) {
  const BadCase cases[] = {
      {"'abc", GLECMA_RULE_UNTERMINATED_STRING, 0},
      {"\"abc", GLECMA_RULE_UNTERMINATED_STRING, 0},
      {"'a\nb'", GLECMA_RULE_UNTERMINATED_STRING, 0},
      {"x = 'a\rb'", GLECMA_RULE_UNTERMINATED_STRING, 4},
      {"'a\\", GLECMA_RULE_UNTERMINATED_STRING, 0},
      {"'\\x4'", GLECMA_RULE_ESCAPE_HEX, 1},
      {"'\\xZ1'", GLECMA_RULE_ESCAPE_HEX, 1},
      {"'ab\\x'", GLECMA_RULE_ESCAPE_HEX, 3},
      {"'\\u12'", GLECMA_RULE_ESCAPE_UNICODE, 1},
      {"'\\uZZZZ'", GLECMA_RULE_ESCAPE_UNICODE, 1},
      {"'\\u'", GLECMA_RULE_ESCAPE_UNICODE, 1},
      {"'\\u{}'", GLECMA_RULE_ESCAPE_CODE_POINT, 1},
      {"'\\u{110000}'", GLECMA_RULE_ESCAPE_CODE_POINT, 1},
      {"'\\u{12'", GLECMA_RULE_ESCAPE_CODE_POINT, 1},
      {"'\\u{zz}'", GLECMA_RULE_ESCAPE_CODE_POINT, 1},
      {"'\\u{FFFFFFFFFFFFFFFF}'", GLECMA_RULE_ESCAPE_CODE_POINT, 1},
  };
  for (const BadCase & c : cases) {
    Lexed lexed = Lex(c.text);
    EXPECT_EQ(lexed.result, GLECMA_ERR_SYNTAX) << c.text;
    EXPECT_EQ(lexed.failure.rule, R(c.rule)) << c.text;
    EXPECT_EQ(lexed.failure.location.offset, c.offset) << c.text;
  }
  // The largest code point is fine, with any number of leading zeros.
  EXPECT_EQ(Lex("'\\u{10FFFF}'").result, GLECMA_OK);
  EXPECT_EQ(Lex("'\\u{00000000000000000010FFFF}'").result, GLECMA_OK);
}

// 12.9.6: templates.
TEST(Templates, PartsAndValues) {
  Lexed lexed = Lex("`abc`");
  ASSERT_EQ(lexed.tokens.size(), 2u);
  EXPECT_EQ(lexed.tokens[0].kind, GLECMA_TOK_TEMPLATE);
  EXPECT_EQ(lexed.tokens[0].part, GLECMA_TEMPLATE_NO_SUBSTITUTION);
  EXPECT_EQ(ValueOf("`abc`", 0), "abc");
  EXPECT_EQ(ValueOf("`abc`", 0, GLECMA_VALUE_RAW), "abc");

  const std::string text = "`a${b}c${d}e`";
  lexed = Lex(text);
  ASSERT_EQ(lexed.result, GLECMA_OK);
  ASSERT_EQ(Sig(lexed), "TEMPLATE NAME TEMPLATE NAME TEMPLATE");
  EXPECT_EQ(lexed.tokens[0].part, GLECMA_TEMPLATE_HEAD);
  EXPECT_EQ(lexed.tokens[2].part, GLECMA_TEMPLATE_MIDDLE);
  EXPECT_EQ(lexed.tokens[4].part, GLECMA_TEMPLATE_TAIL);
  EXPECT_EQ(lexed.tokens[0].text, "`a${");
  EXPECT_EQ(lexed.tokens[2].text, "}c${");
  EXPECT_EQ(lexed.tokens[4].text, "}e`");
  EXPECT_EQ(ValueOf(text, 0), "a");
  EXPECT_EQ(ValueOf(text, 2), "c");
  EXPECT_EQ(ValueOf(text, 4), "e");

  // Nested, to any depth the driver follows; braces in a substitution do not
  // end it.
  lexed = Lex("`a${`b${c}`}d` `x${ {y:1}.y }z`");
  ASSERT_EQ(lexed.result, GLECMA_OK);
  EXPECT_EQ(Sig(lexed),
      "TEMPLATE TEMPLATE NAME TEMPLATE TEMPLATE TEMPLATE { NAME : NUMBER } . "
      "NAME TEMPLATE");
}

TEST(Templates, DollarAndEscapedDelimiters) {
  EXPECT_EQ(ValueOf("`$`", 0), "$");
  EXPECT_EQ(ValueOf("`$a`", 0), "$a");
  EXPECT_EQ(ValueOf("`a$`", 0), "a$");
  EXPECT_EQ(ValueOf("`$$`", 0), "$$");
  // An escaped `$` and an escaped backtick do not begin or end anything.
  Lexed lexed = Lex("`\\${x}`");
  ASSERT_EQ(lexed.result, GLECMA_OK);
  EXPECT_EQ(Sig(lexed), "TEMPLATE");
  EXPECT_EQ(ValueOf("`\\${x}`", 0), "${x}");
  EXPECT_EQ(ValueOf("`\\${x}`", 0, GLECMA_VALUE_RAW), "\\${x}");
  EXPECT_EQ(ValueOf("`\\``", 0), "`");
  EXPECT_EQ(ValueOf("`\\``", 0, GLECMA_VALUE_RAW), "\\`");
  EXPECT_EQ(ValueOf("`\\\\`", 0, GLECMA_VALUE_RAW), "\\\\");
  EXPECT_EQ(Sig(Lex("`\\\\` x")), "TEMPLATE NAME");
}

// 12.9.6: TV and TRV read CR and CRLF as LF; LS and PS are kept.
TEST(Templates, LineTerminators) {
  EXPECT_EQ(ValueOf("`a\r\nb\rc\nd`", 0), "a\nb\nc\nd");
  EXPECT_EQ(ValueOf("`a\r\nb\rc\nd`", 0, GLECMA_VALUE_RAW), "a\nb\nc\nd");
  EXPECT_EQ(ValueOf("`a\xE2\x80\xA8" "b\xE2\x80\xA9`", 0),
      "a\xE2\x80\xA8" "b\xE2\x80\xA9");
  // A line continuation: nothing in the cooked value; the raw value keeps
  // the backslash and the (normalized) terminator.
  EXPECT_EQ(ValueOf("`a\\\nb`", 0), "ab");
  EXPECT_EQ(ValueOf("`a\\\nb`", 0, GLECMA_VALUE_RAW), "a\\\nb");
  EXPECT_EQ(ValueOf("`a\\\r\nb`", 0), "ab");
  EXPECT_EQ(ValueOf("`a\\\r\nb`", 0, GLECMA_VALUE_RAW), "a\\\nb");
  EXPECT_EQ(ValueOf("`a\\\rb`", 0, GLECMA_VALUE_RAW), "a\\\nb");
  // Terminators in a template are line terminators for locations.
  Lexed lexed = Lex("`a\r\nb`;x");
  EXPECT_EQ(lexed.tokens[2].start.line, 2u);
  EXPECT_EQ(lexed.tokens[2].start.column, 3u);
}

TEST(Templates, EscapesAreCooked) {
  EXPECT_EQ(ValueOf("`\\x41\\u0041\\u{41}\\n\\0`", 0),
      std::string("AAA\n\0", 5));
  EXPECT_EQ(ValueOf("`\\x41\\u0041`", 0, GLECMA_VALUE_RAW), "\\x41\\u0041");
  EXPECT_EQ(ValueOf("`\\q`", 0), "q");
  EXPECT_EQ(Lex("`\\x41`").tokens[0].flags & GLECMA_TOKF_COOKED_UNDEFINED, 0u);
}

// 12.9.6: an invalid escape in a template is not an error here: its cooked
// value is undefined, its raw value is intact, and the token says why, so
// that the parser can refuse it in an untagged template and accept it in a
// tagged one.
TEST(Templates, InvalidEscapesMakeTheCookedValueUndefined) {
  struct Case {
    const char * text;
    GLECMA_Rule rule;
    size_t offset;
  };
  const Case cases[] = {
      {"`\\u{`", GLECMA_RULE_ESCAPE_CODE_POINT, 1},
      {"`\\u{110000}`", GLECMA_RULE_ESCAPE_CODE_POINT, 1},
      {"`\\u00`", GLECMA_RULE_ESCAPE_UNICODE, 1},
      {"`\\u`", GLECMA_RULE_ESCAPE_UNICODE, 1},
      {"`\\xg`", GLECMA_RULE_ESCAPE_HEX, 1},
      {"`ab\\x4`", GLECMA_RULE_ESCAPE_HEX, 3},
      {"`\\01`", GLECMA_RULE_TEMPLATE_ESCAPE_OCTAL, 1},
      {"`\\00`", GLECMA_RULE_TEMPLATE_ESCAPE_OCTAL, 1},
      {"`\\1`", GLECMA_RULE_TEMPLATE_ESCAPE_OCTAL, 1},
      {"`\\7`", GLECMA_RULE_TEMPLATE_ESCAPE_OCTAL, 1},
      {"`\\8`", GLECMA_RULE_TEMPLATE_ESCAPE_OCTAL, 1},
      {"`\\9`", GLECMA_RULE_TEMPLATE_ESCAPE_OCTAL, 1},
  };
  for (uint32_t mode : {0u, kStrict, kModule}) {
    for (const Case & c : cases) {
      Lexed lexed = Lex(c.text, mode);
      ASSERT_EQ(lexed.result, GLECMA_OK) << c.text;
      ASSERT_GE(lexed.tokens.size(), 1u);
      EXPECT_NE(lexed.tokens[0].flags & GLECMA_TOKF_COOKED_UNDEFINED, 0u)
          << c.text;
      EXPECT_EQ(lexed.tokens[0].escape_rule, R(c.rule)) << c.text;
      GLECMA_Result r = GLECMA_OK;
      ValueOf(c.text, 0, GLECMA_VALUE_COOKED, mode, &r);
      EXPECT_EQ(r, GLECMA_ERR_INVALID) << c.text;
      r = GLECMA_ERR_INVALID;
      ValueOf(c.text, 0, GLECMA_VALUE_RAW, mode, &r);
      EXPECT_EQ(r, GLECMA_OK) << c.text;
    }
  }
  // The first invalid escape is the one recorded, with its own location.
  GLECMA_Source * source = nullptr;
  const char text[] = "`a\\xq\\u{`";
  ASSERT_EQ(glecma_source_create_utf8(text, sizeof(text) - 1, nullptr, nullptr,
                nullptr, &source),
      GLECMA_OK);
  GLECMA_Lexer lexer;
  ASSERT_EQ(glecma_lexer_open(&lexer, source, nullptr, 0, nullptr), GLECMA_OK);
  GLECMA_Token t;
  ASSERT_EQ(glecma_lexer_next(&lexer, GLECMA_GOAL_DIV, &t), GLECMA_OK);
  EXPECT_EQ(t.escape_rule, R(GLECMA_RULE_ESCAPE_HEX));
  EXPECT_EQ(t.escape_location.offset, 2u);
  EXPECT_EQ(t.escape_location.column, 2u);
  glecma_source_release(source);
  // A valid `\0` is not invalid; neither is a `\0` before a non-digit.
  EXPECT_EQ(Lex("`\\0`").tokens[0].flags & GLECMA_TOKF_COOKED_UNDEFINED, 0u);
  EXPECT_EQ(Lex("`\\0a`").tokens[0].flags & GLECMA_TOKF_COOKED_UNDEFINED, 0u);
  // An invalid escape does not hide the end of the template.
  EXPECT_EQ(Sig(Lex("`\\u{` x")), "TEMPLATE NAME");
  EXPECT_EQ(Sig(Lex("`\\xg${y}`")), "TEMPLATE NAME TEMPLATE");
}

TEST(Templates, Unterminated) {
  Lexed lexed = Lex("`abc");
  EXPECT_EQ(lexed.result, GLECMA_ERR_SYNTAX);
  EXPECT_EQ(lexed.failure.rule, R(GLECMA_RULE_UNTERMINATED_TEMPLATE));
  EXPECT_EQ(lexed.failure.location.offset, 0u);
  lexed = Lex("`a${b}c");
  EXPECT_EQ(lexed.failure.rule, R(GLECMA_RULE_UNTERMINATED_TEMPLATE));
  EXPECT_EQ(lexed.failure.location.offset, 5u);
  EXPECT_EQ(Lex("`a\\").failure.rule, R(GLECMA_RULE_UNTERMINATED_TEMPLATE));
}

// 12.9.5: regular expression literals; the pattern is regex's to validate.
TEST(Regexps, BodiesAndFlags) {
  Lexed lexed = Lex("/ab+c/gi");
  ASSERT_EQ(lexed.result, GLECMA_OK);
  ASSERT_EQ(lexed.tokens[0].kind, GLECMA_TOK_REGEXP);
  EXPECT_EQ(lexed.tokens[0].text, "/ab+c/gi");
  EXPECT_EQ(lexed.tokens[0].regexp_flags_offset, 6u);
  EXPECT_EQ(Lex("/a/").tokens[0].regexp_flags_offset, 3u);
  EXPECT_EQ(Lex("/a/").tokens[0].end.offset, 3u);
  // A slash in a class, an escaped slash and an escaped bracket.
  for (const char * text : {"/[/]/", "/\\//", "/[\\]/]/", "/[^/]/g",
           "/a[/]b/", "/(?<n>a)\\k<n>/u", "/\\u{1F600}/u", "/=/"}) {
    Lexed ok = Lex(text);
    EXPECT_EQ(ok.result, GLECMA_OK) << text;
    ASSERT_GE(ok.tokens.size(), 1u);
    EXPECT_EQ(ok.tokens[0].kind, GLECMA_TOK_REGEXP) << text;
    EXPECT_EQ(ok.tokens[0].text, text);
  }
  // Flags are IdentifierPartChars: a digit, `$`, ZWNJ, a Unicode letter.
  EXPECT_EQ(Lex("/a/g1$").tokens[0].text, "/a/g1$");
  EXPECT_EQ(Lex("/a/g\xE2\x80\x8C").tokens[0].text, "/a/g\xE2\x80\x8C");
  EXPECT_EQ(Lex("/a/\xCE\xB1").tokens[0].text, "/a/\xCE\xB1");
  // The literal ends where the flags do.
  EXPECT_EQ(Sig(Lex("/a/g.test(x)")), "REGEXP . NAME ( NAME )");
  EXPECT_EQ(Sig(Lex("/a/g /b/g")), "REGEXP / NAME / NAME");
  // A non-ASCII body, in a class, with an astral character.
  EXPECT_EQ(Lex("/[\xF0\x9F\x98\x80-\xF0\x9F\x98\x81]/u").result, GLECMA_OK);
}

TEST(Regexps, InvalidLiterals) {
  const BadCase cases[] = {
      {"/a", GLECMA_RULE_UNTERMINATED_REGEXP, 0},
      {"x = /a\n/", GLECMA_RULE_UNTERMINATED_REGEXP, 4},
      {"/a\r/", GLECMA_RULE_UNTERMINATED_REGEXP, 0},
      {"/a\xE2\x80\xA8/", GLECMA_RULE_UNTERMINATED_REGEXP, 0},
      {"/a\xE2\x80\xA9/", GLECMA_RULE_UNTERMINATED_REGEXP, 0},
      {"/a\\\n/", GLECMA_RULE_UNTERMINATED_REGEXP, 0},
      {"/a\\", GLECMA_RULE_UNTERMINATED_REGEXP, 0},
      {"/[a/", GLECMA_RULE_UNTERMINATED_REGEXP, 0},
      {"/[/\n]/", GLECMA_RULE_UNTERMINATED_REGEXP, 0},
      {"/a/\\u0067", GLECMA_RULE_REGEXP_FLAGS, 3},
      {"/a/g\\x", GLECMA_RULE_REGEXP_FLAGS, 4},
  };
  for (const BadCase & c : cases) {
    Lexed lexed = Lex(c.text);
    EXPECT_EQ(lexed.result, GLECMA_ERR_SYNTAX) << c.text;
    EXPECT_EQ(lexed.failure.rule, R(c.rule)) << c.text;
    EXPECT_EQ(lexed.failure.location.offset, c.offset) << c.text;
  }
}

// An invalid `*` first character is a comment, not a regular expression.
TEST(Regexps, SlashStarIsAComment) {
  EXPECT_EQ(Sig(Lex("/*a*/ /b/")), "REGEXP");
  EXPECT_EQ(Sig(Lex("//a\n/b/")), "REGEXP");
}

// Token values allocate through the allocator they are given and free
// everything on failure.
TEST(Values, AllocationFailureLeaksNothing) {
  GLECMA_Source * source = nullptr;
  const char text[] = "'a\\u{1F600}b' `x${y}z` \\u0061bc";
  ASSERT_EQ(glecma_source_create_utf8(text, sizeof(text) - 1, nullptr, nullptr,
                nullptr, &source),
      GLECMA_OK);
  GLECMA_Lexer lexer;
  ASSERT_EQ(glecma_lexer_open(&lexer, source, nullptr, 0, nullptr), GLECMA_OK);
  std::vector<GLECMA_Token> tokens;
  glecma_lexer_run(&lexer,
      [](void * c, const GLECMA_Token * t) {
        static_cast<std::vector<GLECMA_Token> *>(c)->push_back(*t);
        return true;
      },
      &tokens);
  size_t valued = 0;
  for (const GLECMA_Token & token : tokens) {
    if (token.kind != GLECMA_TOK_STRING && token.kind != GLECMA_TOK_TEMPLATE &&
        token.kind != GLECMA_TOK_NAME) {
      continue;
    }
    ++valued;
    CountingAllocator counting;
    uint16_t * units = nullptr;
    size_t length = 0;
    ASSERT_EQ(glecma_token_value(source, &token, GLECMA_VALUE_COOKED,
                  counting.get(), &units, &length),
        GLECMA_OK);
    EXPECT_EQ(counting.live(), 1u);
    glecma_token_value_free(counting.get(), units);
    EXPECT_EQ(counting.live(), 0u);
    counting.reset();
    counting.fail_at(0);
    units = nullptr;
    EXPECT_EQ(glecma_token_value(source, &token, GLECMA_VALUE_COOKED,
                  counting.get(), &units, &length),
        GLECMA_ERR_OOM);
    EXPECT_EQ(counting.live(), 0u);
    EXPECT_EQ(units, nullptr);
  }
  EXPECT_EQ(valued, 5u);
  // A token that has no value is refused.
  GLECMA_Token number;
  std::memset(&number, 0, sizeof(number));
  number.kind = GLECMA_TOK_NUMBER;
  uint16_t * units = nullptr;
  size_t length = 0;
  EXPECT_EQ(glecma_token_value(source, &number, GLECMA_VALUE_COOKED, nullptr,
                &units, &length),
      GLECMA_ERR_INVALID);
  EXPECT_EQ(glecma_token_value(source, nullptr, GLECMA_VALUE_COOKED, nullptr,
                &units, &length),
      GLECMA_ERR_INVALID);
  // A raw value is only for templates.
  EXPECT_EQ(glecma_token_value(source, &tokens[0], GLECMA_VALUE_RAW, nullptr,
                &units, &length),
      GLECMA_ERR_INVALID);
  glecma_source_release(source);
}

int main(int argc, char * * argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

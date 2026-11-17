// SPDX-License-Identifier: LGPL-3.0-only
//
// The tokenizer: punctuators, names and keywords, white space and comments,
// the input element goals, and locations. Every case runs in UTF-8 and in
// UTF-16 and requires the same tokens (Lex in lex_test_support.h).
// ECMA-262 clause numbers are the 17th edition's
// (documentation/clause-concordance.tsv maps them from the 16th).

#include "../lex_test_support.h"

#include <ghoti.io/unicode/char.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using namespace glecma_test;

namespace {

std::string Texts(const Lexed & lexed) {
  std::string out;
  for (const Tok & t : lexed.tokens) {
    if (t.kind == GLECMA_TOK_EOF) {
      continue;
    }
    if (!out.empty()) {
      out += '|';
    }
    out += t.text;
  }
  return out;
}

constexpr uint32_t kNewline = GLECMA_TOKF_NEWLINE_BEFORE;

}  // namespace

// Clause 12.8: every punctuator, alone, is its own kind.
TEST(Punctuators, EachIsOneToken) {
  for (int k = GLECMA_TOK_LBRACE; k < GLECMA_TOK_COUNT; ++k) {
    const char * text =
        glecma_token_punctuator(static_cast<GLECMA_TokenKind>(k));
    ASSERT_NE(text, nullptr) << k;
    // After a name the driver is in the division goal, so `/` and `/=` are
    // the DivPunctuators; a `}` outside a template is a RightBrace.
    Lexed lexed = Lex(std::string("a ") + text + " b");
    ASSERT_EQ(lexed.result, GLECMA_OK) << text;
    ASSERT_EQ(lexed.tokens.size(), 4u) << text;
    EXPECT_EQ(lexed.tokens[1].kind, k) << text;
    EXPECT_EQ(lexed.tokens[1].text, text);
  }
}

// Clause 12.8: the longest punctuator wins.
TEST(Punctuators, LongestMatch) {
  EXPECT_EQ(Texts(Lex("a>>>=b")), "a|>>>=|b");
  EXPECT_EQ(Texts(Lex("a>>>b")), "a|>>>|b");
  EXPECT_EQ(Texts(Lex("a>>=b")), "a|>>=|b");
  EXPECT_EQ(Texts(Lex("a===b")), "a|===|b");
  EXPECT_EQ(Texts(Lex("a!==b")), "a|!==|b");
  EXPECT_EQ(Texts(Lex("a**=b")), "a|**=|b");
  EXPECT_EQ(Texts(Lex("a&&=b||=c")), "a|&&=|b|||=|c");
  EXPECT_EQ(Texts(Lex("a=>b")), "a|=>|b");
  EXPECT_EQ(Texts(Lex("a...b")), "a|...|b");
  EXPECT_EQ(Texts(Lex("a..b")), "a|.|.|b");
  EXPECT_EQ(Texts(Lex("a<<=b<<c<=d")), "a|<<=|b|<<|c|<=|d");
  EXPECT_EQ(Texts(Lex("a+++b")), "a|++|+|b");
  EXPECT_EQ(Texts(Lex("a---b")), "a|--|-|b");
}

// Clause 12.8: `?.` is the optional chaining punctuator unless a digit follows.
TEST(Punctuators, OptionalChainingIsNotBeforeADigit) {
  EXPECT_EQ(Texts(Lex("a?.b")), "a|?.|b");
  EXPECT_EQ(Texts(Lex("a?.5:1")), "a|?|.5|:|1");
  EXPECT_EQ(Texts(Lex("a?.[0]")), "a|?.|[|0|]");
  EXPECT_EQ(Texts(Lex("a?.(0)")), "a|?.|(|0|)");
  EXPECT_EQ(Texts(Lex("a??.5")), "a|??|.5");
}

// Clause 12.6: a stray character begins no token.
TEST(Punctuators, AStrayCharacterIsRefused) {
  for (const char * text : {"@", "a @ b", "\\", "a \\ b"}) {
    EXPECT_EQ(Lex(text).result, GLECMA_ERR_SYNTAX) << text;
  }
  Lexed at = Lex("a @");
  EXPECT_EQ(at.failure.rule,
      static_cast<uint32_t>(GLECMA_RULE_UNEXPECTED_CHARACTER));
  EXPECT_EQ(at.failure.location.offset, 2u);
  EXPECT_EQ(at.failure.location.column, 2u);
  // U+00B7 is ID_Continue, not ID_Start; a lone one begins no token.
  EXPECT_EQ(Lex("\xC2\xB7").failure.rule,
      static_cast<uint32_t>(GLECMA_RULE_UNEXPECTED_CHARACTER));
}

// Clause 12.7: every reserved word, strict-mode word and contextual word is
// recognized, and nothing else is.
TEST(Names, EveryKeywordRoundTrips) {
  for (int k = GLECMA_KW_NONE + 1; k < GLECMA_KW_COUNT; ++k) {
    const char * text = glecma_keyword_text(static_cast<GLECMA_Keyword>(k));
    ASSERT_NE(text, nullptr) << k;
    Lexed lexed = Lex(text);
    ASSERT_EQ(lexed.tokens.size(), 2u) << text;
    EXPECT_EQ(lexed.tokens[0].kind, GLECMA_TOK_NAME) << text;
    EXPECT_EQ(lexed.tokens[0].keyword, k) << text;
    EXPECT_EQ(lexed.tokens[0].flags & GLECMA_TOKF_ESCAPED, 0u) << text;
    // A longer name that merely begins with it is an ordinary name.
    EXPECT_EQ(Lex(std::string(text) + "x").tokens[0].keyword, GLECMA_KW_NONE);
    // ...as is a shorter one.
    if (std::strlen(text) > 2) {
      EXPECT_EQ(Lex(std::string(text, std::strlen(text) - 1)).tokens[0].keyword,
          GLECMA_KW_NONE)
          << text;
    }
  }
  EXPECT_EQ(glecma_keyword_text(GLECMA_KW_NONE), nullptr);
}

// Clause 12.7.2: the reserved words of the edition, one by one.
TEST(Names, EveryReservedWordIsRecognized) {
  for (const char * word : {"break", "case", "catch", "class", "const",
           "continue", "debugger", "default", "delete", "do", "else", "enum",
           "export", "extends", "false", "finally", "for", "function", "if",
           "import", "in", "instanceof", "new", "null", "return", "super",
           "switch", "this", "throw", "true", "try", "typeof", "var", "void",
           "while", "with"}) {
    GLECMA_Keyword kw = Lex(word).tokens[0].keyword;
    EXPECT_NE(kw, GLECMA_KW_NONE) << word;
    GLECMA_Token token;
    std::memset(&token, 0, sizeof(token));
    token.kind = GLECMA_TOK_NAME;
    token.keyword = kw;
    EXPECT_EQ(glecma_token_identifier_check(&token, 0),
        static_cast<uint32_t>(GLECMA_RULE_RESERVED_WORD))
        << word;
  }
  // The table is searched by bisection, so it must be sorted and a name must
  // not be listed twice.
  std::vector<std::string> words;
  for (int k = GLECMA_KW_NONE + 1; k < GLECMA_KW_COUNT; ++k) {
    words.push_back(glecma_keyword_text(static_cast<GLECMA_Keyword>(k)));
  }
  std::sort(words.begin(), words.end());
  EXPECT_EQ(std::adjacent_find(words.begin(), words.end()), words.end());
}

// Clause 12.7.1: an identifier may be written with escapes, and an escaped
// reserved word is flagged and is not the keyword.
TEST(Names, EscapesAreFlaggedAndDecoded) {
  Lexed lexed = Lex("\\u0069f a\\u{62}c");
  ASSERT_EQ(lexed.result, GLECMA_OK);
  EXPECT_EQ(lexed.tokens[0].keyword, GLECMA_KW_IF);
  EXPECT_NE(lexed.tokens[0].flags & GLECMA_TOKF_ESCAPED, 0u);
  EXPECT_EQ(lexed.tokens[1].keyword, GLECMA_KW_NONE);
  EXPECT_NE(lexed.tokens[1].flags & GLECMA_TOKF_ESCAPED, 0u);
  EXPECT_EQ(ValueOf("\\u0069f a\\u{62}c", 0), "if");
  EXPECT_EQ(ValueOf("\\u0069f a\\u{62}c", 1), "abc");
  // The same word, spelled with an escape in the middle.
  EXPECT_EQ(Lex("fo\\u0072").tokens[0].keyword, GLECMA_KW_FOR);
  // Astral, escaped.
  EXPECT_EQ(ValueOf("a\\u{20BB7}", 0), "a\xF0\xA0\xAE\xB7");
}

TEST(Names, IdentifierEscapesAreValidated) {
  // Not a \u escape; malformed; out of range; not an IdentifierPart.
  for (const char * text : {"\\x41", "\\u00", "\\u{}", "\\u{110000}",
           "\\u{zz}", "a\\u0020", "\\ud800", "\\u0030", "\\u{0}",
           "\\u{1d7d8}"}) {
    Lexed lexed = Lex(text);
    EXPECT_EQ(lexed.result, GLECMA_ERR_SYNTAX) << text;
    EXPECT_EQ(lexed.failure.rule,
        static_cast<uint32_t>(GLECMA_RULE_IDENTIFIER_ESCAPE))
        << text;
  }
  // A digit is an IdentifierPart, so it is fine after the first.
  EXPECT_EQ(Lex("a\\u0030").result, GLECMA_OK);
  // ZWNJ and ZWJ are IdentifierParts, never starts.
  EXPECT_EQ(Lex("a\\u200c").result, GLECMA_OK);
  EXPECT_EQ(Lex("\\u200c").result, GLECMA_ERR_SYNTAX);
  EXPECT_EQ(Lex("a\xE2\x80\x8D" "b").result, GLECMA_OK);
  EXPECT_EQ(Lex("\xE2\x80\x8D" "a").result, GLECMA_ERR_SYNTAX);
}

// Clause 12.7: identifiers from `unicode` at its one pinned version.
TEST(Names, UnicodeIdentifiers) {
  EXPECT_EQ(Sig(Lex("\xC3\xA9t\xC3\xA9 \xCE\xB1\xCE\xB2\xCE\xB3 $ _ _$1")),
      "NAME NAME NAME NAME NAME");
  // An astral ID_Start (U+20BB7), raw and escaped.
  EXPECT_EQ(Sig(Lex("\xF0\xA0\xAE\xB7 x\xF0\xA0\xAE\xB7")), "NAME NAME");
  EXPECT_EQ(Lex("\\u{20BB7}").result, GLECMA_OK);
  // Other_ID_Start (U+2118) and Other_ID_Continue (U+00B7).
  EXPECT_EQ(Lex("\xE2\x84\x98").result, GLECMA_OK);
  EXPECT_EQ(Lex("a\xC2\xB7").result, GLECMA_OK);
  // Pattern_Syntax, a currency symbol and an emoji are not identifier
  // characters.
  EXPECT_EQ(Lex("\xE2\xB8\xAF").result, GLECMA_ERR_SYNTAX);      // U+2E2F
  EXPECT_EQ(Lex("\xE2\x82\xAC").result, GLECMA_ERR_SYNTAX);      // U+20AC
  EXPECT_EQ(Lex("\xF0\x9F\x98\x80").result, GLECMA_ERR_SYNTAX);  // U+1F600
}

// The ASCII table the scanners use agrees with `unicode` and 12.7.
TEST(Names, TheAsciiTableAgreesWithUnicode) {
  for (uint32_t cp = 0; cp < 128; ++cp) {
    bool start =
        guni_has_property(cp, GUNI_PROP_ID_START) || cp == '$' || cp == '_';
    bool part = guni_has_property(cp, GUNI_PROP_ID_CONTINUE) || cp == '$';
    EXPECT_EQ(glecma_lex_is_id_start(cp), start) << cp;
    EXPECT_EQ(glecma_lex_is_id_part(cp), part) << cp;
    EXPECT_EQ((glecma_lex_ascii_class[cp] & GLECMA_LEXC_START) != 0, start)
        << cp;
    EXPECT_EQ((glecma_lex_ascii_class[cp] & GLECMA_LEXC_PART) != 0, part)
        << cp;
  }
  // ZWNJ and ZWJ.
  EXPECT_TRUE(glecma_lex_is_id_part(0x200C));
  EXPECT_TRUE(glecma_lex_is_id_part(0x200D));
  EXPECT_FALSE(glecma_lex_is_id_start(0x200C));
}

// Every non-ASCII code point as the first character of a source, against
// `unicode`'s ID_Start; each is its own line of one source and the lexer is
// moved to it.
TEST(Names, EveryCodePointIsClassifiedAsUnicodeSays) {
  std::string text;
  std::vector<size_t> offsets;
  std::vector<uint32_t> cps;
  for (uint32_t cp = 0x80; cp <= 0x10FFFF; ++cp) {
    if (cp >= 0xD800 && cp <= 0xDFFF) {
      continue;  // not UTF-8
    }
    offsets.push_back(text.size());
    cps.push_back(cp);
    text += Utf8Of(cp);
    text += '\n';
  }
  offsets.push_back(text.size());
  GLECMA_Source * source = nullptr;
  ASSERT_EQ(glecma_source_create_utf8(text.data(), text.size(), nullptr,
                nullptr, nullptr, &source),
      GLECMA_OK);
  GLECMA_Lexer lexer;
  ASSERT_EQ(glecma_lexer_open(&lexer, source, nullptr, 0, nullptr), GLECMA_OK);
  size_t wrong = 0;
  for (size_t i = 0; i < cps.size(); ++i) {
    uint32_t cp = cps[i];
    bool is_space = cp == 0xA0 || cp == 0xFEFF || cp == 0x2028 ||
        cp == 0x2029 || guni_general_category(cp) == GUNI_GC_ZS;
    if (is_space) {
      continue;  // trivia: the next token is the next line's
    }
    GLECMA_Location where;
    ASSERT_EQ(glecma_source_location(source, offsets[i], &where), GLECMA_OK);
    glecma_lexer_reset(&lexer, &where);
    GLECMA_Token token;
    GLECMA_Result r = glecma_lexer_next(&lexer, GLECMA_GOAL_DIV, &token);
    bool is_start = guni_has_property(cp, GUNI_PROP_ID_START);
    bool lexed_as_name = r == GLECMA_OK && token.kind == GLECMA_TOK_NAME &&
        token.start.offset == offsets[i];
    if (lexed_as_name != is_start && ++wrong < 5) {
      ADD_FAILURE() << "U+" << std::hex << cp << " is_start=" << is_start;
    }
  }
  EXPECT_EQ(wrong, 0u);
  glecma_source_release(source);
}

// Every code point through the escape path: a name starting with `\u{X}`
// lexes exactly when X is an IdentifierStartChar, and `a\u{X}` exactly when
// it is an IdentifierPartChar.
TEST(Names, EveryEscapedCodePointIsClassifiedAsUnicodeSays) {
  std::string start_text;
  std::string part_text;
  size_t starts = 0;
  size_t parts = 0;
  std::vector<uint32_t> not_start;
  for (uint32_t cp = 0; cp <= 0x10FFFF; ++cp) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "\\u{%X}", cp);
    bool u_start = cp < 128
        ? (guni_has_property(cp, GUNI_PROP_ID_START) || cp == '$' || cp == '_')
        : guni_has_property(cp, GUNI_PROP_ID_START);
    bool u_part = cp < 128
        ? (guni_has_property(cp, GUNI_PROP_ID_CONTINUE) || cp == '$')
        : (guni_has_property(cp, GUNI_PROP_ID_CONTINUE) || cp == 0x200C ||
              cp == 0x200D);
    ASSERT_EQ(glecma_lex_is_id_start(cp), u_start) << cp;
    ASSERT_EQ(glecma_lex_is_id_part(cp), u_part) << cp;
    if (u_start) {
      start_text += buf;
      start_text += ' ';
      ++starts;
    } else if (cp % 61 == 0) {
      not_start.push_back(cp);
    }
    if (u_part) {
      part_text += std::string("a") + buf + ' ';
      ++parts;
    }
  }
  // Every ID_Start escape lexes, as one name each, and so does every part.
  Lexed a = LexUtf8(start_text);
  ASSERT_EQ(a.result, GLECMA_OK);
  EXPECT_EQ(a.tokens.size(), starts + 1);
  Lexed b = LexUtf8(part_text);
  ASSERT_EQ(b.result, GLECMA_OK);
  EXPECT_EQ(b.tokens.size(), parts + 1);
  // And a sample of the code points that are not starts is refused.
  EXPECT_GT(not_start.size(), 10000u);
  for (uint32_t cp : not_start) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "\\u{%X}", cp);
    ASSERT_EQ(LexUtf8(buf).result, GLECMA_ERR_SYNTAX) << cp;
  }
}

// Clause 12.7: a private name.
TEST(Names, PrivateNames) {
  Lexed lexed = Lex("#x #\\u0061b a.#c");
  ASSERT_EQ(lexed.result, GLECMA_OK);
  EXPECT_EQ(lexed.tokens[0].kind, GLECMA_TOK_PRIVATE_NAME);
  EXPECT_EQ(lexed.tokens[1].kind, GLECMA_TOK_PRIVATE_NAME);
  EXPECT_EQ(ValueOf("#x #\\u0061b", 0), "x");
  EXPECT_EQ(ValueOf("#x #\\u0061b", 1), "ab");
  for (const char * text : {"#", "# x", "#1", "# "}) {
    Lexed bad = Lex(text);
    EXPECT_EQ(bad.result, GLECMA_ERR_SYNTAX) << text;
    EXPECT_EQ(bad.failure.rule,
        static_cast<uint32_t>(GLECMA_RULE_PRIVATE_NAME))
        << text;
    EXPECT_EQ(bad.failure.location.offset, 0u) << text;
  }
  // An escape that is not an IdentifierStart is the escape's error.
  Lexed escape = Lex("#\\u0031");
  EXPECT_EQ(escape.failure.rule,
      static_cast<uint32_t>(GLECMA_RULE_IDENTIFIER_ESCAPE));
  EXPECT_EQ(escape.failure.location.offset, 1u);
}

// The identifier check a parser asks where an Identifier is required.
TEST(Names, ReservedWordsAreNotIdentifiers) {
  auto check = [](const char * text, uint32_t context) {
    Lexed lexed = Lex(text);
    GLECMA_Token token;
    std::memset(&token, 0, sizeof(token));
    token.kind = lexed.tokens[0].kind;
    token.keyword = lexed.tokens[0].keyword;
    token.flags = lexed.tokens[0].flags;
    return glecma_token_identifier_check(&token, context);
  };
  EXPECT_EQ(check("if", 0), static_cast<uint32_t>(GLECMA_RULE_RESERVED_WORD));
  EXPECT_EQ(check("enum", 0),
      static_cast<uint32_t>(GLECMA_RULE_RESERVED_WORD));
  EXPECT_EQ(check("\\u0069f", 0),
      static_cast<uint32_t>(GLECMA_RULE_ESCAPED_RESERVED_WORD));
  EXPECT_EQ(check("foo", 0), 0u);
  EXPECT_EQ(check("async", GLECMA_IDF_STRICT), 0u);
  // Strict-mode words.
  EXPECT_EQ(check("let", 0), 0u);
  EXPECT_EQ(check("let", GLECMA_IDF_STRICT),
      static_cast<uint32_t>(GLECMA_RULE_RESERVED_WORD_STRICT));
  EXPECT_EQ(check("static", GLECMA_IDF_STRICT),
      static_cast<uint32_t>(GLECMA_RULE_RESERVED_WORD_STRICT));
  EXPECT_EQ(check("l\\u0065t", GLECMA_IDF_STRICT),
      static_cast<uint32_t>(GLECMA_RULE_ESCAPED_RESERVED_WORD));
  EXPECT_EQ(check("l\\u0065t", 0), 0u);
  // yield and await depend on the context.
  EXPECT_EQ(check("yield", 0), 0u);
  EXPECT_NE(check("yield", GLECMA_IDF_STRICT), 0u);
  EXPECT_NE(check("yield", GLECMA_IDF_YIELD), 0u);
  EXPECT_EQ(check("await", 0), 0u);
  EXPECT_NE(check("await", GLECMA_IDF_AWAIT), 0u);
  EXPECT_EQ(check("await", GLECMA_IDF_STRICT), 0u);
  EXPECT_EQ(check("a", GLECMA_IDF_STRICT | GLECMA_IDF_AWAIT | GLECMA_IDF_YIELD),
      0u);
  // Not a name at all.
  GLECMA_Token number;
  std::memset(&number, 0, sizeof(number));
  number.kind = GLECMA_TOK_NUMBER;
  EXPECT_EQ(glecma_token_identifier_check(&number, GLECMA_IDF_STRICT), 0u);
  EXPECT_EQ(glecma_token_identifier_check(nullptr, 0), 0u);
}

// Clause 12.2: white space is TAB, VT, FF, ZWNBSP and the Space_Separators.
TEST(Trivia, WhiteSpace) {
  for (uint32_t cp : {0x09u, 0x0Bu, 0x0Cu, 0x20u, 0xA0u, 0xFEFFu, 0x1680u,
           0x2000u, 0x2005u, 0x200Au, 0x202Fu, 0x205Fu, 0x3000u}) {
    Lexed lexed = Lex("a" + Utf8Of(cp) + "b");
    EXPECT_EQ(Sig(lexed), "NAME NAME") << cp;
    EXPECT_EQ(lexed.tokens[1].flags & kNewline, 0u) << cp;
  }
  // U+180E (Mongolian vowel separator) has been Cf since Unicode 6.3.
  EXPECT_EQ(Lex("a\xE1\xA0\x8E" "b").result, GLECMA_ERR_SYNTAX);
  // U+200B (zero width space) is not white space either.
  EXPECT_EQ(Lex("a\xE2\x80\x8B" "b").result, GLECMA_ERR_SYNTAX);
  // A leading ZWNBSP is white space, never stripped (source-text.md).
  Lexed bom = Lex("\xEF\xBB\xBF" "a");
  EXPECT_EQ(Sig(bom), "NAME");
  EXPECT_EQ(bom.tokens[0].start.column, 1u);
  EXPECT_EQ(bom.tokens[0].start.offset, 3u);
}

// Every code point whose General_Category is Zs is white space.
TEST(Trivia, EverySpaceSeparatorIsWhiteSpace) {
  size_t seen = 0;
  for (uint32_t cp = 0x80; cp <= 0x10FFFF; ++cp) {
    if (guni_general_category(cp) == GUNI_GC_ZS) {
      EXPECT_EQ(Sig(Lex("a" + Utf8Of(cp) + "b")), "NAME NAME") << cp;
      ++seen;
    }
  }
  EXPECT_GE(seen, 7u);
}

// 12.3 and 12.10: the newline flag, from terminators and from comments.
TEST(Trivia, TheNewlineFlag) {
  for (const char * terminator :
      {"\n", "\r", "\r\n", "\xE2\x80\xA8", "\xE2\x80\xA9"}) {
    Lexed lexed = Lex(std::string("a") + terminator + "b");
    ASSERT_EQ(lexed.tokens.size(), 3u);
    EXPECT_NE(lexed.tokens[1].flags & kNewline, 0u);
    EXPECT_EQ(lexed.tokens[0].flags & kNewline, 0u);
    EXPECT_EQ(lexed.tokens[1].start.line, 2u);
    EXPECT_EQ(lexed.tokens[1].start.column, 0u);
  }
  // A multi-line comment holding a terminator counts; one without does not.
  EXPECT_NE(Lex("a /* \n */ b").tokens[1].flags & kNewline, 0u);
  EXPECT_NE(Lex("a /* \xE2\x80\xA8 */ b").tokens[1].flags & kNewline, 0u);
  EXPECT_EQ(Lex("a /* x */ b").tokens[1].flags & kNewline, 0u);
  // The flag belongs to the one token it precedes.
  Lexed lexed = Lex("a\nb c");
  EXPECT_NE(lexed.tokens[1].flags & kNewline, 0u);
  EXPECT_EQ(lexed.tokens[2].flags & kNewline, 0u);
}

// Clause 12.4: comments.
TEST(Trivia, Comments) {
  EXPECT_EQ(Sig(Lex("a // c\nb")), "NAME NAME");
  EXPECT_EQ(Sig(Lex("a // c")), "NAME");
  EXPECT_EQ(Sig(Lex("a /* c */ b")), "NAME NAME");
  EXPECT_EQ(Sig(Lex("a /* * / **/ b")), "NAME NAME");
  EXPECT_EQ(Sig(Lex("a/**/b")), "NAME NAME");
  // A single-line comment ends at LS and PS, not at other characters.
  EXPECT_EQ(Sig(Lex("a // c\xE2\x80\xA8 b")), "NAME NAME");
  // An unterminated comment is refused at its start.
  Lexed bad = Lex("a /* never");
  EXPECT_EQ(bad.result, GLECMA_ERR_SYNTAX);
  EXPECT_EQ(bad.failure.rule,
      static_cast<uint32_t>(GLECMA_RULE_UNTERMINATED_COMMENT));
  EXPECT_EQ(bad.failure.location.offset, 2u);
  // `/*/` does not close itself.
  EXPECT_EQ(Lex("/*/").result, GLECMA_ERR_SYNTAX);
  EXPECT_EQ(Lex("/*/ */").result, GLECMA_OK);
}

// Clause 12.5: a hashbang comment, only at the very start.
TEST(Trivia, Hashbang) {
  EXPECT_EQ(Sig(Lex("#!/usr/bin/env node\nx")), "NAME");
  EXPECT_EQ(Sig(Lex("#!")), "");
  EXPECT_EQ(Lex("#!x").result, GLECMA_OK);
  EXPECT_EQ(Lex(" #!x").result, GLECMA_ERR_SYNTAX);
  EXPECT_EQ(Lex("\n#!x").result, GLECMA_ERR_SYNTAX);
  EXPECT_EQ(Lex("a #!x").failure.rule,
      static_cast<uint32_t>(GLECMA_RULE_PRIVATE_NAME));
  // A leading BOM is white space, so a `#!` after it is not at the start.
  EXPECT_EQ(Lex("\xEF\xBB\xBF#!x").result, GLECMA_ERR_SYNTAX);
  // Both Script and Module goals allow one.
  EXPECT_EQ(Lex("#!x\ny", GLECMA_LEXF_MODULE).result, GLECMA_OK);
}

// Clause B.1.1: HTML-like comments, in the Script goal only.
TEST(Html, OpenCommentInScript) {
  EXPECT_EQ(Sig(Lex("a <!-- c\nb")), "NAME NAME");
  EXPECT_EQ(Sig(Lex("<!-- c")), "");
  EXPECT_EQ(Sig(Lex("a<!--b\nc")), "NAME NAME");
}

TEST(Html, CloseCommentNeedsALineStart) {
  // At the start of the source, and after a terminator.
  EXPECT_EQ(Sig(Lex("--> c\na")), "NAME");
  EXPECT_EQ(Sig(Lex("a\n--> c\nb")), "NAME NAME");
  EXPECT_EQ(Sig(Lex("a\n  /* x */ --> c\nb")), "NAME NAME");
  // After a multi-line comment that holds a terminator.
  EXPECT_EQ(Sig(Lex("a /*\n*/ --> c\nb")), "NAME NAME");
  // Not mid-line: `a-->b` is `a --` `>` `b`.
  EXPECT_EQ(Sig(Lex("a-->b")), "NAME -- > NAME");
  EXPECT_EQ(Sig(Lex("a /* x */ --> b")), "NAME -- > NAME");
  // A token ends the line start.
  EXPECT_EQ(Sig(Lex("x\ny --> b")), "NAME NAME -- > NAME");
}

TEST(Html, NotCommentsInModule) {
  const uint32_t module = GLECMA_LEXF_MODULE;
  EXPECT_EQ(Sig(Lex("a <!-- b", module)), "NAME < ! -- NAME");
  EXPECT_EQ(Sig(Lex("a\n--> b", module)), "NAME -- > NAME");
  EXPECT_EQ(Sig(Lex("--> b", module)), "-- > NAME");
}

// Strictness does not change HTML-like comments; only the goal does.
TEST(Html, StrictScriptStillHasThem) {
  EXPECT_EQ(Sig(Lex("a <!-- b", GLECMA_LEXF_STRICT)), "NAME");
}

// Clauses 12.9.5 and 12.8: where a `/` is division and where it begins a regular
// expression is the goal the caller names.
TEST(Goals, DivisionOrRegularExpression) {
  GLECMA_Source * source = nullptr;
  ASSERT_EQ(glecma_source_create_utf8("/a/g", 4, nullptr, nullptr, nullptr,
                &source),
      GLECMA_OK);
  GLECMA_Lexer lexer;
  ASSERT_EQ(glecma_lexer_open(&lexer, source, nullptr, 0, nullptr), GLECMA_OK);
  GLECMA_Lexer saved = lexer;
  GLECMA_Token token;
  ASSERT_EQ(glecma_lexer_next(&lexer, GLECMA_GOAL_REGEXP, &token), GLECMA_OK);
  EXPECT_EQ(token.kind, GLECMA_TOK_REGEXP);
  EXPECT_EQ(token.end.offset, 4u);
  EXPECT_EQ(token.regexp_flags_offset, 3u);
  // The same position under the other goal reads `/` and the rest.
  lexer = saved;
  ASSERT_EQ(glecma_lexer_next(&lexer, GLECMA_GOAL_DIV, &token), GLECMA_OK);
  EXPECT_EQ(token.kind, GLECMA_TOK_SLASH);
  ASSERT_EQ(glecma_lexer_next(&lexer, GLECMA_GOAL_DIV, &token), GLECMA_OK);
  EXPECT_EQ(token.kind, GLECMA_TOK_NAME);
  glecma_source_release(source);
}

TEST(Goals, TheDriverGuessesBothWays) {
  EXPECT_EQ(Sig(Lex("a / b / c")), "NAME / NAME / NAME");
  EXPECT_EQ(Sig(Lex("a = /b/g")), "NAME = REGEXP");
  EXPECT_EQ(Sig(Lex("(a) / b")), "( NAME ) / NAME");
  EXPECT_EQ(Sig(Lex("return /x/")), "NAME REGEXP");
  EXPECT_EQ(Sig(Lex("x = [1] / 2")), "NAME = [ NUMBER ] / NUMBER");
  EXPECT_EQ(Sig(Lex("this / 2")), "NAME / NUMBER");
  EXPECT_EQ(Sig(Lex("/=/")), "REGEXP");
  EXPECT_EQ(Sig(Lex("a /= 2")), "NAME /= NUMBER");
}

// A `}` is a punctuator or the rest of a template by goal (clause 12.9.6).
TEST(Goals, TemplateTail) {
  GLECMA_Source * source = nullptr;
  const char text[] = "`a${x}b`";
  ASSERT_EQ(glecma_source_create_utf8(text, 8, nullptr, nullptr, nullptr,
                &source),
      GLECMA_OK);
  GLECMA_Lexer lexer;
  ASSERT_EQ(glecma_lexer_open(&lexer, source, nullptr, 0, nullptr), GLECMA_OK);
  GLECMA_Token t;
  ASSERT_EQ(glecma_lexer_next(&lexer, GLECMA_GOAL_REGEXP, &t), GLECMA_OK);
  EXPECT_EQ(t.template_part, GLECMA_TEMPLATE_HEAD);
  ASSERT_EQ(glecma_lexer_next(&lexer, GLECMA_GOAL_REGEXP, &t), GLECMA_OK);
  EXPECT_EQ(t.kind, GLECMA_TOK_NAME);
  GLECMA_Lexer before_brace = lexer;
  ASSERT_EQ(glecma_lexer_next(&lexer, GLECMA_GOAL_TEMPLATE_TAIL, &t),
      GLECMA_OK);
  EXPECT_EQ(t.kind, GLECMA_TOK_TEMPLATE);
  EXPECT_EQ(t.template_part, GLECMA_TEMPLATE_TAIL);
  lexer = before_brace;
  ASSERT_EQ(glecma_lexer_next(&lexer, GLECMA_GOAL_DIV, &t), GLECMA_OK);
  EXPECT_EQ(t.kind, GLECMA_TOK_RBRACE);
  // The combined goal reads a `}` as a template and a `/` as a regexp.
  lexer = before_brace;
  ASSERT_EQ(glecma_lexer_next(&lexer, GLECMA_GOAL_REGEXP_OR_TEMPLATE_TAIL, &t),
      GLECMA_OK);
  EXPECT_EQ(t.kind, GLECMA_TOK_TEMPLATE);
  glecma_source_release(source);
}

// A lexer is a plain value: a copy is a saved position, and the end of
// input answers again and again.
TEST(Goals, SaveRestoreAndTheEndOfInput) {
  GLECMA_Source * source = nullptr;
  ASSERT_EQ(glecma_source_create_utf8("a b", 3, nullptr, nullptr, nullptr,
                &source),
      GLECMA_OK);
  GLECMA_Lexer lexer;
  ASSERT_EQ(glecma_lexer_open(&lexer, source, nullptr, 0, nullptr), GLECMA_OK);
  GLECMA_Token t;
  ASSERT_EQ(glecma_lexer_next(&lexer, GLECMA_GOAL_DIV, &t), GLECMA_OK);
  GLECMA_Lexer saved = lexer;
  ASSERT_EQ(glecma_lexer_next(&lexer, GLECMA_GOAL_DIV, &t), GLECMA_OK);
  EXPECT_EQ(t.start.offset, 2u);
  lexer = saved;
  ASSERT_EQ(glecma_lexer_next(&lexer, GLECMA_GOAL_DIV, &t), GLECMA_OK);
  EXPECT_EQ(t.start.offset, 2u);
  for (int i = 0; i < 3; ++i) {
    ASSERT_EQ(glecma_lexer_next(&lexer, GLECMA_GOAL_DIV, &t), GLECMA_OK);
    EXPECT_EQ(t.kind, GLECMA_TOK_EOF);
    EXPECT_EQ(t.start.offset, 3u);
  }
  glecma_source_release(source);
}

// The tokenizer's positions are the source's: every token's start and end
// equal what the source's own location query answers.
TEST(Locations, MatchTheSourcesOwnQuery) {
  const std::vector<std::string> inputs = {
      "var a = 1;\nvar b = \"two\";\r\nfunction f(x) { return x * 3; }\n",
      "a\r\nb\rc\nd\xE2\x80\xA8" "e\xE2\x80\xA9" "f",
      "x = \"\xF0\x9F\x98\x80\" + "
      "`\xF0\x9F\x98\x80${x}\xE2\x80\xA8`;",
      "/* a\nb\r\nc */ x /* \xE2\x80\xA8 */ y // z\n w",
      "`line1\nline2\r\nline3` + `a${`b${c}d`}e`",
      "\xC3\xA9\xCE\xB1 \xE2\x84\x98 \xF0\xA0\xAE\xB7 12.5e3 0x1F 'a\\\nb'",
      "x = /[/\\n]+/gi.test(y) ? a\n/ b : c",
  };
  for (const std::string & input : inputs) {
    for (int wide = 0; wide < 2; ++wide) {
      Lexed lexed = wide ? LexUtf16(input) : LexUtf8(input);
      ASSERT_EQ(lexed.result, GLECMA_OK) << input;
      GLECMA_Source * source = nullptr;
      std::vector<uint16_t> units = ToUtf16(input);
      ASSERT_EQ(wide ? glecma_source_create_utf16(units.data(), units.size(),
                           nullptr, nullptr, nullptr, &source)
                     : glecma_source_create_utf8(input.data(), input.size(),
                           nullptr, nullptr, nullptr, &source),
          GLECMA_OK);
      for (const Tok & t : lexed.tokens) {
        GLECMA_Location expected;
        ASSERT_EQ(glecma_source_location(source, t.start.offset, &expected),
            GLECMA_OK);
        EXPECT_EQ(expected.line, t.start.line) << input;
        EXPECT_EQ(expected.column, t.start.column) << input;
        ASSERT_EQ(glecma_source_location(source, t.end.offset, &expected),
            GLECMA_OK);
        EXPECT_EQ(expected.line, t.end.line) << input;
        EXPECT_EQ(expected.column, t.end.column) << input;
      }
      glecma_source_release(source);
    }
  }
}

// A lone surrogate in UTF-16 source is a code point of its own: inside a
// string it lexes, and anywhere else it begins no token.
TEST(Locations, LoneSurrogatesInUtf16) {
  const uint16_t in_string[] = {'"', 0xD800, '"'};
  GLECMA_Source * source = nullptr;
  ASSERT_EQ(glecma_source_create_utf16(in_string, 3, nullptr, nullptr, nullptr,
                &source),
      GLECMA_OK);
  GLECMA_Lexer lexer;
  ASSERT_EQ(glecma_lexer_open(&lexer, source, nullptr, 0, nullptr), GLECMA_OK);
  GLECMA_Token t;
  ASSERT_EQ(glecma_lexer_next(&lexer, GLECMA_GOAL_DIV, &t), GLECMA_OK);
  EXPECT_EQ(t.kind, GLECMA_TOK_STRING);
  uint16_t * units = nullptr;
  size_t length = 0;
  ASSERT_EQ(glecma_token_value(source, &t, GLECMA_VALUE_COOKED, nullptr,
                &units, &length),
      GLECMA_OK);
  ASSERT_EQ(length, 1u);
  EXPECT_EQ(units[0], 0xD800);
  glecma_token_value_free(nullptr, units);
  glecma_source_release(source);

  const uint16_t bare[] = {'a', 0xDC00, 'b'};
  ASSERT_EQ(glecma_source_create_utf16(bare, 3, nullptr, nullptr, nullptr,
                &source),
      GLECMA_OK);
  ASSERT_EQ(glecma_lexer_open(&lexer, source, nullptr, 0, nullptr), GLECMA_OK);
  ASSERT_EQ(glecma_lexer_run(&lexer, nullptr, nullptr), GLECMA_ERR_SYNTAX);
  GLECMA_Failure failure = GLECMA_FAILURE_INIT;
  glecma_lexer_failure(&lexer, &failure);
  EXPECT_EQ(failure.rule,
      static_cast<uint32_t>(GLECMA_RULE_UNEXPECTED_CHARACTER));
  EXPECT_EQ(failure.location.offset, 1u);
  EXPECT_EQ(failure.location.column, 1u);
  glecma_source_release(source);
}

// One source lexed by many threads at once: a source is immutable, so this
// needs no lock (run under TSan by `make test-tsan`).
TEST(Threads, ManyLexersOneSource) {
  std::string text;
  for (int i = 0; i < 2000; ++i) {
    text += "var v" + std::to_string(i) + " = \"s\\n\" + 0x" +
        std::to_string(i) + " / 2 + `t${i}`;\n";
  }
  GLECMA_Source * source = nullptr;
  ASSERT_EQ(glecma_source_create_utf8(text.data(), text.size(), nullptr,
                nullptr, nullptr, &source),
      GLECMA_OK);
  std::vector<size_t> counts(8, 0);
  std::vector<std::thread> threads;
  for (size_t t = 0; t < counts.size(); ++t) {
    threads.emplace_back([&, t] {
      GLECMA_Lexer lexer;
      if (glecma_lexer_open(&lexer, source, nullptr, 0, nullptr) !=
          GLECMA_OK) {
        return;
      }
      glecma_lexer_run(&lexer,
          [](void * c, const GLECMA_Token *) {
            ++*static_cast<size_t *>(c);
            return true;
          },
          &counts[t]);
    });
  }
  for (auto & th : threads) {
    th.join();
  }
  for (size_t c : counts) {
    EXPECT_EQ(c, counts[0]);
    EXPECT_GT(c, 20000u);
  }
  glecma_source_release(source);
}

int main(int argc, char * * argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

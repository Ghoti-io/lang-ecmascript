// SPDX-License-Identifier: LGPL-3.0-only
//
// Shared by the tokenizer's tests: lexing a string through the driver in
// both encodings and comparing, so that every test of what the tokenizer
// reads is also a test that UTF-8 and UTF-16 sources read the same.

#ifndef GHOTI_IO_GLECMA_TESTS_LEX_TEST_SUPPORT_H
#define GHOTI_IO_GLECMA_TESTS_LEX_TEST_SUPPORT_H

#include <ghoti.io/lang-ecmascript/lang-ecmascript.h>

#include "lex/lex_internal.h"
#include "lex/lexer.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace glecma_test {

struct Tok {
  GLECMA_TokenKind kind;
  GLECMA_Keyword keyword;
  GLECMA_TemplatePart part;
  uint32_t flags;
  uint32_t escape_rule;
  GLECMA_Location start;
  GLECMA_Location end;
  size_t regexp_flags_offset;
  std::string text;  // The token's source text, as UTF-8.
};

struct Lexed {
  GLECMA_Result result = GLECMA_OK;
  GLECMA_Failure failure = GLECMA_FAILURE_INIT;
  std::vector<Tok> tokens;  // The end of input is the last token on success.
};

// UTF-8 to UTF-16 for test inputs, which are well-formed.
inline std::vector<uint16_t> ToUtf16(const std::string & text) {
  std::vector<uint16_t> out;
  for (size_t i = 0; i < text.size();) {
    unsigned char c = static_cast<unsigned char>(text[i]);
    uint32_t cp;
    size_t len;
    if (c < 0x80) { cp = c; len = 1; }
    else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
    else { cp = c & 0x07; len = 4; }
    for (size_t k = 1; k < len; ++k) {
      cp = (cp << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3F);
    }
    i += len;
    if (cp >= 0x10000) {
      cp -= 0x10000;
      out.push_back(static_cast<uint16_t>(0xD800 + (cp >> 10)));
      out.push_back(static_cast<uint16_t>(0xDC00 + (cp & 0x3FF)));
    } else {
      out.push_back(static_cast<uint16_t>(cp));
    }
  }
  return out;
}

inline std::string Utf8Of(uint32_t cp) {
  std::string s;
  if (cp < 0x80) {
    s += static_cast<char>(cp);
  } else if (cp < 0x800) {
    s += static_cast<char>(0xC0 | (cp >> 6));
    s += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    s += static_cast<char>(0xE0 | (cp >> 12));
    s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    s += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    s += static_cast<char>(0xF0 | (cp >> 18));
    s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    s += static_cast<char>(0x80 | (cp & 0x3F));
  }
  return s;
}

struct CollectContext {
  Lexed * out;
  const std::string * utf8;
};

inline bool Collect(void * context, const GLECMA_Token * token) {
  auto * c = static_cast<CollectContext *>(context);
  Tok t;
  t.kind = token->kind;
  t.keyword = token->keyword;
  t.part = token->template_part;
  t.flags = token->flags;
  t.escape_rule = token->escape_rule;
  t.start = token->start;
  t.end = token->end;
  t.regexp_flags_offset = token->regexp_flags_offset;
  if (c->utf8 != nullptr &&
      token->end.offset <= c->utf8->size()) {
    t.text = c->utf8->substr(token->start.offset,
        token->end.offset - token->start.offset);
  }
  c->out->tokens.push_back(t);
  return true;
}

enum Entry { kPublic, kBuiltin };

// Lexes `text` (UTF-8) to the end or the first failure through the driver.
inline Lexed LexUtf8(const std::string & text, uint32_t mode = 0,
    const GLECMA_Limits * limits = nullptr, Entry entry = kPublic) {
  Lexed out;
  GLECMA_Source * source = nullptr;
  EXPECT_EQ(glecma_source_create_utf8(text.data(), text.size(), nullptr,
                nullptr, nullptr, &source),
      GLECMA_OK);
  if (source == nullptr) {
    out.result = GLECMA_ERR_INTERNAL;
    return out;
  }
  GLECMA_ParseOptions options = GLECMA_PARSE_OPTIONS_INIT;
  options.limits = limits;
  GLECMA_Lexer lexer;
  GLECMA_Failure open_failure = GLECMA_FAILURE_INIT;
  GLECMA_Result opened =
      entry == kBuiltin
      ? glecma_lexer_open_builtin(&lexer, source, &options, &open_failure)
      : glecma_lexer_open(&lexer, source, &options, mode, &open_failure);
  EXPECT_EQ(opened, GLECMA_OK);
  if (opened != GLECMA_OK) {
    glecma_source_release(source);
    out.result = opened;
    return out;
  }
  CollectContext context{&out, &text};
  out.result = glecma_lexer_run(&lexer, Collect, &context);
  glecma_lexer_failure(&lexer, &out.failure);
  glecma_source_release(source);
  return out;
}

inline Lexed LexUtf16(const std::string & text, uint32_t mode = 0,
    const GLECMA_Limits * limits = nullptr, Entry entry = kPublic) {
  Lexed out;
  std::vector<uint16_t> units = ToUtf16(text);
  GLECMA_Source * source = nullptr;
  EXPECT_EQ(glecma_source_create_utf16(units.data(), units.size(), nullptr,
                nullptr, nullptr, &source),
      GLECMA_OK);
  if (source == nullptr) {
    out.result = GLECMA_ERR_INTERNAL;
    return out;
  }
  GLECMA_ParseOptions options = GLECMA_PARSE_OPTIONS_INIT;
  options.limits = limits;
  GLECMA_Lexer lexer;
  GLECMA_Result opened =
      entry == kBuiltin
      ? glecma_lexer_open_builtin(&lexer, source, &options, nullptr)
      : glecma_lexer_open(&lexer, source, &options, mode, nullptr);
  EXPECT_EQ(opened, GLECMA_OK);
  if (opened != GLECMA_OK) {
    glecma_source_release(source);
    out.result = opened;
    return out;
  }
  CollectContext context{&out, nullptr};
  out.result = glecma_lexer_run(&lexer, Collect, &context);
  glecma_lexer_failure(&lexer, &out.failure);
  glecma_source_release(source);
  return out;
}

// Lexes in both encodings and requires the same tokens, flags, lines and
// columns and the same failure; returns the UTF-8 result.
inline Lexed Lex(const std::string & text, uint32_t mode = 0,
    const GLECMA_Limits * limits = nullptr, Entry entry = kPublic) {
  Lexed a = LexUtf8(text, mode, limits, entry);
  Lexed b = LexUtf16(text, mode, limits, entry);
  EXPECT_EQ(a.result, b.result) << text;
  EXPECT_EQ(a.tokens.size(), b.tokens.size()) << text;
  size_t n = a.tokens.size() < b.tokens.size() ? a.tokens.size()
                                               : b.tokens.size();
  for (size_t i = 0; i < n; ++i) {
    EXPECT_EQ(a.tokens[i].kind, b.tokens[i].kind) << text << " #" << i;
    EXPECT_EQ(a.tokens[i].keyword, b.tokens[i].keyword) << text << " #" << i;
    EXPECT_EQ(a.tokens[i].flags, b.tokens[i].flags) << text << " #" << i;
    EXPECT_EQ(a.tokens[i].part, b.tokens[i].part) << text << " #" << i;
    EXPECT_EQ(a.tokens[i].escape_rule, b.tokens[i].escape_rule)
        << text << " #" << i;
    EXPECT_EQ(a.tokens[i].start.line, b.tokens[i].start.line)
        << text << " #" << i;
    EXPECT_EQ(a.tokens[i].start.column, b.tokens[i].start.column)
        << text << " #" << i;
    EXPECT_EQ(a.tokens[i].end.line, b.tokens[i].end.line) << text << " #" << i;
    EXPECT_EQ(a.tokens[i].end.column, b.tokens[i].end.column)
        << text << " #" << i;
  }
  EXPECT_EQ(a.failure.result, b.failure.result) << text;
  EXPECT_EQ(a.failure.rule, b.failure.rule) << text;
  EXPECT_EQ(a.failure.location.line, b.failure.location.line) << text;
  EXPECT_EQ(a.failure.location.column, b.failure.location.column) << text;
  EXPECT_STREQ(a.failure.limit == nullptr ? "" : a.failure.limit,
      b.failure.limit == nullptr ? "" : b.failure.limit) << text;
  return a;
}

// "a + 1" -> "NAME + NUMBER": kinds in order, the end of input left out.
inline std::string Sig(const Lexed & lexed) {
  std::string out;
  for (const Tok & t : lexed.tokens) {
    if (t.kind == GLECMA_TOK_EOF) {
      continue;
    }
    if (!out.empty()) {
      out += ' ';
    }
    out += glecma_token_kind_name(t.kind);
  }
  return out;
}

inline std::string SigOf(const std::string & text, uint32_t mode = 0) {
  return Sig(Lex(text, mode));
}

// The value of a token of a UTF-8 source, as UTF-8 for comparison.
inline std::string ValueOf(const std::string & text, size_t token_index,
    GLECMA_ValueKind kind = GLECMA_VALUE_COOKED, uint32_t mode = 0,
    GLECMA_Result * result_out = nullptr) {
  std::vector<uint16_t> units16 = ToUtf16(text);
  GLECMA_Source * source = nullptr;
  EXPECT_EQ(glecma_source_create_utf16(units16.data(), units16.size(), nullptr,
                nullptr, nullptr, &source),
      GLECMA_OK);
  Lexed lexed;
  std::vector<GLECMA_Token> tokens;
  GLECMA_Lexer lexer;
  EXPECT_EQ(glecma_lexer_open(&lexer, source, nullptr, mode, nullptr),
      GLECMA_OK);
  struct Ctx { std::vector<GLECMA_Token> * tokens; } ctx{&tokens};
  GLECMA_Result run = glecma_lexer_run(&lexer,
      [](void * c, const GLECMA_Token * t) {
        static_cast<Ctx *>(c)->tokens->push_back(*t);
        return true;
      }, &ctx);
  EXPECT_EQ(run, GLECMA_OK) << text;
  std::string out;
  if (token_index < tokens.size()) {
    uint16_t * units = nullptr;
    size_t length = 0;
    GLECMA_Result r = glecma_token_value(source, &tokens[token_index], kind,
        nullptr, &units, &length);
    if (result_out != nullptr) {
      *result_out = r;
    }
    if (r == GLECMA_OK) {
      for (size_t i = 0; i < length; ++i) {
        uint32_t cp = units[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < length &&
            units[i + 1] >= 0xDC00 && units[i + 1] <= 0xDFFF) {
          cp = 0x10000 + ((cp - 0xD800) << 10) + (units[i + 1] - 0xDC00);
          ++i;
        }
        if (cp >= 0xD800 && cp <= 0xDFFF) {
          // A lone surrogate has no UTF-8; show it as \uXXXX.
          char buf[16];
          std::snprintf(buf, sizeof(buf), "\\u%04X", cp);
          out += buf;
        } else {
          out += Utf8Of(cp);
        }
      }
      glecma_token_value_free(nullptr, units);
    }
  }
  glecma_source_release(source);
  return out;
}

}  // namespace glecma_test

#endif  // GHOTI_IO_GLECMA_TESTS_LEX_TEST_SUPPORT_H

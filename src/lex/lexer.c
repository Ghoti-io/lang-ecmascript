/*
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * Copyright (C) 2026 Corey Pennycuff
 *
 * This file is part of Ghoti.io Lang-ecmascript.
 *
 * Ghoti.io Lang-ecmascript is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License version 3
 * as published by the Free Software Foundation.
 *
 * Ghoti.io Lang-ecmascript is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file
 *
 * The tokenizer: opening, trivia (white space, line terminators and
 * comments), names and keywords, punctuators, and the driver that follows
 * the goals by a guess for tests and fuzzers. The literals are in
 * literal.c.
 */

#include <ghoti.io/lang-ecmascript/macros.h>

#include "core/core_internal.h"
#include "lex/lex_internal.h"

#include <ghoti.io/unicode/char.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ---- Character classes --------------------------------------------------- */

bool glecma_lex_is_id_start(uint32_t cp) {
  if (cp < 128u) {
    return (glecma_lex_ascii_class[cp] & GLECMA_LEXC_START) != 0;
  }
  return guni_has_property(cp, GUNI_PROP_ID_START);
}

bool glecma_lex_is_id_part(uint32_t cp) {
  if (cp < 128u) {
    return (glecma_lex_ascii_class[cp] & GLECMA_LEXC_PART) != 0;
  }
  // ZWNJ and ZWJ are IdentifierPartChars (clause 12.7); they are not ID_Continue.
  return cp == 0x200Cu || cp == 0x200Du ||
      guni_has_property(cp, GUNI_PROP_ID_CONTINUE);
}

// WhiteSpace (clause 12.2): TAB, VT, FF, ZWNBSP and any Space_Separator (which
// includes SP and NBSP).
static bool is_white_space(uint32_t cp) {
  if (cp < 128u) {
    return cp == 0x20u || cp == 0x09u || cp == 0x0Bu || cp == 0x0Cu;
  }
  return cp == 0xA0u || cp == 0xFEFFu ||
      (cp >= 0x1680u && guni_general_category(cp) == GUNI_GC_ZS);
}

/* ---- Keywords ------------------------------------------------------------ */

typedef struct {
  const char * text;
  GLECMA_Keyword keyword;
} KeywordRow;

// Sorted by text, for the binary search; a test checks the order.
static const KeywordRow keywords[] = {
  {"arguments", GLECMA_KW_ARGUMENTS}, {"as", GLECMA_KW_AS},
  {"async", GLECMA_KW_ASYNC}, {"await", GLECMA_KW_AWAIT},
  {"break", GLECMA_KW_BREAK}, {"case", GLECMA_KW_CASE},
  {"catch", GLECMA_KW_CATCH}, {"class", GLECMA_KW_CLASS},
  {"const", GLECMA_KW_CONST}, {"continue", GLECMA_KW_CONTINUE},
  {"debugger", GLECMA_KW_DEBUGGER}, {"default", GLECMA_KW_DEFAULT},
  {"delete", GLECMA_KW_DELETE}, {"do", GLECMA_KW_DO},
  {"else", GLECMA_KW_ELSE}, {"enum", GLECMA_KW_ENUM},
  {"eval", GLECMA_KW_EVAL}, {"export", GLECMA_KW_EXPORT},
  {"extends", GLECMA_KW_EXTENDS}, {"false", GLECMA_KW_FALSE},
  {"finally", GLECMA_KW_FINALLY}, {"for", GLECMA_KW_FOR},
  {"from", GLECMA_KW_FROM}, {"function", GLECMA_KW_FUNCTION},
  {"get", GLECMA_KW_GET}, {"if", GLECMA_KW_IF},
  {"implements", GLECMA_KW_IMPLEMENTS}, {"import", GLECMA_KW_IMPORT},
  {"in", GLECMA_KW_IN}, {"instanceof", GLECMA_KW_INSTANCEOF},
  {"interface", GLECMA_KW_INTERFACE}, {"let", GLECMA_KW_LET},
  {"meta", GLECMA_KW_META}, {"new", GLECMA_KW_NEW},
  {"null", GLECMA_KW_NULL}, {"of", GLECMA_KW_OF},
  {"package", GLECMA_KW_PACKAGE}, {"private", GLECMA_KW_PRIVATE},
  {"protected", GLECMA_KW_PROTECTED}, {"public", GLECMA_KW_PUBLIC},
  {"return", GLECMA_KW_RETURN}, {"set", GLECMA_KW_SET},
  {"static", GLECMA_KW_STATIC}, {"super", GLECMA_KW_SUPER},
  {"switch", GLECMA_KW_SWITCH}, {"target", GLECMA_KW_TARGET},
  {"this", GLECMA_KW_THIS}, {"throw", GLECMA_KW_THROW},
  {"true", GLECMA_KW_TRUE}, {"try", GLECMA_KW_TRY},
  {"typeof", GLECMA_KW_TYPEOF}, {"var", GLECMA_KW_VAR},
  {"void", GLECMA_KW_VOID}, {"while", GLECMA_KW_WHILE},
  {"with", GLECMA_KW_WITH}, {"yield", GLECMA_KW_YIELD},
};

#define KEYWORD_ROWS (sizeof(keywords) / sizeof(keywords[0]))
#define KEYWORD_MAX_LENGTH 10u // "instanceof", "implements"

static GLECMA_Keyword keyword_lookup(const char * text, size_t length) {
  size_t low = 0;
  size_t high = KEYWORD_ROWS;
  while (low < high) {
    size_t middle = low + (high - low) / 2;
    int order = strncmp(text, keywords[middle].text, length);
    if (order == 0) {
      order = keywords[middle].text[length] == '\0' ? 0 : -1;
    }
    if (order == 0) {
      return keywords[middle].keyword;
    }
    if (order < 0) {
      high = middle;
    } else {
      low = middle + 1;
    }
  }
  return GLECMA_KW_NONE;
}

const char * glecma_keyword_text(GLECMA_Keyword keyword) {
  for (size_t i = 0; i < KEYWORD_ROWS; i++) {
    if (keywords[i].keyword == keyword) {
      return keywords[i].text;
    }
  }
  return NULL;
}

// Reserved words (never an Identifier) are the keywords up to `with`; the
// two context-dependent words and the strict-mode ones follow.
static bool keyword_always_reserved(GLECMA_Keyword keyword) {
  return keyword >= GLECMA_KW_BREAK && keyword <= GLECMA_KW_WITH;
}

static bool keyword_strict_reserved(GLECMA_Keyword keyword) {
  return keyword >= GLECMA_KW_IMPLEMENTS && keyword <= GLECMA_KW_STATIC;
}

uint32_t glecma_token_identifier_check(const GLECMA_Token * token,
    uint32_t context) {
  if (token == NULL || token->kind != GLECMA_TOK_NAME) {
    return GLECMA_RULE_NONE;
  }
  GLECMA_Keyword keyword = token->keyword;
  bool reserved = keyword_always_reserved(keyword) ||
      (keyword == GLECMA_KW_YIELD &&
          (context & (GLECMA_IDF_STRICT | GLECMA_IDF_YIELD)) != 0) ||
      (keyword == GLECMA_KW_AWAIT && (context & GLECMA_IDF_AWAIT) != 0) ||
      (keyword_strict_reserved(keyword) && (context & GLECMA_IDF_STRICT) != 0);
  if (!reserved) {
    return GLECMA_RULE_NONE;
  }
  if ((token->flags & GLECMA_TOKF_ESCAPED) != 0) {
    return GLECMA_RULE_ESCAPED_RESERVED_WORD;
  }
  return keyword_always_reserved(keyword) ? GLECMA_RULE_RESERVED_WORD
                                          : GLECMA_RULE_RESERVED_WORD_STRICT;
}

/* ---- Punctuator and kind names ------------------------------------------- */

static const char * const kind_names[GLECMA_TOK_COUNT] = {
  [GLECMA_TOK_EOF] = "EOF", [GLECMA_TOK_NAME] = "NAME",
  [GLECMA_TOK_PRIVATE_NAME] = "PRIVATE_NAME",
  [GLECMA_TOK_INTRINSIC_NAME] = "INTRINSIC_NAME",
  [GLECMA_TOK_NUMBER] = "NUMBER", [GLECMA_TOK_BIGINT] = "BIGINT",
  [GLECMA_TOK_STRING] = "STRING", [GLECMA_TOK_TEMPLATE] = "TEMPLATE",
  [GLECMA_TOK_REGEXP] = "REGEXP",
  [GLECMA_TOK_LBRACE] = "{", [GLECMA_TOK_RBRACE] = "}",
  [GLECMA_TOK_LPAREN] = "(", [GLECMA_TOK_RPAREN] = ")",
  [GLECMA_TOK_LBRACKET] = "[", [GLECMA_TOK_RBRACKET] = "]",
  [GLECMA_TOK_DOT] = ".", [GLECMA_TOK_ELLIPSIS] = "...",
  [GLECMA_TOK_SEMICOLON] = ";", [GLECMA_TOK_COMMA] = ",",
  [GLECMA_TOK_LT] = "<", [GLECMA_TOK_GT] = ">", [GLECMA_TOK_LE] = "<=",
  [GLECMA_TOK_GE] = ">=", [GLECMA_TOK_EQ] = "==", [GLECMA_TOK_NE] = "!=",
  [GLECMA_TOK_SEQ] = "===", [GLECMA_TOK_SNE] = "!==",
  [GLECMA_TOK_PLUS] = "+", [GLECMA_TOK_MINUS] = "-", [GLECMA_TOK_STAR] = "*",
  [GLECMA_TOK_SLASH] = "/", [GLECMA_TOK_PERCENT] = "%",
  [GLECMA_TOK_STARSTAR] = "**", [GLECMA_TOK_INC] = "++",
  [GLECMA_TOK_DEC] = "--", [GLECMA_TOK_SHL] = "<<", [GLECMA_TOK_SHR] = ">>",
  [GLECMA_TOK_USHR] = ">>>", [GLECMA_TOK_AMP] = "&", [GLECMA_TOK_PIPE] = "|",
  [GLECMA_TOK_CARET] = "^", [GLECMA_TOK_BANG] = "!", [GLECMA_TOK_TILDE] = "~",
  [GLECMA_TOK_ANDAND] = "&&", [GLECMA_TOK_OROR] = "||",
  [GLECMA_TOK_QQ] = "??", [GLECMA_TOK_QUESTION] = "?",
  [GLECMA_TOK_QDOT] = "?.", [GLECMA_TOK_COLON] = ":",
  [GLECMA_TOK_ASSIGN] = "=", [GLECMA_TOK_PLUS_ASSIGN] = "+=",
  [GLECMA_TOK_MINUS_ASSIGN] = "-=", [GLECMA_TOK_STAR_ASSIGN] = "*=",
  [GLECMA_TOK_SLASH_ASSIGN] = "/=", [GLECMA_TOK_PERCENT_ASSIGN] = "%=",
  [GLECMA_TOK_STARSTAR_ASSIGN] = "**=", [GLECMA_TOK_SHL_ASSIGN] = "<<=",
  [GLECMA_TOK_SHR_ASSIGN] = ">>=", [GLECMA_TOK_USHR_ASSIGN] = ">>>=",
  [GLECMA_TOK_AMP_ASSIGN] = "&=", [GLECMA_TOK_PIPE_ASSIGN] = "|=",
  [GLECMA_TOK_CARET_ASSIGN] = "^=", [GLECMA_TOK_ANDAND_ASSIGN] = "&&=",
  [GLECMA_TOK_OROR_ASSIGN] = "||=", [GLECMA_TOK_QQ_ASSIGN] = "?\?=",
  [GLECMA_TOK_ARROW] = "=>",
};

const char * glecma_token_kind_name(GLECMA_TokenKind kind) {
  if ((unsigned)kind >= (unsigned)GLECMA_TOK_COUNT) {
    return "?";
  }
  return kind_names[kind];
}

const char * glecma_token_punctuator(GLECMA_TokenKind kind) {
  if (kind < GLECMA_TOK_LBRACE || kind >= GLECMA_TOK_COUNT) {
    return NULL;
  }
  return kind_names[kind];
}

/* ---- Opening ------------------------------------------------------------- */

static GLECMA_Result open_lexer(GLECMA_Lexer * lexer,
    const GLECMA_Source * source, const GLECMA_ParseOptions * options,
    uint32_t mode, GLECMA_Failure * failure) {
  if (failure != NULL && failure->size < GLECMA_FAILURE_HEADER_SIZE) {
    return GLECMA_ERR_INVALID;
  }
  if (lexer == NULL || source == NULL) {
    glecma_failure_set(failure, GLECMA_ERR_INVALID,
        "lexer or source is NULL", NULL, NULL, 0, NULL);
    return GLECMA_ERR_INVALID;
  }
  GLECMA_Limits limits;
  const GLECMA_FeatureSet * features = NULL;
  if (!glecma_parse_options_resolve(options, &limits, &features)) {
    glecma_failure_set(failure, GLECMA_ERR_INVALID,
        "options or limits has a size below the header", NULL, NULL, 0, NULL);
    return GLECMA_ERR_INVALID;
  }
  // Before any token is read: a feature this build lacks is refused, never
  // ignored.
  const char * unbuilt = glecma_feature_set_first_unbuilt(features);
  if (unbuilt != NULL) {
    glecma_failure_set(failure, GLECMA_ERR_UNSUPPORTED,
        "the feature set names a feature this build does not define", NULL,
        NULL, 0, unbuilt);
    return GLECMA_ERR_UNSUPPORTED;
  }
  if ((mode & GLECMA_LEXF_MODULE) != 0) {
    mode |= GLECMA_LEXF_STRICT;
  }
  memset(lexer, 0, sizeof(*lexer));
  lexer->source = source;
  lexer->position.line = 1;
  lexer->max_tokens = limits.tokens;
  lexer->max_literal = limits.literal_length;
  lexer->mode = mode;
  lexer->line_start = true;
  lexer->error.result = GLECMA_OK;
  glecma_failure_set(failure, GLECMA_OK, NULL, NULL, NULL, 0, NULL);
  return GLECMA_OK;
}

GLECMA_Result glecma_lexer_open(GLECMA_Lexer * lexer,
    const GLECMA_Source * source, const GLECMA_ParseOptions * options,
    uint32_t mode, GLECMA_Failure * failure) {
  if ((mode & ~(GLECMA_LEXF_STRICT | GLECMA_LEXF_MODULE)) != 0) {
    // The built-in bit is not a public option; no other bit exists.
    if (failure != NULL && failure->size >= GLECMA_FAILURE_HEADER_SIZE) {
      glecma_failure_set(failure, GLECMA_ERR_INVALID, "unknown lexer mode",
          NULL, NULL, 0, NULL);
    }
    return GLECMA_ERR_INVALID;
  }
  return open_lexer(lexer, source, options, mode, failure);
}

GLECMA_Result glecma_lexer_open_builtin(GLECMA_Lexer * lexer,
    const GLECMA_Source * source, const GLECMA_ParseOptions * options,
    GLECMA_Failure * failure) {
  return open_lexer(lexer, source, options,
      GLECMA_LEXF_BUILTIN | GLECMA_LEXF_STRICT, failure);
}

void glecma_lexer_set_strict(GLECMA_Lexer * lexer, bool strict) {
  if (strict) {
    lexer->mode |= GLECMA_LEXF_STRICT;
  } else if ((lexer->mode & GLECMA_LEXF_MODULE) == 0) {
    lexer->mode &= ~GLECMA_LEXF_STRICT;
  }
}

void glecma_lexer_reset(GLECMA_Lexer * lexer, const GLECMA_Location * where) {
  lexer->position = *where;
  lexer->line_start = false;
  memset(&lexer->error, 0, sizeof(lexer->error));
  lexer->error.result = GLECMA_OK;
}

GLECMA_Result glecma_lexer_set_range(GLECMA_Lexer * lexer, size_t start,
    size_t end) {
  if (lexer == NULL || start >= end || end > lexer->source->units) {
    return GLECMA_ERR_INVALID;
  }
  GLECMA_Location from = {0, 0, 0};
  if (glecma_source_location(lexer->source, start, &from) != GLECMA_OK) {
    return GLECMA_ERR_INVALID;
  }
  GLECMA_Location to = {0, 0, 0};
  if (glecma_source_location(lexer->source, end, &to) != GLECMA_OK) {
    return GLECMA_ERR_INVALID;
  }
  glecma_lexer_reset(lexer, &from);
  lexer->bounded = true;
  lexer->limit = end;
  return GLECMA_OK;
}

void glecma_lexer_failure(const GLECMA_Lexer * lexer,
    GLECMA_Failure * failure) {
  glecma_failure_set(failure, lexer->error.result, lexer->error.message,
      &lexer->error.location, lexer->error.limit, lexer->error.rule, NULL);
}

/* ---- Trivia -------------------------------------------------------------- */

// Skips white space, line terminators and comments. Leaves the lexer's
// position at the first unit of the next token (or the end of text) and
// reports whether a line terminator was crossed.
static GLECMA_Result skip_trivia(GLECMA_Lexer * lexer,
    const GLECMA_LexText * text, bool * newline) {
  size_t offset = lexer->position.offset;
  const bool html = (lexer->mode & GLECMA_LEXF_MODULE) == 0;
  for (;;) {
    uint32_t u = glecma_lex_unit(text, offset);
    if (u < 0x80u) {
      if (u == ' ' || u == '\t' || u == 0x0Bu || u == 0x0Cu) {
        offset++;
        continue;
      }
      if (u == '\n' || u == '\r') {
        *newline = true;
        lexer->line_start = true;
        offset++;
        continue;
      }
      if (u == '/') {
        uint32_t next = glecma_lex_unit(text, offset + 1);
        if (next == '/') {
          offset += 2;
          goto single_line;
        }
        if (next == '*') {
          glecma_lex_walk(text, &lexer->position, offset);
          size_t scan = offset + 2;
          bool has_newline = false;
          for (;;) {
            uint32_t c = glecma_lex_unit(text, scan);
            if (c == GLECMA_LEX_NONE) {
              return glecma_lex_syntax_error(lexer, text,
                  GLECMA_RULE_UNTERMINATED_COMMENT, offset);
            }
            if (c == '*' && glecma_lex_unit(text, scan + 1) == '/') {
              scan += 2;
              break;
            }
            if (c == '\n' || c == '\r') {
              has_newline = true;
            } else if (c >= 0x80u) {
              size_t length = 0;
              uint32_t cp = glecma_lex_cp(text, scan, &length);
              if (cp == 0x2028u || cp == 0x2029u) {
                has_newline = true;
              }
              scan += length == 0 ? 1 : length;
              continue;
            }
            scan++;
          }
          if (has_newline) {
            *newline = true;
            lexer->line_start = true;
          }
          offset = scan;
          continue;
        }
        break;
      }
      if (html && u == '<' && glecma_lex_unit(text, offset + 1) == '!' &&
          glecma_lex_unit(text, offset + 2) == '-' &&
          glecma_lex_unit(text, offset + 3) == '-') {
        offset += 4; // `<!--`: SingleLineHTMLOpenComment (clause B.1.1)
        goto single_line;
      }
      if (html && u == '-' && lexer->line_start &&
          glecma_lex_unit(text, offset + 1) == '-' &&
          glecma_lex_unit(text, offset + 2) == '>') {
        offset += 3; // `-->`: HTMLCloseComment, at the start of a line (clause B.1.1)
        goto single_line;
      }
      if (u == '#' && offset == 0 && glecma_lex_unit(text, 1) == '!') {
        offset += 2; // HashbangComment (clause 12.5)
        goto single_line;
      }
      break;
    }
    // A non-ASCII code point: white space, a line terminator, or a token.
    size_t length = 0;
    uint32_t cp = glecma_lex_cp(text, offset, &length);
    if (cp == 0x2028u || cp == 0x2029u) {
      *newline = true;
      lexer->line_start = true;
      offset += length;
      continue;
    }
    if (cp != GLECMA_LEX_NONE && cp != GLECMA_LEX_BAD && is_white_space(cp)) {
      offset += length;
      continue;
    }
    break;
  single_line:
    // To the end of the line; the terminator itself is the next trivia.
    for (;;) {
      uint32_t c = glecma_lex_unit(text, offset);
      if (c == GLECMA_LEX_NONE || c == '\n' || c == '\r') {
        break;
      }
      if (c >= 0x80u) {
        size_t clen = 0;
        uint32_t cp2 = glecma_lex_cp(text, offset, &clen);
        if (cp2 == 0x2028u || cp2 == 0x2029u) {
          break;
        }
        offset += clen == 0 ? 1 : clen;
        continue;
      }
      offset++;
    }
  }
  glecma_lex_walk(text, &lexer->position, offset);
  return GLECMA_OK;
}

/* ---- Names --------------------------------------------------------------- */

// Scans an IdentifierName whose first unit is at `begin`: letters, digits,
// `$`, `_`, non-ASCII ID_Start/ID_Continue, and `\u` escapes (not in an
// intrinsic's name). `prefix` is the units already consumed that count toward
// the literal length (the `#` or `%`).
static GLECMA_Result scan_name(GLECMA_Lexer * lexer,
    const GLECMA_LexText * text, size_t token_start, size_t begin,
    bool escapes, GLECMA_Token * token, size_t * end) {
  size_t offset = begin;
  uint64_t count = begin - token_start; // The prefix, one unit.
  const uint64_t max = lexer->max_literal;
  bool first = true;
  bool escaped = false;
  bool ascii = true;
  char word[KEYWORD_MAX_LENGTH + 1];
  size_t word_length = 0;
  for (;;) {
    uint32_t u = glecma_lex_unit(text, offset);
    uint32_t cp;
    size_t units;
    if (u < 0x80u) {
      if (u == '\\' && escapes) {
        size_t after = 0;
        uint32_t named = 0;
        // 12.7: a UnicodeEscapeSequence whose code point is an
        // IdentifierStartChar (first) or IdentifierPartChar.
        bool valid = glecma_lex_scan_name_escape(text, offset, &named, &after) &&
            (first ? glecma_lex_is_id_start(named) : glecma_lex_is_id_part(named));
        if (!valid) {
          return glecma_lex_syntax_error(lexer, text,
              GLECMA_RULE_IDENTIFIER_ESCAPE, offset);
        }
        escaped = true;
        cp = named;
        units = after - offset;
        count += units - 1; // The loop's own increment counts one.
      } else if (glecma_lex_ascii_is(u, first ? GLECMA_LEXC_START
                                              : GLECMA_LEXC_PART)) {
        cp = u;
        units = 1;
      } else {
        break;
      }
    } else {
      size_t length = 0;
      cp = glecma_lex_cp(text, offset, &length);
      if (cp == GLECMA_LEX_NONE || cp == GLECMA_LEX_BAD ||
          !(first ? glecma_lex_is_id_start(cp) : glecma_lex_is_id_part(cp))) {
        break;
      }
      units = length;
    }
    if (cp >= 0x80u) {
      ascii = false;
    } else if (word_length < sizeof(word)) {
      word[word_length++] = (char)cp;
    }
    offset += units;
    count++;
    first = false;
    if (count > max) {
      return glecma_lex_literal_limit(lexer, text);
    }
  }
  if (first) {
    return GLECMA_ERR_INTERNAL; // The caller checked the first unit.
  }
  if (escaped) {
    token->flags |= GLECMA_TOKF_ESCAPED;
  }
  if (token->kind == GLECMA_TOK_NAME && ascii && word_length >= 2 &&
      word_length <= KEYWORD_MAX_LENGTH) {
    token->keyword = keyword_lookup(word, word_length);
  }
  *end = offset;
  return GLECMA_OK;
}

/* ---- The next token ------------------------------------------------------ */

static void finish_punctuator(GLECMA_Token * token, GLECMA_TokenKind kind,
    size_t start, size_t length, size_t * end) {
  token->kind = kind;
  *end = start + length;
}

// Whether the unit at `offset` can begin an IdentifierName.
static bool name_starts_at(const GLECMA_LexText * text, size_t offset,
    bool escapes) {
  uint32_t u = glecma_lex_unit(text, offset);
  if (u < 0x80u) {
    return glecma_lex_ascii_is(u, GLECMA_LEXC_START) || (escapes && u == '\\');
  }
  size_t length = 0;
  uint32_t cp = glecma_lex_cp(text, offset, &length);
  return cp != GLECMA_LEX_NONE && cp != GLECMA_LEX_BAD &&
      glecma_lex_is_id_start(cp);
}

static GLECMA_Result scan_token(GLECMA_Lexer * lexer,
    const GLECMA_LexText * text, uint32_t goal, size_t start,
    GLECMA_Token * token, size_t * end) {
  uint32_t u = glecma_lex_unit(text, start);
  uint32_t n1 = glecma_lex_unit(text, start + 1);
  uint32_t n2 = glecma_lex_unit(text, start + 2);
  uint32_t n3 = glecma_lex_unit(text, start + 3);

  if (u >= 0x80u) {
    if (name_starts_at(text, start, false)) {
      token->kind = GLECMA_TOK_NAME;
      return scan_name(lexer, text, start, start, true, token, end);
    }
    return glecma_lex_syntax_error(lexer, text,
        GLECMA_RULE_UNEXPECTED_CHARACTER, start);
  }
  if (glecma_lex_ascii_is(u, GLECMA_LEXC_START) || u == '\\') {
    token->kind = GLECMA_TOK_NAME;
    return scan_name(lexer, text, start, start, true, token, end);
  }
  if (glecma_lex_ascii_is(u, GLECMA_LEXC_DIGIT)) {
    return glecma_lex_scan_number(lexer, text, start, token, end);
  }
  switch (u) {
    case '"': case '\'':
      return glecma_lex_scan_string(lexer, text, start, token, end);
    case '`':
      return glecma_lex_scan_template(lexer, text, start, false, token, end);
    case '}':
      if ((goal & GLECMA_GOAL_TEMPLATE_TAIL) != 0) {
        return glecma_lex_scan_template(lexer, text, start, true, token, end);
      }
      finish_punctuator(token, GLECMA_TOK_RBRACE, start, 1, end);
      return GLECMA_OK;
    case '#':
      if (!name_starts_at(text, start + 1, true)) {
        return glecma_lex_syntax_error(lexer, text, GLECMA_RULE_PRIVATE_NAME,
            start);
      }
      token->kind = GLECMA_TOK_PRIVATE_NAME;
      return scan_name(lexer, text, start, start + 1, true, token, end);
    case '/':
      if ((goal & GLECMA_GOAL_REGEXP) != 0) {
        return glecma_lex_scan_regexp(lexer, text, start, token, end);
      }
      if (n1 == '=') {
        finish_punctuator(token, GLECMA_TOK_SLASH_ASSIGN, start, 2, end);
      } else {
        finish_punctuator(token, GLECMA_TOK_SLASH, start, 1, end);
      }
      return GLECMA_OK;
    case '%':
      if ((lexer->mode & GLECMA_LEXF_BUILTIN) != 0 &&
          (goal & GLECMA_GOAL_REGEXP) != 0 &&
          name_starts_at(text, start + 1, false)) {
        // The internal built-in mode: `%IdentifierName` where a primary
        // expression is expected, which is where a `/` would begin a
        // regular expression. Nowhere else, and never with the bit clear.
        token->kind = GLECMA_TOK_INTRINSIC_NAME;
        return scan_name(lexer, text, start, start + 1, false, token, end);
      }
      if (n1 == '=') {
        finish_punctuator(token, GLECMA_TOK_PERCENT_ASSIGN, start, 2, end);
      } else {
        finish_punctuator(token, GLECMA_TOK_PERCENT, start, 1, end);
      }
      return GLECMA_OK;
    case '.':
      if (glecma_lex_ascii_is(n1, GLECMA_LEXC_DIGIT)) {
        return glecma_lex_scan_number(lexer, text, start, token, end);
      }
      if (n1 == '.' && n2 == '.') {
        finish_punctuator(token, GLECMA_TOK_ELLIPSIS, start, 3, end);
      } else {
        finish_punctuator(token, GLECMA_TOK_DOT, start, 1, end);
      }
      return GLECMA_OK;
    case '{': finish_punctuator(token, GLECMA_TOK_LBRACE, start, 1, end); return GLECMA_OK;
    case '(': finish_punctuator(token, GLECMA_TOK_LPAREN, start, 1, end); return GLECMA_OK;
    case ')': finish_punctuator(token, GLECMA_TOK_RPAREN, start, 1, end); return GLECMA_OK;
    case '[': finish_punctuator(token, GLECMA_TOK_LBRACKET, start, 1, end); return GLECMA_OK;
    case ']': finish_punctuator(token, GLECMA_TOK_RBRACKET, start, 1, end); return GLECMA_OK;
    case ';': finish_punctuator(token, GLECMA_TOK_SEMICOLON, start, 1, end); return GLECMA_OK;
    case ',': finish_punctuator(token, GLECMA_TOK_COMMA, start, 1, end); return GLECMA_OK;
    case '~': finish_punctuator(token, GLECMA_TOK_TILDE, start, 1, end); return GLECMA_OK;
    case ':': finish_punctuator(token, GLECMA_TOK_COLON, start, 1, end); return GLECMA_OK;
    case '<':
      if (n1 == '<' && n2 == '=') {
        finish_punctuator(token, GLECMA_TOK_SHL_ASSIGN, start, 3, end);
      } else if (n1 == '<') {
        finish_punctuator(token, GLECMA_TOK_SHL, start, 2, end);
      } else if (n1 == '=') {
        finish_punctuator(token, GLECMA_TOK_LE, start, 2, end);
      } else {
        finish_punctuator(token, GLECMA_TOK_LT, start, 1, end);
      }
      return GLECMA_OK;
    case '>':
      if (n1 == '>' && n2 == '>' && n3 == '=') {
        finish_punctuator(token, GLECMA_TOK_USHR_ASSIGN, start, 4, end);
      } else if (n1 == '>' && n2 == '>') {
        finish_punctuator(token, GLECMA_TOK_USHR, start, 3, end);
      } else if (n1 == '>' && n2 == '=') {
        finish_punctuator(token, GLECMA_TOK_SHR_ASSIGN, start, 3, end);
      } else if (n1 == '>') {
        finish_punctuator(token, GLECMA_TOK_SHR, start, 2, end);
      } else if (n1 == '=') {
        finish_punctuator(token, GLECMA_TOK_GE, start, 2, end);
      } else {
        finish_punctuator(token, GLECMA_TOK_GT, start, 1, end);
      }
      return GLECMA_OK;
    case '=':
      if (n1 == '=' && n2 == '=') {
        finish_punctuator(token, GLECMA_TOK_SEQ, start, 3, end);
      } else if (n1 == '=') {
        finish_punctuator(token, GLECMA_TOK_EQ, start, 2, end);
      } else if (n1 == '>') {
        finish_punctuator(token, GLECMA_TOK_ARROW, start, 2, end);
      } else {
        finish_punctuator(token, GLECMA_TOK_ASSIGN, start, 1, end);
      }
      return GLECMA_OK;
    case '!':
      if (n1 == '=' && n2 == '=') {
        finish_punctuator(token, GLECMA_TOK_SNE, start, 3, end);
      } else if (n1 == '=') {
        finish_punctuator(token, GLECMA_TOK_NE, start, 2, end);
      } else {
        finish_punctuator(token, GLECMA_TOK_BANG, start, 1, end);
      }
      return GLECMA_OK;
    case '+':
      if (n1 == '+') {
        finish_punctuator(token, GLECMA_TOK_INC, start, 2, end);
      } else if (n1 == '=') {
        finish_punctuator(token, GLECMA_TOK_PLUS_ASSIGN, start, 2, end);
      } else {
        finish_punctuator(token, GLECMA_TOK_PLUS, start, 1, end);
      }
      return GLECMA_OK;
    case '-':
      if (n1 == '-') {
        finish_punctuator(token, GLECMA_TOK_DEC, start, 2, end);
      } else if (n1 == '=') {
        finish_punctuator(token, GLECMA_TOK_MINUS_ASSIGN, start, 2, end);
      } else {
        finish_punctuator(token, GLECMA_TOK_MINUS, start, 1, end);
      }
      return GLECMA_OK;
    case '*':
      if (n1 == '*' && n2 == '=') {
        finish_punctuator(token, GLECMA_TOK_STARSTAR_ASSIGN, start, 3, end);
      } else if (n1 == '*') {
        finish_punctuator(token, GLECMA_TOK_STARSTAR, start, 2, end);
      } else if (n1 == '=') {
        finish_punctuator(token, GLECMA_TOK_STAR_ASSIGN, start, 2, end);
      } else {
        finish_punctuator(token, GLECMA_TOK_STAR, start, 1, end);
      }
      return GLECMA_OK;
    case '&':
      if (n1 == '&' && n2 == '=') {
        finish_punctuator(token, GLECMA_TOK_ANDAND_ASSIGN, start, 3, end);
      } else if (n1 == '&') {
        finish_punctuator(token, GLECMA_TOK_ANDAND, start, 2, end);
      } else if (n1 == '=') {
        finish_punctuator(token, GLECMA_TOK_AMP_ASSIGN, start, 2, end);
      } else {
        finish_punctuator(token, GLECMA_TOK_AMP, start, 1, end);
      }
      return GLECMA_OK;
    case '|':
      if (n1 == '|' && n2 == '=') {
        finish_punctuator(token, GLECMA_TOK_OROR_ASSIGN, start, 3, end);
      } else if (n1 == '|') {
        finish_punctuator(token, GLECMA_TOK_OROR, start, 2, end);
      } else if (n1 == '=') {
        finish_punctuator(token, GLECMA_TOK_PIPE_ASSIGN, start, 2, end);
      } else {
        finish_punctuator(token, GLECMA_TOK_PIPE, start, 1, end);
      }
      return GLECMA_OK;
    case '^':
      if (n1 == '=') {
        finish_punctuator(token, GLECMA_TOK_CARET_ASSIGN, start, 2, end);
      } else {
        finish_punctuator(token, GLECMA_TOK_CARET, start, 1, end);
      }
      return GLECMA_OK;
    case '?':
      if (n1 == '?' && n2 == '=') {
        finish_punctuator(token, GLECMA_TOK_QQ_ASSIGN, start, 3, end);
      } else if (n1 == '?') {
        finish_punctuator(token, GLECMA_TOK_QQ, start, 2, end);
      } else if (n1 == '.' && !glecma_lex_ascii_is(n2, GLECMA_LEXC_DIGIT)) {
        // OptionalChainingPunctuator: `?.` not followed by a DecimalDigit,
        // so `a?.5:1` is a conditional.
        finish_punctuator(token, GLECMA_TOK_QDOT, start, 2, end);
      } else {
        finish_punctuator(token, GLECMA_TOK_QUESTION, start, 1, end);
      }
      return GLECMA_OK;
    default:
      return glecma_lex_syntax_error(lexer, text,
          GLECMA_RULE_UNEXPECTED_CHARACTER, start);
  }
}

GLECMA_Result glecma_lexer_next(GLECMA_Lexer * lexer, uint32_t goal,
    GLECMA_Token * token) {
  if (lexer == NULL || token == NULL) {
    return GLECMA_ERR_INVALID;
  }
  if (lexer->error.result != GLECMA_OK) {
    return lexer->error.result;
  }
  GLECMA_LexText text;
  glecma_lex_text_init(&text, lexer->source);
  if (lexer->bounded && text.units > lexer->limit) {
    text.units = lexer->limit;
  }

  bool newline = false;
  GLECMA_Result result = skip_trivia(lexer, &text, &newline);
  if (result != GLECMA_OK) {
    return result;
  }
  GLECMA_Token out;
  memset(&out, 0, sizeof(out));
  out.start = lexer->position;
  out.flags = newline ? GLECMA_TOKF_NEWLINE_BEFORE : 0u;
  size_t start = lexer->position.offset;
  if (start >= text.units) {
    out.kind = GLECMA_TOK_EOF;
    out.end = out.start;
    *token = out;
    return GLECMA_OK;
  }
  if (lexer->tokens >= lexer->max_tokens) {
    lexer->error.result = GLECMA_ERR_LIMIT;
    lexer->error.rule = 0;
    lexer->error.message = "source has more tokens than the tokens limit";
    lexer->error.location = lexer->position;
    lexer->error.limit = "tokens";
    return GLECMA_ERR_LIMIT;
  }
  size_t end = start;
  result = scan_token(lexer, &text, goal, start, &out, &end);
  if (result != GLECMA_OK) {
    return result;
  }
  glecma_lex_walk(&text, &lexer->position, end);
  out.end = lexer->position;
  lexer->tokens++;
  lexer->line_start = false;
  *token = out;
  return GLECMA_OK;
}

/* ---- The driver ---------------------------------------------------------- */

#define DRIVER_TEMPLATE_DEPTH 4096u

static bool expression_may_follow(const GLECMA_Token * token) {
  switch (token->kind) {
    case GLECMA_TOK_NAME:
      switch (token->keyword) {
        case GLECMA_KW_NONE: case GLECMA_KW_THIS: case GLECMA_KW_SUPER:
        case GLECMA_KW_NULL: case GLECMA_KW_TRUE: case GLECMA_KW_FALSE:
          return false;
        default:
          return (token->flags & GLECMA_TOKF_ESCAPED) == 0 &&
              (token->keyword <= GLECMA_KW_YIELD);
      }
    case GLECMA_TOK_PRIVATE_NAME: case GLECMA_TOK_INTRINSIC_NAME:
    case GLECMA_TOK_NUMBER: case GLECMA_TOK_BIGINT: case GLECMA_TOK_STRING:
    case GLECMA_TOK_REGEXP: case GLECMA_TOK_RPAREN: case GLECMA_TOK_RBRACKET:
    case GLECMA_TOK_INC: case GLECMA_TOK_DEC:
      return false;
    case GLECMA_TOK_TEMPLATE:
      return token->template_part == GLECMA_TEMPLATE_HEAD ||
          token->template_part == GLECMA_TEMPLATE_MIDDLE;
    default:
      return true;
  }
}

GLECMA_Result glecma_lexer_run(GLECMA_Lexer * lexer, GLECMA_LexVisitor visit,
    void * context) {
  uint32_t braces[DRIVER_TEMPLATE_DEPTH];
  size_t depth = 0; // Open `${` substitutions.
  bool regexp = true;
  for (;;) {
    uint32_t goal = regexp ? GLECMA_GOAL_REGEXP : GLECMA_GOAL_DIV;
    if (depth > 0 && braces[depth - 1] == 0) {
      goal |= GLECMA_GOAL_TEMPLATE_TAIL;
    }
    GLECMA_Token token;
    GLECMA_Result result = glecma_lexer_next(lexer, goal, &token);
    if (result != GLECMA_OK) {
      return result;
    }
    if (visit != NULL && !visit(context, &token)) {
      return GLECMA_OK;
    }
    if (token.kind == GLECMA_TOK_EOF) {
      return GLECMA_OK;
    }
    if (token.kind == GLECMA_TOK_TEMPLATE) {
      if (token.template_part == GLECMA_TEMPLATE_HEAD) {
        if (depth == DRIVER_TEMPLATE_DEPTH) {
          lexer->error.result = GLECMA_ERR_LIMIT;
          lexer->error.message = "templates nest deeper than the driver follows";
          lexer->error.limit = "nesting";
          lexer->error.location = token.start;
          return GLECMA_ERR_LIMIT;
        }
        braces[depth++] = 0;
      } else if (token.template_part == GLECMA_TEMPLATE_TAIL) {
        depth--;
      }
    } else if (token.kind == GLECMA_TOK_LBRACE && depth > 0) {
      braces[depth - 1]++;
    } else if (token.kind == GLECMA_TOK_RBRACE && depth > 0 &&
        braces[depth - 1] > 0) {
      braces[depth - 1]--;
    }
    regexp = expression_may_follow(&token);
  }
}

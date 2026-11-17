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
 * The literals of the lexical grammar: numeric literals, string literals,
 * template parts and regular expression literals; their escape sequences;
 * and the values a token stands for.
 *
 * One escape scanner serves the scanner, which validates, and the value
 * producer, which decodes, so that what is accepted and what is meant cannot
 * disagree.
 */

#include <ghoti.io/lang-ecmascript/macros.h>

#include "core/core_internal.h"
#include "lex/lex_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

const uint8_t glecma_lex_ascii_class[128] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 0, 0, 0, 0, 0, 0,
  0, 11, 11, 11, 11, 11, 11, 3, 3, 3, 3, 3, 3, 3, 3, 3,
  3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 0, 0, 0, 0, 3,
  0, 11, 11, 11, 11, 11, 11, 3, 3, 3, 3, 3, 3, 3, 3, 3,
  3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 0, 0, 0, 0, 0,
};

/* ---- Locations ----------------------------------------------------------- */

void glecma_lex_walk(const GLECMA_LexText * text, GLECMA_Location * position,
    size_t target) {
  size_t offset = position->offset;
  size_t line = position->line;
  size_t column = position->column;
  if (target > text->units) {
    target = text->units;
  }
  if (text->u8 != NULL) {
    const uint8_t * bytes = text->u8;
    while (offset < target) {
      uint8_t b = bytes[offset];
      if (b < 0x80u) {
        if (b == 0x0Au) {
          line++;
          column = 0;
        } else if (b == 0x0Du) {
          // A CR that an LF follows does not end the line; the LF does.
          if (offset + 1 < text->units && bytes[offset + 1] == 0x0Au) {
            column++;
          } else {
            line++;
            column = 0;
          }
        } else {
          column++;
        }
        offset++;
      } else if (b < 0xC0u) {
        offset++; // A continuation byte: only reachable inside a sequence.
      } else {
        size_t length = b < 0xE0u ? 2 : (b < 0xF0u ? 3 : 4);
        if (b == 0xE2u && offset + 2 < text->units &&
            bytes[offset + 1] == 0x80u &&
            (bytes[offset + 2] == 0xA8u || bytes[offset + 2] == 0xA9u)) {
          line++; // U+2028 or U+2029
          column = 0;
        } else {
          column += length == 4 ? 2 : 1;
        }
        offset += length;
      }
    }
  } else {
    const uint16_t * units = text->u16;
    while (offset < target) {
      uint16_t u = units[offset];
      if (u < 0x80u) {
        if (u == 0x0Au) {
          line++;
          column = 0;
        } else if (u == 0x0Du) {
          if (offset + 1 < text->units && units[offset + 1] == 0x0Au) {
            column++;
          } else {
            line++;
            column = 0;
          }
        } else {
          column++;
        }
        offset++;
      } else if (u == 0x2028u || u == 0x2029u) {
        line++;
        column = 0;
        offset++;
      } else if (u >= 0xD800u && u <= 0xDBFFu && offset + 1 < text->units &&
          units[offset + 1] >= 0xDC00u && units[offset + 1] <= 0xDFFFu) {
        column += 2;
        offset += 2;
      } else {
        column++;
        offset++;
      }
    }
  }
  position->offset = offset;
  position->line = line;
  position->column = column;
}

GLECMA_Location glecma_lex_locate(const GLECMA_Lexer * lexer,
    const GLECMA_LexText * text, size_t offset) {
  GLECMA_Location where = lexer->position;
  if (offset > where.offset) {
    glecma_lex_walk(text, &where, offset);
  }
  return where;
}

GLECMA_Result glecma_lex_syntax_error(GLECMA_Lexer * lexer,
    const GLECMA_LexText * text, uint32_t rule, size_t offset) {
  lexer->error.result = GLECMA_ERR_SYNTAX;
  lexer->error.rule = rule;
  lexer->error.message = glecma_rule_message(rule);
  lexer->error.location = glecma_lex_locate(lexer, text, offset);
  lexer->error.limit = NULL;
  return GLECMA_ERR_SYNTAX;
}

GLECMA_Result glecma_lex_literal_limit(GLECMA_Lexer * lexer,
    const GLECMA_LexText * text) {
  (void)text;
  lexer->error.result = GLECMA_ERR_LIMIT;
  lexer->error.rule = 0;
  lexer->error.message = "literal is longer than the literal_length limit";
  lexer->error.location = lexer->position;
  lexer->error.limit = "literal_length";
  return GLECMA_ERR_LIMIT;
}

size_t glecma_lex_count_cps(const GLECMA_LexText * text, size_t from,
    size_t to) {
  if (to > text->units) {
    to = text->units;
  }
  size_t count = 0;
  if (text->u8 != NULL) {
    for (size_t i = from; i < to; i++) {
      if ((text->u8[i] & 0xC0u) != 0x80u) {
        count++;
      }
    }
  } else {
    for (size_t i = from; i < to; i++) {
      count++;
      uint16_t u = text->u16[i];
      if (u >= 0xD800u && u <= 0xDBFFu && i + 1 < to &&
          text->u16[i + 1] >= 0xDC00u && text->u16[i + 1] <= 0xDFFFu) {
        i++;
      }
    }
  }
  return count;
}

/* ---- Escape sequences ---------------------------------------------------- */

typedef enum {
  ESCAPE_STRING_SLOPPY,
  ESCAPE_STRING_STRICT,
  ESCAPE_TEMPLATE
} EscapeMode;

typedef struct {
  size_t end;     // The offset after the escape.
  uint32_t rule;  // Non-zero when the escape is invalid in this mode.
  uint32_t flags; // GLECMA_TOKF_* the escape contributes.
  uint32_t value; // The code point it denotes, when has_value.
  bool has_value; // False for a line continuation and at the end of text.
} Escape;

// `\u` already read: offset is the unit after the `u`. Returns 0 for a
// well-formed escape, 1 for a malformed four-digit form and 2 for a
// malformed or out-of-range braced form.
static int scan_u_escape(const GLECMA_LexText * text, size_t offset,
    uint32_t * out_cp, size_t * out_end) {
  if (glecma_lex_unit(text, offset) == '{') {
    size_t i = offset + 1;
    uint32_t value = 0;
    size_t digits = 0;
    bool big = false;
    for (;;) {
      int digit = glecma_lex_hex_value(glecma_lex_unit(text, i));
      if (digit < 0) {
        break;
      }
      value = value * 16u + (uint32_t)digit;
      if (value > 0x10FFFFu) {
        big = true;
        value = 0x110000u; // Clamp: later digits cannot overflow it.
      }
      digits++;
      i++;
    }
    if (digits == 0 || glecma_lex_unit(text, i) != '}' || big) {
      return 2;
    }
    *out_cp = value;
    *out_end = i + 1;
    return 0;
  }
  uint32_t value = 0;
  for (size_t k = 0; k < 4; k++) {
    int digit = glecma_lex_hex_value(glecma_lex_unit(text, offset + k));
    if (digit < 0) {
      return 1;
    }
    value = value * 16u + (uint32_t)digit;
  }
  *out_cp = value;
  *out_end = offset + 4;
  return 0;
}

bool glecma_lex_scan_name_escape(const GLECMA_LexText * text, size_t offset,
    uint32_t * out_cp, size_t * out_end) {
  if (glecma_lex_unit(text, offset) != '\\' ||
      glecma_lex_unit(text, offset + 1) != 'u') {
    return false;
  }
  return scan_u_escape(text, offset + 2, out_cp, out_end) == 0;
}

// Reads the escape whose backslash is at offset-1; offset is the unit after
// it. A malformed or forbidden escape sets `rule` and ends one unit on, so a
// template can go on looking for its own end.
static void scan_escape(const GLECMA_LexText * text, size_t offset,
    EscapeMode mode, Escape * e) {
  e->rule = 0;
  e->flags = 0;
  e->value = 0;
  e->has_value = true;
  e->end = offset;
  uint32_t u = glecma_lex_unit(text, offset);
  switch (u) {
    case GLECMA_LEX_NONE:
      e->has_value = false; // The caller finds the end of text.
      return;
    case '\n':
      e->end = offset + 1;
      e->has_value = false;
      return;
    case '\r':
      e->end = offset + 1;
      if (glecma_lex_unit(text, offset + 1) == '\n') {
        e->end++;
      }
      e->has_value = false;
      return;
    case 'b': e->value = 0x08; e->end = offset + 1; return;
    case 't': e->value = 0x09; e->end = offset + 1; return;
    case 'n': e->value = 0x0A; e->end = offset + 1; return;
    case 'v': e->value = 0x0B; e->end = offset + 1; return;
    case 'f': e->value = 0x0C; e->end = offset + 1; return;
    case 'r': e->value = 0x0D; e->end = offset + 1; return;
    case '"': e->value = '"'; e->end = offset + 1; return;
    case '\'': e->value = '\''; e->end = offset + 1; return;
    case '\\': e->value = '\\'; e->end = offset + 1; return;
    case 'x': {
      int high = glecma_lex_hex_value(glecma_lex_unit(text, offset + 1));
      int low = glecma_lex_hex_value(glecma_lex_unit(text, offset + 2));
      if (high < 0 || low < 0) {
        e->rule = GLECMA_RULE_ESCAPE_HEX;
        e->end = offset + 1;
        return;
      }
      e->value = (uint32_t)(high * 16 + low);
      e->end = offset + 3;
      return;
    }
    case 'u': {
      uint32_t cp = 0;
      size_t end = 0;
      int bad = scan_u_escape(text, offset + 1, &cp, &end);
      if (bad != 0) {
        e->rule = bad == 1 ? GLECMA_RULE_ESCAPE_UNICODE
                           : GLECMA_RULE_ESCAPE_CODE_POINT;
        e->end = offset + 1;
        return;
      }
      e->value = cp;
      e->end = end;
      return;
    }
    case '0': case '1': case '2': case '3': case '4': case '5': case '6':
    case '7': {
      uint32_t next = glecma_lex_unit(text, offset + 1);
      bool next_digit = next >= '0' && next <= '9';
      if (u == '0' && !next_digit) {
        e->value = 0; // \0, not followed by a digit: NUL.
        e->end = offset + 1;
        return;
      }
      // A legacy octal escape: up to three digits, at most 255.
      uint32_t value = u - '0';
      size_t end = offset + 1;
      if (next >= '0' && next <= '7') {
        value = value * 8u + (next - '0');
        end++;
        uint32_t third = glecma_lex_unit(text, offset + 2);
        if (u <= '3' && third >= '0' && third <= '7') {
          value = value * 8u + (third - '0');
          end++;
        }
      }
      e->value = value;
      e->end = end;
      if (mode == ESCAPE_TEMPLATE) {
        e->rule = GLECMA_RULE_TEMPLATE_ESCAPE_OCTAL;
        e->end = offset + 1;
      } else {
        e->flags = GLECMA_TOKF_LEGACY_OCTAL;
        if (mode == ESCAPE_STRING_STRICT) {
          e->rule = GLECMA_RULE_ESCAPE_LEGACY_OCTAL_STRICT;
        }
      }
      return;
    }
    case '8': case '9':
      e->value = u; // The digit itself.
      e->end = offset + 1;
      if (mode == ESCAPE_TEMPLATE) {
        e->rule = GLECMA_RULE_TEMPLATE_ESCAPE_OCTAL;
      } else {
        e->flags = GLECMA_TOKF_NON_OCTAL_DECIMAL;
        if (mode == ESCAPE_STRING_STRICT) {
          e->rule = GLECMA_RULE_ESCAPE_NON_OCTAL_DECIMAL_STRICT;
        }
      }
      return;
    default: {
      size_t length = 0;
      uint32_t cp = glecma_lex_cp(text, offset, &length);
      if (cp == 0x2028u || cp == 0x2029u) {
        e->has_value = false; // A line continuation.
      } else {
        e->value = cp; // A NonEscapeCharacter stands for itself.
      }
      e->end = offset + (length == 0 ? 1 : length);
      return;
    }
  }
}

/* ---- Numeric literals ---------------------------------------------------- */

static bool radix_digit(uint32_t unit, unsigned radix) {
  switch (radix) {
    case 2: return unit == '0' || unit == '1';
    case 8: return unit >= '0' && unit <= '7';
    case 10: return unit >= '0' && unit <= '9';
    default: return glecma_lex_hex_value(unit) >= 0;
  }
}

// Digits with numeric separators. A separator must sit between two digits.
// Returns false, with the offset of the offending separator in *bad, when it
// does not.
static bool scan_digits(const GLECMA_LexText * text, size_t * offset,
    unsigned radix, size_t * count, size_t * bad) {
  size_t i = *offset;
  size_t digits = 0;
  bool after_digit = false;
  for (;;) {
    uint32_t u = glecma_lex_unit(text, i);
    if (radix_digit(u, radix)) {
      digits++;
      after_digit = true;
      i++;
    } else if (u == '_') {
      if (!after_digit || !radix_digit(glecma_lex_unit(text, i + 1), radix)) {
        *bad = i;
        return false;
      }
      after_digit = false;
      i++;
    } else {
      break;
    }
  }
  *offset = i;
  *count = digits;
  return true;
}

GLECMA_Result glecma_lex_scan_number(GLECMA_Lexer * lexer,
    const GLECMA_LexText * text, size_t start, GLECMA_Token * token,
    size_t * end) {
  size_t offset = start;
  size_t count = 0;
  size_t bad = 0;
  uint32_t flags = 0;
  GLECMA_TokenKind kind = GLECMA_TOK_NUMBER;
  bool integer = true; // A form a BigInt suffix may follow.
  bool strict = (lexer->mode & (GLECMA_LEXF_STRICT | GLECMA_LEXF_MODULE)) != 0;
  uint32_t u = glecma_lex_unit(text, offset);
  enum { TAIL_DOT, TAIL_EXPONENT, TAIL_SUFFIX } tail = TAIL_DOT;

  if (u == '.') {
    // The caller saw a digit after the dot.
    integer = false;
    offset++;
    if (!scan_digits(text, &offset, 10, &count, &bad)) {
      return glecma_lex_syntax_error(lexer, text, GLECMA_RULE_NUMERIC_SEPARATOR,
          bad);
    }
    tail = TAIL_EXPONENT;
  } else if (u == '0') {
    uint32_t next = glecma_lex_unit(text, offset + 1);
    unsigned radix = 0;
    if (next == 'x' || next == 'X') {
      radix = 16;
    } else if (next == 'o' || next == 'O') {
      radix = 8;
    } else if (next == 'b' || next == 'B') {
      radix = 2;
    }
    if (radix != 0) {
      offset += 2;
      if (!scan_digits(text, &offset, radix, &count, &bad)) {
        return glecma_lex_syntax_error(lexer, text,
            GLECMA_RULE_NUMERIC_SEPARATOR, bad);
      }
      if (count == 0) {
        return glecma_lex_syntax_error(lexer, text,
            GLECMA_RULE_NUMERIC_EXPECTED_DIGIT, offset);
      }
      tail = TAIL_SUFFIX;
    } else if ((next >= '0' && next <= '9') || next == '_') {
      // Clause 12.9.3: a legacy octal literal, or a decimal literal with a leading
      // zero. Neither has numeric separators.
      bool has_89 = false;
      offset++;
      for (;;) {
        uint32_t d = glecma_lex_unit(text, offset);
        if (d < '0' || d > '9') {
          break;
        }
        if (d >= '8') {
          has_89 = true;
        }
        offset++;
      }
      if (glecma_lex_unit(text, offset) == '_') {
        return glecma_lex_syntax_error(lexer, text,
            GLECMA_RULE_NUMERIC_SEPARATOR_LEGACY, offset);
      }
      if (!has_89) {
        flags |= GLECMA_TOKF_LEGACY_OCTAL;
        if (strict) {
          return glecma_lex_syntax_error(lexer, text,
              GLECMA_RULE_NUMERIC_LEGACY_OCTAL_STRICT, start);
        }
        if (glecma_lex_unit(text, offset) == 'n') {
          return glecma_lex_syntax_error(lexer, text,
              GLECMA_RULE_NUMERIC_BIGINT, offset);
        }
        goto finish; // Octal has no fraction or exponent.
      }
      flags |= GLECMA_TOKF_NON_OCTAL_DECIMAL;
      if (strict) {
        return glecma_lex_syntax_error(lexer, text,
            GLECMA_RULE_NUMERIC_NON_OCTAL_DECIMAL_STRICT, start);
      }
      integer = false;
      tail = TAIL_DOT;
    } else {
      offset++; // A plain 0.
      tail = TAIL_DOT;
    }
  } else {
    if (!scan_digits(text, &offset, 10, &count, &bad)) {
      return glecma_lex_syntax_error(lexer, text, GLECMA_RULE_NUMERIC_SEPARATOR,
          bad);
    }
    tail = TAIL_DOT;
  }

  if (tail == TAIL_DOT) {
    if (glecma_lex_unit(text, offset) == '.') {
      integer = false;
      offset++;
      if (!scan_digits(text, &offset, 10, &count, &bad)) {
        return glecma_lex_syntax_error(lexer, text,
            GLECMA_RULE_NUMERIC_SEPARATOR, bad);
      }
    }
    tail = TAIL_EXPONENT;
  }
  if (tail == TAIL_EXPONENT) {
    uint32_t e = glecma_lex_unit(text, offset);
    if (e == 'e' || e == 'E') {
      integer = false;
      offset++;
      uint32_t sign = glecma_lex_unit(text, offset);
      if (sign == '+' || sign == '-') {
        offset++;
      }
      if (!scan_digits(text, &offset, 10, &count, &bad)) {
        return glecma_lex_syntax_error(lexer, text,
            GLECMA_RULE_NUMERIC_SEPARATOR, bad);
      }
      if (count == 0) {
        return glecma_lex_syntax_error(lexer, text,
            GLECMA_RULE_NUMERIC_EXPECTED_DIGIT, offset);
      }
    }
  }
  // The BigInt suffix, on an integer form only.
  if (glecma_lex_unit(text, offset) == 'n') {
    if (!integer) {
      return glecma_lex_syntax_error(lexer, text, GLECMA_RULE_NUMERIC_BIGINT,
          offset);
    }
    kind = GLECMA_TOK_BIGINT;
    offset++;
  }

finish:;
  // Clause 12.9.3: the unit after a numeric literal must not be an IdentifierStart
  // or a DecimalDigit.
  uint32_t after = glecma_lex_unit(text, offset);
  bool bad_after = after == '\\' ||
      glecma_lex_ascii_is(after, GLECMA_LEXC_START | GLECMA_LEXC_DIGIT);
  if (!bad_after && after >= 0x80u) {
    size_t length = 0;
    uint32_t cp = glecma_lex_cp(text, offset, &length);
    bad_after = cp != GLECMA_LEX_NONE && cp != GLECMA_LEX_BAD &&
        glecma_lex_is_id_start(cp);
  }
  if (bad_after) {
    return glecma_lex_syntax_error(lexer, text, GLECMA_RULE_NUMERIC_TRAILING,
        offset);
  }
  token->kind = kind;
  token->flags |= flags;
  *end = offset;
  return GLECMA_OK;
}

/* ---- String literals ----------------------------------------------------- */

GLECMA_Result glecma_lex_scan_string(GLECMA_Lexer * lexer,
    const GLECMA_LexText * text, size_t start, GLECMA_Token * token,
    size_t * end) {
  uint32_t quote = glecma_lex_unit(text, start);
  EscapeMode mode =
      (lexer->mode & (GLECMA_LEXF_STRICT | GLECMA_LEXF_MODULE)) != 0
      ? ESCAPE_STRING_STRICT : ESCAPE_STRING_SLOPPY;
  size_t offset = start + 1;
  uint64_t count = 1;
  uint32_t flags = 0;
  const uint64_t max = lexer->max_literal;
  for (;;) {
    // The common case, a run of ordinary ASCII, without the work of the rest.
    for (;;) {
      uint32_t u = glecma_lex_unit(text, offset);
      if (u < 0x80u && u != quote && u != '\\' && u != '\n' && u != '\r' &&
          count <= max) {
        offset++;
        count++;
        continue;
      }
      break;
    }
    if (count > max) {
      return glecma_lex_literal_limit(lexer, text);
    }
    uint32_t u = glecma_lex_unit(text, offset);
    if (u == GLECMA_LEX_NONE || u == '\n' || u == '\r') {
      return glecma_lex_syntax_error(lexer, text,
          GLECMA_RULE_UNTERMINATED_STRING, start);
    }
    if (u == quote) {
      offset++;
      count++;
      break;
    }
    if (u == '\\') {
      Escape e;
      scan_escape(text, offset + 1, mode, &e);
      if (e.rule != 0) {
        return glecma_lex_syntax_error(lexer, text, e.rule, offset);
      }
      flags |= e.flags;
      count += glecma_lex_count_cps(text, offset, e.end);
      offset = e.end;
    } else {
      size_t length = 0;
      (void)glecma_lex_cp(text, offset, &length);
      offset += length == 0 ? 1 : length;
      count++;
    }
  }
  if (count > max) {
    return glecma_lex_literal_limit(lexer, text);
  }
  token->kind = GLECMA_TOK_STRING;
  token->flags |= flags;
  *end = offset;
  return GLECMA_OK;
}

/* ---- Template parts ------------------------------------------------------ */

GLECMA_Result glecma_lex_scan_template(GLECMA_Lexer * lexer,
    const GLECMA_LexText * text, size_t start, bool continuation,
    GLECMA_Token * token, size_t * end) {
  size_t offset = start + 1;
  uint64_t count = 1;
  const uint64_t max = lexer->max_literal;
  bool undefined = false;
  uint32_t first_rule = 0;
  size_t first_offset = 0;
  GLECMA_TemplatePart part;
  for (;;) {
    for (;;) {
      uint32_t u = glecma_lex_unit(text, offset);
      if (u < 0x80u && u != '`' && u != '$' && u != '\\' && count <= max) {
        offset++;
        count++;
        continue;
      }
      break;
    }
    if (count > max) {
      return glecma_lex_literal_limit(lexer, text);
    }
    uint32_t u = glecma_lex_unit(text, offset);
    if (u == GLECMA_LEX_NONE) {
      return glecma_lex_syntax_error(lexer, text,
          GLECMA_RULE_UNTERMINATED_TEMPLATE, start);
    }
    if (u == '`') {
      offset++;
      count++;
      part = continuation ? GLECMA_TEMPLATE_TAIL
                          : GLECMA_TEMPLATE_NO_SUBSTITUTION;
      break;
    }
    if (u == '$' && glecma_lex_unit(text, offset + 1) == '{') {
      offset += 2;
      count += 2;
      part = continuation ? GLECMA_TEMPLATE_MIDDLE : GLECMA_TEMPLATE_HEAD;
      break;
    }
    if (u == '\\') {
      Escape e;
      scan_escape(text, offset + 1, ESCAPE_TEMPLATE, &e);
      if (e.rule != 0 && !undefined) {
        undefined = true;
        first_rule = e.rule;
        first_offset = offset;
      }
      count += glecma_lex_count_cps(text, offset, e.end);
      offset = e.end;
    } else {
      size_t length = 0;
      (void)glecma_lex_cp(text, offset, &length);
      offset += length == 0 ? 1 : length;
      count++;
    }
  }
  if (count > max) {
    return glecma_lex_literal_limit(lexer, text);
  }
  token->kind = GLECMA_TOK_TEMPLATE;
  token->template_part = part;
  if (undefined) {
    token->flags |= GLECMA_TOKF_COOKED_UNDEFINED;
    token->escape_rule = first_rule;
    token->escape_location = glecma_lex_locate(lexer, text, first_offset);
  }
  *end = offset;
  return GLECMA_OK;
}

/* ---- Regular expression literals ----------------------------------------- */

GLECMA_Result glecma_lex_scan_regexp(GLECMA_Lexer * lexer,
    const GLECMA_LexText * text, size_t start, GLECMA_Token * token,
    size_t * end) {
  size_t offset = start + 1;
  uint64_t count = 1;
  const uint64_t max = lexer->max_literal;
  bool in_class = false;
  for (;;) {
    size_t length = 0;
    uint32_t cp = glecma_lex_cp(text, offset, &length);
    if (cp == GLECMA_LEX_NONE || glecma_lex_is_line_terminator(cp)) {
      return glecma_lex_syntax_error(lexer, text,
          GLECMA_RULE_UNTERMINATED_REGEXP, start);
    }
    if (cp == '\\') {
      // A backslash and the character after it, which may be neither the end
      // of the text nor a line terminator.
      size_t next_length = 0;
      uint32_t next = glecma_lex_cp(text, offset + 1, &next_length);
      if (next == GLECMA_LEX_NONE || glecma_lex_is_line_terminator(next)) {
        return glecma_lex_syntax_error(lexer, text,
            GLECMA_RULE_UNTERMINATED_REGEXP, start);
      }
      offset += 1 + next_length;
      count += 2;
    } else {
      if (cp == '[') {
        in_class = true;
      } else if (cp == ']') {
        in_class = false;
      } else if (cp == '/' && !in_class) {
        offset++;
        count++;
        break;
      }
      offset += length == 0 ? 1 : length;
      count++;
    }
    if (count > max) {
      return glecma_lex_literal_limit(lexer, text);
    }
  }
  // The flags: IdentifierPartChars, never an escape.
  token->regexp_flags_offset = offset;
  for (;;) {
    uint32_t u = glecma_lex_unit(text, offset);
    if (u == '\\') {
      return glecma_lex_syntax_error(lexer, text, GLECMA_RULE_REGEXP_FLAGS,
          offset);
    }
    if (u < 0x80u) {
      if (!glecma_lex_ascii_is(u, GLECMA_LEXC_PART)) {
        break;
      }
      offset++;
    } else {
      size_t length = 0;
      uint32_t cp = glecma_lex_cp(text, offset, &length);
      if (cp == GLECMA_LEX_NONE || cp == GLECMA_LEX_BAD ||
          !glecma_lex_is_id_part(cp)) {
        break;
      }
      offset += length;
    }
    count++;
    if (count > max) {
      return glecma_lex_literal_limit(lexer, text);
    }
  }
  token->kind = GLECMA_TOK_REGEXP;
  *end = offset;
  return GLECMA_OK;
}

/* ---- Token values -------------------------------------------------------- */

typedef struct {
  uint16_t * units;
  size_t length;
} Builder;

static void emit(Builder * b, uint32_t cp) {
  if (cp > 0xFFFFu) {
    cp -= 0x10000u;
    b->units[b->length++] = (uint16_t)(0xD800u + (cp >> 10));
    b->units[b->length++] = (uint16_t)(0xDC00u + (cp & 0x3FFu));
  } else {
    b->units[b->length++] = (uint16_t)cp;
  }
}

static void release_units(const GLECMA_Allocator * allocator,
    uint16_t * given, uint16_t * units) {
  if (given == NULL) {
    allocator->free_fn(allocator->ctx, units);
  }
}

// The value, written into @p given when it is not NULL (the caller guarantees
// room for one unit per unit of the token's text, which is the most a value
// can take) and into a block from @p allocator otherwise.
static GLECMA_Result token_value(const GLECMA_Source * source,
    const GLECMA_Token * token, GLECMA_ValueKind kind,
    const GLECMA_Allocator * allocator, uint16_t * given,
    uint16_t ** out_units, size_t * out_length) {
  if (source == NULL || token == NULL || out_units == NULL ||
      out_length == NULL) {
    return GLECMA_ERR_INVALID;
  }
  if (allocator == NULL) {
    allocator = glecma_allocator_default();
  }
  size_t begin = token->start.offset;
  size_t stop = token->end.offset;
  bool is_name = false;
  bool is_template = false;
  switch (token->kind) {
    case GLECMA_TOK_NAME:
      is_name = true;
      break;
    case GLECMA_TOK_PRIVATE_NAME:
    case GLECMA_TOK_INTRINSIC_NAME:
      is_name = true;
      begin++;
      break;
    case GLECMA_TOK_STRING:
      begin++;
      stop--;
      break;
    case GLECMA_TOK_TEMPLATE:
      is_template = true;
      begin++;
      stop -= (token->template_part == GLECMA_TEMPLATE_HEAD ||
               token->template_part == GLECMA_TEMPLATE_MIDDLE) ? 2 : 1;
      break;
    default:
      return GLECMA_ERR_INVALID;
  }
  if (is_template && kind == GLECMA_VALUE_COOKED &&
      (token->flags & GLECMA_TOKF_COOKED_UNDEFINED) != 0) {
    return GLECMA_ERR_INVALID;
  }
  if (!is_template && kind == GLECMA_VALUE_RAW) {
    return GLECMA_ERR_INVALID;
  }
  GLECMA_LexText text;
  glecma_lex_text_init(&text, source);
  if (stop > text.units || begin > stop) {
    return GLECMA_ERR_INVALID;
  }
  if (begin == stop) {
    *out_units = NULL;
    *out_length = 0;
    return GLECMA_OK;
  }
  // Every unit of the text yields at most one unit of value (a four-byte
  // UTF-8 sequence yields two), and an escape yields fewer than it takes.
  Builder b;
  b.length = 0;
  b.units = given != NULL ? given
      : (uint16_t *)allocator->malloc_fn(allocator->ctx,
            (stop - begin) * sizeof(uint16_t));
  if (b.units == NULL) {
    return GLECMA_ERR_OOM;
  }
  size_t offset = begin;
  while (offset < stop) {
    size_t length = 0;
    uint32_t cp = glecma_lex_cp(&text, offset, &length);
    if (cp == GLECMA_LEX_NONE || cp == GLECMA_LEX_BAD) {
      release_units(allocator, given, b.units);
      return GLECMA_ERR_INTERNAL;
    }
    if (cp == '\\') {
      if (is_name) {
        uint32_t named = 0;
        size_t after = 0;
        if (!glecma_lex_scan_name_escape(&text, offset, &named, &after)) {
          release_units(allocator, given, b.units);
          return GLECMA_ERR_INTERNAL;
        }
        emit(&b, named);
        offset = after;
        continue;
      }
      if (is_template && kind == GLECMA_VALUE_RAW) {
        // The raw value keeps the backslash and the character after it as
        // written, except that a CR or CRLF there is an LF.
        emit(&b, '\\');
        offset++;
        size_t next_length = 0;
        uint32_t next = glecma_lex_cp(&text, offset, &next_length);
        if (offset >= stop || next == GLECMA_LEX_NONE) {
          continue;
        }
        if (next == '\r') {
          emit(&b, '\n');
          offset++;
          if (glecma_lex_unit(&text, offset) == '\n') {
            offset++;
          }
        } else {
          emit(&b, next);
          offset += next_length;
        }
        continue;
      }
      Escape e;
      scan_escape(&text, offset + 1,
          is_template ? ESCAPE_TEMPLATE : ESCAPE_STRING_SLOPPY, &e);
      if (e.rule != 0) {
        release_units(allocator, given, b.units);
        return GLECMA_ERR_INTERNAL;
      }
      if (e.has_value) {
        emit(&b, e.value);
      }
      offset = e.end;
      continue;
    }
    if (is_template && cp == '\r') {
      emit(&b, '\n'); // TV and TRV read CR and CRLF as LF.
      offset++;
      if (glecma_lex_unit(&text, offset) == '\n') {
        offset++;
      }
      continue;
    }
    emit(&b, cp);
    offset += length;
  }
  if (b.length == 0) {
    release_units(allocator, given, b.units);
    *out_units = NULL;
    *out_length = 0;
    return GLECMA_OK;
  }
  *out_units = b.units;
  *out_length = b.length;
  return GLECMA_OK;
}

GLECMA_Result glecma_token_value(const GLECMA_Source * source,
    const GLECMA_Token * token, GLECMA_ValueKind kind,
    const GLECMA_Allocator * allocator, uint16_t ** out_units,
    size_t * out_length) {
  return token_value(source, token, kind, allocator, NULL, out_units,
      out_length);
}

GLECMA_Result glecma_token_value_into(const GLECMA_Source * source,
    const GLECMA_Token * token, GLECMA_ValueKind kind, uint16_t * buffer,
    size_t capacity, size_t * out_length) {
  if (buffer == NULL || token == NULL) {
    return GLECMA_ERR_INVALID;
  }
  size_t span = token->end.offset - token->start.offset;
  if (span > capacity) {
    return GLECMA_ERR_INVALID;
  }
  uint16_t * units = NULL;
  GLECMA_Result r = token_value(source, token, kind, NULL, buffer, &units,
      out_length);
  return r;
}

void glecma_token_value_free(const GLECMA_Allocator * allocator,
    uint16_t * units) {
  if (units == NULL) {
    return;
  }
  if (allocator == NULL) {
    allocator = glecma_allocator_default();
  }
  allocator->free_fn(allocator->ctx, units);
}

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
 * What the tokenizer's two translation units share: reading the source's
 * text by offset, the ASCII character classes, the error recorder and the
 * literal scanners. Never installed.
 *
 * The scanners work on offsets into the source's own units and never track
 * a line or a column character by character. A token's position is
 * brought up to date once, at its end, by ::glecma_lex_advance, which walks
 * the units it passed; an error inside a token asks ::glecma_lex_locate for
 * the location of an offset from the token's start. A differential test
 * checks that every token's start and end equal what the source's own
 * location query answers, so the two ways of counting cannot drift.
 */

#ifndef GHOTI_IO_GLECMA_SRC_LEX_LEX_INTERNAL_H
#define GHOTI_IO_GLECMA_SRC_LEX_LEX_INTERNAL_H

#include <ghoti.io/lang-ecmascript/macros.h>

#include "lex/lexer.h"
#include "source/source_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief What a read past the end of the text answers. */
#define GLECMA_LEX_NONE UINT32_C(0xFFFFFFFF)
/** @brief What an undecodable position answers; a validated source has none. */
#define GLECMA_LEX_BAD UINT32_C(0xFFFFFFFE)

/** @brief The source's text, unpacked for reading. */
typedef struct GLECMA_LexText {
  const uint8_t * u8;   ///< The units when the source is UTF-8, else NULL.
  const uint16_t * u16; ///< The units when the source is UTF-16, else NULL.
  size_t units;
} GLECMA_LexText;

static inline void glecma_lex_text_init(GLECMA_LexText * text,
    const GLECMA_Source * source) {
  bool wide = source->encoding == GLECMA_ENCODING_UTF16;
  text->u8 = wide ? NULL : (const uint8_t *)source->text;
  text->u16 = wide ? (const uint16_t *)source->text : NULL;
  text->units = source->units;
}

/**
 * @brief The unit at an offset: a byte for UTF-8, a code unit for UTF-16.
 *
 * An ASCII character is one unit in both encodings and never occurs inside
 * a longer sequence, so comparing units is how the scanners recognize every
 * ASCII character. A unit of 0x80 or more is part of a non-ASCII code
 * point; ::glecma_lex_cp reads the whole of it.
 *
 * @return The unit, or ::GLECMA_LEX_NONE at or past the end.
 */
static inline uint32_t glecma_lex_unit(const GLECMA_LexText * text,
    size_t offset) {
  if (offset >= text->units) {
    return GLECMA_LEX_NONE;
  }
  return text->u8 != NULL ? text->u8[offset] : text->u16[offset];
}

/**
 * @brief The code point at an offset and the units it takes.
 *
 * A surrogate pair in UTF-16 is one code point of two units, and an unpaired
 * surrogate is a code point of its own (ECMA-262 UTF16DecodeString).
 *
 * @return The code point, or ::GLECMA_LEX_NONE at or past the end.
 */
static inline uint32_t glecma_lex_cp(const GLECMA_LexText * text,
    size_t offset, size_t * length) {
  if (offset >= text->units) {
    *length = 0;
    return GLECMA_LEX_NONE;
  }
  if (text->u8 != NULL) {
    uint32_t first = text->u8[offset];
    if (first < 0x80u) {
      *length = 1;
      return first;
    }
    uint32_t cp = 0;
    size_t len = 0;
    if (!glecma_source_utf8_decode(text->u8 + offset, text->units - offset,
            &cp, &len)) {
      *length = 1;
      return GLECMA_LEX_BAD;
    }
    *length = len;
    return cp;
  }
  uint32_t first = text->u16[offset];
  if (first >= 0xD800u && first <= 0xDBFFu && offset + 1 < text->units) {
    uint32_t second = text->u16[offset + 1];
    if (second >= 0xDC00u && second <= 0xDFFFu) {
      *length = 2;
      return 0x10000u + ((first - 0xD800u) << 10) + (second - 0xDC00u);
    }
  }
  *length = 1;
  return first;
}

/** @name ASCII character classes */
///@{
#define GLECMA_LEXC_START 1u ///< IdentifierStartChar: letters, `$`, `_`.
#define GLECMA_LEXC_PART 2u  ///< IdentifierPartChar: also digits.
#define GLECMA_LEXC_DIGIT 4u ///< DecimalDigit.
#define GLECMA_LEXC_HEX 8u   ///< HexDigit.

extern const uint8_t glecma_lex_ascii_class[128];

static inline bool glecma_lex_ascii_is(uint32_t unit, unsigned mask) {
  return unit < 128u && (glecma_lex_ascii_class[unit] & mask) != 0;
}
///@}

/** @brief Whether a code point is a LineTerminator (12.3). */
static inline bool glecma_lex_is_line_terminator(uint32_t cp) {
  return cp == 0x0Au || cp == 0x0Du || cp == 0x2028u || cp == 0x2029u;
}

/** @brief The value of a hex digit unit, or -1. */
static inline int glecma_lex_hex_value(uint32_t unit) {
  if (unit >= '0' && unit <= '9') {
    return (int)(unit - '0');
  }
  if (unit >= 'a' && unit <= 'f') {
    return (int)(unit - 'a') + 10;
  }
  if (unit >= 'A' && unit <= 'F') {
    return (int)(unit - 'A') + 10;
  }
  return -1;
}

/**
 * @brief Walks @p position forward to @p offset, updating its line and
 *   column by the cursor's rules.
 *
 * @param position In and out; its offset must not exceed @p offset, and
 *   @p offset must be a code-point boundary or the end.
 */
GLECMA_INTERNAL_API void glecma_lex_walk(const GLECMA_LexText * text,
    GLECMA_Location * position, size_t offset);

/**
 * @brief The location of an offset at or after the token being scanned.
 */
GLECMA_INTERNAL_API GLECMA_Location glecma_lex_locate(
    const GLECMA_Lexer * lexer, const GLECMA_LexText * text, size_t offset);

/**
 * @brief Records a syntax error at an offset and returns ::GLECMA_ERR_SYNTAX.
 *
 * The offset must lie at or after the lexer's position, which is where the
 * construct being scanned starts.
 */
GLECMA_INTERNAL_API GLECMA_Result glecma_lex_syntax_error(GLECMA_Lexer * lexer,
    const GLECMA_LexText * text, uint32_t rule, size_t offset);

/**
 * @brief Records that a literal exceeded the `literal_length` cap.
 */
GLECMA_INTERNAL_API GLECMA_Result glecma_lex_literal_limit(
    GLECMA_Lexer * lexer, const GLECMA_LexText * text);

/** @brief Code points in the units [from, to) of the text. */
GLECMA_INTERNAL_API size_t glecma_lex_count_cps(const GLECMA_LexText * text,
    size_t from, size_t to);

/**
 * @brief Scans a numeric literal that begins at the lexer's position.
 *
 * @param start Its first unit (a digit, or a `.` followed by a digit).
 * @return ::GLECMA_OK with the token's kind, flags and end set (the caller
 *   brings the position up to the end), or an error already recorded.
 */
GLECMA_INTERNAL_API GLECMA_Result glecma_lex_scan_number(GLECMA_Lexer * lexer,
    const GLECMA_LexText * text, size_t start, GLECMA_Token * token,
    size_t * end);

/** @brief Scans a string literal whose opening quote is at @p start. */
GLECMA_INTERNAL_API GLECMA_Result glecma_lex_scan_string(GLECMA_Lexer * lexer,
    const GLECMA_LexText * text, size_t start, GLECMA_Token * token,
    size_t * end);

/**
 * @brief Scans a template part whose opening delimiter (a backtick, or the
 *   `}` of a continuation) is at @p start.
 */
GLECMA_INTERNAL_API GLECMA_Result glecma_lex_scan_template(
    GLECMA_Lexer * lexer, const GLECMA_LexText * text, size_t start,
    bool continuation, GLECMA_Token * token, size_t * end);

/** @brief Scans a regular expression literal whose `/` is at @p start. */
GLECMA_INTERNAL_API GLECMA_Result glecma_lex_scan_regexp(GLECMA_Lexer * lexer,
    const GLECMA_LexText * text, size_t start, GLECMA_Token * token,
    size_t * end);

/**
 * @brief Reads a `\u` escape of a name at @p offset (the backslash).
 *
 * @param out_cp Receives the code point.
 * @param out_end Receives the offset after the escape.
 * @return true when well-formed.
 */
GLECMA_INTERNAL_API bool glecma_lex_scan_name_escape(
    const GLECMA_LexText * text, size_t offset, uint32_t * out_cp,
    size_t * out_end);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GLECMA_SRC_LEX_LEX_INTERNAL_H

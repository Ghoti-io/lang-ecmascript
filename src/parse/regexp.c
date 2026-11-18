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
 * Regular expression literals (clause 13.2.7.1): the pattern and the flags of
 * a literal are an early error unless `regex`'s ECMAScript front end accepts
 * them, so the two libraries never disagree about what a literal means.
 *
 * Only the front end runs: the pattern is parsed and released, never
 * compiled, so a literal costs what reading it costs and nothing at run time
 * depends on this check. The pattern is handed over as the UTF-8 of the
 * source's code points (a lone surrogate, which a UTF-16 source can hold and
 * a regular expression may contain, as WTF-8), and `g` and `y`, which are not
 * pattern options, are checked here and left out of what `regex` reads.
 *
 * The check does not read a node (builder.h): the pattern comes from the
 * source, so a literal inside a function body that the checking builder did
 * not build is validated exactly as a built one is.
 */

#include <ghoti.io/lang-ecmascript/macros.h>

#include "parse/parser.h"

#include <ghoti.io/regex/regex.h>

#include <string.h>

/**
 * The stack the regex front end may take, kept in reserve beyond what the
 * parser keeps for itself when a literal is validated: its recursive descent
 * is capped at GLECMA_REGEXP_NESTING groups. design.md says how it was measured.
 */
#define REGEXP_STACK_RESERVE ((uint64_t)160 * 1024)

/** The deepest nesting of groups a literal's pattern may have. */
#define REGEXP_NESTING 128u

// The UTF-8 (WTF-8 for a lone surrogate) bytes of a code point.
static size_t encode_wtf8(uint32_t cp, uint8_t * out) {
  if (cp < 0x80u) {
    out[0] = (uint8_t)cp;
    return 1;
  }
  if (cp < 0x800u) {
    out[0] = (uint8_t)(0xC0u | (cp >> 6));
    out[1] = (uint8_t)(0x80u | (cp & 0x3Fu));
    return 2;
  }
  if (cp < 0x10000u) {
    out[0] = (uint8_t)(0xE0u | (cp >> 12));
    out[1] = (uint8_t)(0x80u | ((cp >> 6) & 0x3Fu));
    out[2] = (uint8_t)(0x80u | (cp & 0x3Fu));
    return 3;
  }
  out[0] = (uint8_t)(0xF0u | (cp >> 18));
  out[1] = (uint8_t)(0x80u | ((cp >> 12) & 0x3Fu));
  out[2] = (uint8_t)(0x80u | ((cp >> 6) & 0x3Fu));
  out[3] = (uint8_t)(0x80u | (cp & 0x3Fu));
  return 4;
}

// How a stretch of the source is handed to regex.
#define ENC_FLAGS 0u    // UTF-8, with `g` and `y` left out and counted
#define ENC_UNICODE 1u  // a pattern read as code points: WTF-8, as written
#define ENC_LEGACY 2u   // a pattern read as code units (no `u`, no `v`)

typedef struct Encoded {
  uint8_t * bytes;
  size_t length;
  unsigned seen_g, seen_y;
} Encoded;

static void put_unit_escape(uint8_t * out, size_t * length, uint32_t unit) {
  static const char digits[] = "0123456789ABCDEF";
  out[(*length)++] = '\\';
  out[(*length)++] = 'u';
  for (int shift = 12; shift >= 0; shift -= 4) {
    out[(*length)++] = (uint8_t)digits[(unit >> shift) & 0xFu];
  }
}

// The source between two offsets as the bytes regex reads, in `out` (NULL to
// only walk). A pattern without `u` or `v` is a sequence of code units in
// ECMA-262, where regex reads code points: a surrogate half, whether a lone
// one or half of an astral character written out, is handed over as its
// `\uXXXX` escape, which regex reads as one unit, so that a class range over
// the halves is ordered as the edition orders it and a lone surrogate (which
// WTF-8 cannot carry without `u`) is the unit it is. An escaped surrogate (a
// backslash before one, an identity escape) loses the backslash. When `stop`
// is not SIZE_MAX the walk ends at the first code point whose bytes reach that
// offset and `*at` is its source offset.
static void encode(Parser * p, size_t from, size_t to, unsigned mode, uint8_t * out,
    Encoded * result, size_t stop, size_t * at) {
  size_t length = 0;
  size_t offset = from;
  bool escaping = false; // the last byte written is a backslash that begins an escape
  while (offset < to) {
    if (stop != SIZE_MAX && length >= stop) {
      break;
    }
    size_t step = 0;
    uint32_t cp = glecma_lex_cp(&p->text, offset, &step);
    if (cp == GLECMA_LEX_NONE || cp == GLECMA_LEX_BAD) {
      parser_fail_result(p, GLECMA_ERR_INTERNAL);
    }
    if (mode == ENC_FLAGS && cp == 'g') {
      result->seen_g++;
    } else if (mode == ENC_FLAGS && cp == 'y') {
      result->seen_y++;
    } else if (mode == ENC_LEGACY && (cp >= 0x10000u || (cp >= 0xD800u && cp <= 0xDFFFu))) {
      uint8_t scratch[12];
      size_t width = 0;
      if (escaping) {
        escaping = false;
        if (out != NULL) {
          length--; // the backslash, which the escape written next replaces
        } else {
          length -= 1;
        }
      }
      if (cp >= 0x10000u) {
        put_unit_escape(scratch, &width, 0xD800u + ((cp - 0x10000u) >> 10));
        put_unit_escape(scratch, &width, 0xDC00u + ((cp - 0x10000u) & 0x3FFu));
      } else {
        put_unit_escape(scratch, &width, cp);
      }
      if (out != NULL) {
        memcpy(out + length, scratch, width);
      }
      length += width;
    } else {
      uint8_t scratch[4];
      size_t width = encode_wtf8(cp, scratch);
      if (out != NULL) {
        memcpy(out + length, scratch, width);
      }
      length += width;
      escaping = cp == '\\' ? !escaping : false;
    }
    offset += step;
  }
  if (out != NULL) {
    out[length] = 0;
  }
  result->length = length;
  if (at != NULL) {
    *at = offset;
  }
}

// The encoded source in a buffer the allocator owns.
static Encoded encode_owned(Parser * p, size_t from, size_t to, unsigned mode) {
  Encoded result = {NULL, 0, 0, 0};
  // A code unit takes at most six bytes (an escape).
  result.bytes = (uint8_t *)p->allocator->malloc_fn(p->allocator->ctx,
      (to - from) * 6 + 1);
  if (result.bytes == NULL) {
    parser_fail_result(p, GLECMA_ERR_OOM);
  }
  encode(p, from, to, mode, result.bytes, &result, SIZE_MAX, NULL);
  return result;
}

static GLECMA_NORETURN void fail_at_offset(Parser * p, uint32_t rule, size_t offset) {
  GLECMA_Location at = {0, 0, 0};
  (void)glecma_source_location(p->source, offset, &at);
  parser_fail(p, rule, &at);
}

void parse_validate_regexp(Parser * p, const GLECMA_Token * literal) {
  // The front end recurses over the pattern's groups: the stack for it must
  // be there before anything is allocated.
  parser_check_stack_for(p, REGEXP_STACK_RESERVE);
  const size_t pattern_from = literal->start.offset + 1;
  const size_t pattern_to = literal->regexp_flags_offset - 1;
  const size_t flags_from = literal->regexp_flags_offset;
  const size_t flags_to = literal->end.offset;

  // The flags: `g` and `y` here, the rest by regex, which knows the rest of
  // the alphabet, a repeat and the two letters that exclude each other.
  Encoded flags = encode_owned(p, flags_from, flags_to, ENC_FLAGS);
  uint32_t options = 0;
  GRX_Error error;
  grx_error_clear(&error);
  GRX_Result r = GRX_OK;
  if (flags.seen_g > 1 || flags.seen_y > 1) {
    r = GRX_ERR_SYNTAX;
  } else {
    r = grx_options_parse(GRX_SYNTAX_ECMASCRIPT, (const char *)flags.bytes, &options,
        &error);
  }
  p->allocator->free_fn(p->allocator->ctx, flags.bytes);
  if (r != GRX_OK) {
    if (r == GRX_ERR_SYNTAX) {
      fail_at_offset(p, GLECMA_RULE_REGEXP_FLAG_SET, flags_from);
    }
    parser_fail_result(p, r == GRX_ERR_OOM ? GLECMA_ERR_OOM : GLECMA_ERR_INTERNAL);
  }

  // The pattern, read as code points under `u` and `v` (a lone surrogate is
  // WTF-8, which regex reads under its option for it) and as code units
  // without them.
  const unsigned mode = (options & (GRX_OPT_UTF | GRX_OPT_UNICODE_SETS)) != 0
      ? ENC_UNICODE : ENC_LEGACY;
  Encoded pattern = encode_owned(p, pattern_from, pattern_to, mode);
  const size_t pattern_bytes = pattern.length;
  if ((options & GRX_OPT_UTF) != 0) {
    options |= GRX_OPT_WTF8;
  }
  GRX_Limits limits;
  grx_limits_unlimited(&limits);
  limits.max_nesting_depth = REGEXP_NESTING;
  limits.max_pattern_length = pattern_bytes > 0 ? pattern_bytes : 1;
  limits.max_nodes = p->max_nodes;
  GRX_Pattern * parsed = NULL;
  grx_error_clear(&error);
  r = grx_pattern_parse_with_allocator((const char *)pattern.bytes, pattern_bytes,
      GRX_SYNTAX_ECMASCRIPT, options, &limits, p->allocator, &error, &parsed);
  p->allocator->free_fn(p->allocator->ctx, pattern.bytes);
  if (r == GRX_OK) {
    grx_pattern_free(parsed);
    return;
  }
  switch (r) {
    case GRX_ERR_SYNTAX: {
      size_t at = pattern_from;
      if (error.offset != GRX_NPOS) {
        Encoded unused = {NULL, 0, 0, 0};
        encode(p, pattern_from, pattern_to, mode, NULL, &unused, error.offset, &at);
      }
      fail_at_offset(p, GLECMA_RULE_REGEXP_PATTERN, at);
    }
    case GRX_ERR_LIMIT:
      parser_fail_limit(p, error.diag == GRX_DIAG_LIMIT_NESTING_DEPTH ? "nesting"
          : error.diag == GRX_DIAG_LIMIT_NODES ? "nodes" : "literal_length");
    case GRX_ERR_OOM:
      parser_fail_result(p, GLECMA_ERR_OOM);
    case GRX_ERR_UNSUPPORTED:
      // A pattern regex does not yet read: a gap of its own, never a syntax
      // error of the program.
      parser_fail_result(p, GLECMA_ERR_UNSUPPORTED);
    default:
      parser_fail_result(p, GLECMA_ERR_INTERNAL);
  }
}

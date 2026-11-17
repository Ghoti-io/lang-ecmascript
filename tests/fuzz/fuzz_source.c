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
 * The source's fuzz harness: UTF-8 and UTF-16 input, and a differential
 * between the two.
 *
 * The first byte chooses: bit 0, the rest is UTF-16 code units (else UTF-8);
 * bit 1, a small source-size cap; bit 2, the source is made from the
 * library's input stream, read in short pieces, instead of from a buffer. A UTF-8 input that is well-formed is also
 * transcoded here, by this file's own decoder, and made into a UTF-16 source:
 * the two must agree on code points, lines, the location of every code point
 * and, when tokenized, on every token's kind, flags, line and column. An
 * ill-formed UTF-8 input must be ::GLECMA_ERR_CORRUPT with a location inside
 * the input.
 *
 * Built twice: by `make fuzz-source` under libFuzzer, and by `make
 * fuzz-replay` as a plain program over the seed corpus.
 */

#include <ghoti.io/lang-ecmascript/lang-ecmascript.h>

#include "fuzz_common.h"
#include "lex/lexer.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void check(int condition) {
  if (!condition) {
    abort();
  }
}

// A well-formed UTF-8 sequence decoder of this file's own, written from
// Table 3-7 and not from the library's.
static size_t utf8_sequence(const uint8_t * s, size_t left, uint32_t * cp) {
  uint8_t b = s[0];
  if (b < 0x80) { *cp = b; return 1; }
  size_t n;
  uint32_t min;
  if (b >= 0xC2 && b <= 0xDF) { n = 2; *cp = b & 0x1F; min = 0x80; }
  else if (b >= 0xE0 && b <= 0xEF) { n = 3; *cp = b & 0x0F; min = 0x800; }
  else if (b >= 0xF0 && b <= 0xF4) { n = 4; *cp = b & 0x07; min = 0x10000; }
  else { return 0; }
  if (left < n) { return 0; }
  for (size_t i = 1; i < n; i++) {
    if ((s[i] & 0xC0) != 0x80) { return 0; }
    *cp = (*cp << 6) | (s[i] & 0x3F);
  }
  if (*cp < min || *cp > 0x10FFFF || (*cp >= 0xD800 && *cp <= 0xDFFF)) {
    return 0;
  }
  return n;
}

typedef struct {
  GLECMA_TokenKind kind;
  uint32_t flags;
  size_t line, column;
} Seen;

typedef struct {
  Seen * items;
  size_t count, capacity;
} Seq;

static bool collect(void * context, const GLECMA_Token * token) {
  Seq * seq = (Seq *)context;
  if (seq->count == seq->capacity) {
    seq->capacity = seq->capacity ? seq->capacity * 2 : 64;
    seq->items = (Seen *)realloc(seq->items, seq->capacity * sizeof(Seen));
    check(seq->items != NULL);
  }
  seq->items[seq->count].kind = token->kind;
  seq->items[seq->count].flags = token->flags;
  seq->items[seq->count].line = token->start.line;
  seq->items[seq->count].column = token->start.column;
  seq->count++;
  return seq->count < 20000;
}

static GLECMA_Result tokenize(const GLECMA_Source * source, Seq * seq,
    GLECMA_Failure * failure) {
  GLECMA_Lexer lexer;
  check(glecma_lexer_open(&lexer, source, NULL, 0, NULL) == GLECMA_OK);
  GLECMA_Result r = glecma_lexer_run(&lexer, collect, seq);
  glecma_lexer_failure(&lexer, failure);
  return r;
}

int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
  if (size == 0) {
    return 0;
  }
  const uint8_t options = data[0];
  data++;
  size--;
  GLECMA_Limits limits = GLECMA_LIMITS_INIT;
  if (options & 2) {
    limits.source_units = 200;
  }
  GLECMA_Failure failure = GLECMA_FAILURE_INIT;
  GLECMA_Source * source = NULL;

  const int stream = (options & 4) != 0;
  if (options & 1) {
    size_t count = size / 2;
    GLECMA_Result r = fuzz_make_source(1, stream, data, size, &limits, &failure,
        &source);
    if (r == GLECMA_ERR_LIMIT) {
      check(count > limits.source_units && source == NULL);
      return 0;
    }
    check(r == GLECMA_OK);
    check(glecma_source_units(source) == count);
    // Every offset that is a boundary has a location, in order.
    GLECMA_Location previous = {0, 1, 0};
    for (size_t i = 0; i <= count && i < 4000; i++) {
      GLECMA_Location where;
      GLECMA_Result q = glecma_source_location(source, i, &where);
      check(q == GLECMA_OK || q == GLECMA_ERR_INVALID);
      if (q == GLECMA_OK) {
        check(where.offset == i && where.line >= previous.line);
        previous = where;
      }
    }
    Seq seq = {0, 0, 0};
    GLECMA_Failure tf = GLECMA_FAILURE_INIT;
    GLECMA_Result t = tokenize(source, &seq, &tf);
    check(t == GLECMA_OK || t == GLECMA_ERR_SYNTAX || t == GLECMA_ERR_LIMIT);
    free(seq.items);
    glecma_source_release(source);
    return 0;
  }

  GLECMA_Result r = fuzz_make_source(0, stream, data, size, &limits, &failure,
      &source);
  if (r == GLECMA_ERR_LIMIT) {
    check(size > limits.source_units && source == NULL);
    return 0;
  }
  // The harness's own decode of the input.
  size_t good = 0;
  int well_formed = 1;
  uint16_t * wide = (uint16_t *)malloc(size * sizeof(uint16_t) + 2);
  size_t * map = (size_t *)malloc((size + 1) * sizeof(size_t));
  check(wide != NULL && map != NULL);
  size_t wide_count = 0;
  size_t code_points = 0;
  while (good < size) {
    uint32_t cp;
    size_t n = utf8_sequence(data + good, size - good, &cp);
    if (n == 0) {
      well_formed = 0;
      break;
    }
    for (size_t k = 0; k < n; k++) {
      map[good + k] = wide_count;
    }
    if (cp > 0xFFFF) {
      wide[wide_count++] = (uint16_t)(0xD800 + ((cp - 0x10000) >> 10));
      wide[wide_count++] = (uint16_t)(0xDC00 + ((cp - 0x10000) & 0x3FF));
    } else {
      wide[wide_count++] = (uint16_t)cp;
    }
    code_points++;
    good += n;
  }
  map[good] = wide_count;
  if (!well_formed) {
    check(r == GLECMA_ERR_CORRUPT);
    check(source == NULL);
    check(failure.location.offset == good);
    check(failure.location.line >= 1);
  } else {
    check(r == GLECMA_OK);
    GLECMA_Source * other = NULL;
    check(glecma_source_create_utf16(wide, wide_count, &limits, NULL, NULL,
              &other) == GLECMA_OK);
    check(glecma_source_code_points(source) == code_points);
    check(glecma_source_code_points(other) == code_points);
    check(glecma_source_lines(source) == glecma_source_lines(other));
    for (size_t i = 0; i <= size && i < 4000; i++) {
      GLECMA_Location a, b;
      GLECMA_Result qa = glecma_source_location(source, i, &a);
      if (qa == GLECMA_OK) {
        check(glecma_source_location(other, map[i], &b) == GLECMA_OK);
        check(a.line == b.line && a.column == b.column);
      }
    }
    // The tokenizer reads both the same.
    Seq sa = {0, 0, 0}, sb = {0, 0, 0};
    GLECMA_Failure fa = GLECMA_FAILURE_INIT, fb = GLECMA_FAILURE_INIT;
    GLECMA_Result ta = tokenize(source, &sa, &fa);
    GLECMA_Result tb = tokenize(other, &sb, &fb);
    check(ta == tb && sa.count == sb.count);
    for (size_t i = 0; i < sa.count; i++) {
      check(sa.items[i].kind == sb.items[i].kind &&
          sa.items[i].flags == sb.items[i].flags &&
          sa.items[i].line == sb.items[i].line &&
          sa.items[i].column == sb.items[i].column);
    }
    check(fa.rule == fb.rule && fa.location.line == fb.location.line &&
        fa.location.column == fb.location.column);
    free(sa.items);
    free(sb.items);
    glecma_source_release(source);
    glecma_source_release(other);
  }
  free(wide);
  free(map);
  return 0;
}

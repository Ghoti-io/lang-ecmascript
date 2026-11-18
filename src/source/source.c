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
 * Source text: creation, validation, the checkpoint table, the cursor and
 * the location query.
 *
 * References: ECMA-262 17th edition, clause 11 (ECMAScript Language: Source
 * Text) and 12.3 (Line Terminators); The Unicode Standard, section 3.9
 * (Unicode Encoding Forms), Tables 3-7 and 3-8.
 */

#include <ghoti.io/lang-ecmascript/macros.h>

#include <ghoti.io/lang-ecmascript/source.h>

#include "core/core_internal.h"
#include "source/source_internal.h"

#include <ghoti.io/cutil/atomic.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// cutil's atomics live in a shared library TSan did not instrument, so it
// cannot see the happens-before edge that a release-to-zero gives the freeing
// thread, and reports every read of the text against the free as a race. The
// edge is real (the atomics are sequentially consistent); under TSan it is
// stated to the tool with its own annotations. Nothing changes otherwise.
#if defined(__SANITIZE_THREAD__)
void __tsan_acquire(void * addr);
void __tsan_release(void * addr);
#define GLECMA_TSAN_RELEASE(addr) __tsan_release(addr)
#define GLECMA_TSAN_ACQUIRE(addr) __tsan_acquire(addr)
#else
#define GLECMA_TSAN_RELEASE(addr) ((void)0)
#define GLECMA_TSAN_ACQUIRE(addr) ((void)0)
#endif

// ECMA-262's line terminators (12.3).
#define CP_LF 0x000Au
#define CP_CR 0x000Du
#define CP_LS 0x2028u
#define CP_PS 0x2029u

bool glecma_source_utf8_decode(const uint8_t * bytes, size_t length,
    uint32_t * out_code_point, size_t * out_length) {
  uint8_t lead = bytes[0];
  if (lead < 0x80u) {
    *out_code_point = lead;
    *out_length = 1;
    return true;
  }
  // Table 3-7: the lead byte fixes the sequence length and the range of the
  // second byte; every later byte is 80..BF. The narrowed second-byte
  // ranges are what exclude overlong forms (E0, F0), surrogates (ED) and
  // values above U+10FFFF (F4).
  size_t trail;
  uint8_t low = 0x80u;
  uint8_t high = 0xBFu;
  uint32_t code_point;
  if (lead >= 0xC2u && lead <= 0xDFu) {
    trail = 1;
    code_point = lead & 0x1Fu;
  } else if (lead >= 0xE0u && lead <= 0xEFu) {
    trail = 2;
    code_point = lead & 0x0Fu;
    if (lead == 0xE0u) {
      low = 0xA0u;
    } else if (lead == 0xEDu) {
      high = 0x9Fu;
    }
  } else if (lead >= 0xF0u && lead <= 0xF4u) {
    trail = 3;
    code_point = lead & 0x07u;
    if (lead == 0xF0u) {
      low = 0x90u;
    } else if (lead == 0xF4u) {
      high = 0x8Fu;
    }
  } else {
    // 80..BF, C0, C1 and F5..FF begin no well-formed sequence, so the
    // maximal subpart is the byte alone.
    *out_length = 1;
    return false;
  }
  for (size_t i = 1; i <= trail; i++) {
    if (i >= length || bytes[i] < low || bytes[i] > high) {
      // Bytes 0..i-1 are a prefix of a well-formed sequence and byte i is
      // not its continuation: those i bytes are the maximal subpart.
      *out_length = i;
      return false;
    }
    code_point = (code_point << 6) | (bytes[i] & 0x3Fu);
    low = 0x80u;
    high = 0xBFu;
  }
  *out_code_point = code_point;
  *out_length = trail + 1;
  return true;
}

// Why the ill-formed sequence at the start of bytes[0..length) is ill-formed,
// given its maximal subpart's length. Static text for the failure record.
static const char * utf8_problem(const uint8_t * bytes, size_t length,
    size_t subpart) {
  uint8_t lead = bytes[0];
  if (lead >= 0x80u && lead <= 0xBFu) {
    return "UTF-8 continuation byte with no lead byte";
  }
  if (lead == 0xC0u || lead == 0xC1u) {
    return "overlong UTF-8 encoding";
  }
  if (lead >= 0xF5u) {
    return "byte that never occurs in UTF-8";
  }
  if (subpart == length) {
    return "UTF-8 sequence truncated by the end of the input";
  }
  if (subpart == 1) {
    uint8_t second = bytes[1];
    if ((lead == 0xE0u && second >= 0x80u && second <= 0x9Fu) ||
        (lead == 0xF0u && second >= 0x80u && second <= 0x8Fu)) {
      return "overlong UTF-8 encoding";
    }
    if (lead == 0xEDu && second >= 0xA0u && second <= 0xBFu) {
      return "UTF-8 encoding of a surrogate code point";
    }
    if (lead == 0xF4u && second >= 0x90u && second <= 0xBFu) {
      return "UTF-8 encoding of a value above U+10FFFF";
    }
  }
  return "UTF-8 sequence missing a continuation byte";
}

static uint32_t unit_at(const GLECMA_Source * source, size_t offset) {
  if (source->encoding == GLECMA_ENCODING_UTF8) {
    return ((const uint8_t *)source->text)[offset];
  }
  return ((const uint16_t *)source->text)[offset];
}

// Decodes the code point at offset (< units). Only UTF-8 can be ill-formed;
// UTF-16 follows UTF16DecodeString, where an unpaired surrogate is itself.
static bool decode_at(const GLECMA_Source * source, size_t offset,
    uint32_t * out_code_point, size_t * out_length) {
  if (source->encoding == GLECMA_ENCODING_UTF8) {
    return glecma_source_utf8_decode((const uint8_t *)source->text + offset,
        source->units - offset, out_code_point, out_length);
  }
  const uint16_t * text = (const uint16_t *)source->text;
  uint32_t first = text[offset];
  if (first >= 0xD800u && first <= 0xDBFFu && offset + 1 < source->units) {
    uint32_t second = text[offset + 1];
    if (second >= 0xDC00u && second <= 0xDFFFu) {
      *out_code_point =
          0x10000u + ((first - 0xD800u) << 10) + (second - 0xDC00u);
      *out_length = 2;
      return true;
    }
  }
  *out_code_point = first;
  *out_length = 1;
  return true;
}

// Moves the cursor past code_point, which occupies length units at its
// offset. A CR ends its line unless an LF follows, in which case the LF ends
// it; so the LF of a CRLF sits on the CR's line, one column on.
static void cursor_advance(GLECMA_SourceCursor * cursor, uint32_t code_point,
    size_t length) {
  const GLECMA_Source * source = cursor->source;
  size_t next = cursor->offset + length;
  bool ends_line;
  if (code_point == CP_CR) {
    ends_line = !(next < source->units && unit_at(source, next) == CP_LF);
  } else {
    ends_line =
        code_point == CP_LF || code_point == CP_LS || code_point == CP_PS;
  }
  if (ends_line) {
    cursor->line++;
    cursor->column = 0;
  } else {
    cursor->column += code_point > 0xFFFFu ? 2u : 1u;
  }
  cursor->offset = next;
}

void glecma_source_cursor_start(GLECMA_SourceCursor * cursor,
    const GLECMA_Source * source) {
  cursor->source = source;
  cursor->offset = 0;
  cursor->line = 1;
  cursor->column = 0;
}

bool glecma_source_cursor_next(GLECMA_SourceCursor * cursor,
    uint32_t * out_code_point, GLECMA_Location * out_location) {
  if (cursor->offset >= cursor->source->units) {
    return false;
  }
  uint32_t code_point = 0;
  size_t length = 0;
  if (!decode_at(cursor->source, cursor->offset, &code_point, &length)) {
    // A source is validated when it is created, so this is unreachable; a
    // cursor that stops here is safer than one that walks on.
    return false;
  }
  if (out_location != NULL) {
    glecma_source_cursor_location(cursor, out_location);
  }
  if (out_code_point != NULL) {
    *out_code_point = code_point;
  }
  cursor_advance(cursor, code_point, length);
  return true;
}

void glecma_source_cursor_location(const GLECMA_SourceCursor * cursor,
    GLECMA_Location * out_location) {
  out_location->offset = cursor->offset;
  out_location->line = cursor->line;
  out_location->column = cursor->column;
}

GLECMA_Result glecma_source_cursor_seek(GLECMA_SourceCursor * cursor,
    const GLECMA_Source * source, size_t offset) {
  if (offset > source->units) {
    return GLECMA_ERR_INVALID;
  }
  // offset <= units, so the index is below units / interval + 1. Checkpoint
  // i is the first boundary at or after i * interval; when it is past the
  // offset, the offset is inside the code point straddling i * interval,
  // the walk below does not move, and the check after it refuses.
  const GLECMA_SourceCheckpoint * checkpoint =
      &source->checkpoints[offset / GLECMA_SOURCE_CHECKPOINT_INTERVAL];
  GLECMA_SourceCursor walk = {
    .source = source,
    .offset = checkpoint->offset,
    .line = checkpoint->line,
    .column = checkpoint->column,
  };
  const uint8_t * const bytes = (const uint8_t *)source->text;
  const uint16_t * const wide = (const uint16_t *)source->text;
  const bool utf8 = source->encoding == GLECMA_ENCODING_UTF8;
  while (walk.offset < offset) {
    // A unit below 0x80 that is not a line terminator is one code point of one
    // column: most of any source, and what a seek is walking over.
    const uint32_t unit = utf8 ? bytes[walk.offset] : wide[walk.offset];
    if (unit < 0x80u && unit != 0x0Au && unit != 0x0Du) {
      walk.offset++;
      walk.column++;
      continue;
    }
    if (!glecma_source_cursor_next(&walk, NULL, NULL)) {
      return GLECMA_ERR_INTERNAL;
    }
  }
  if (walk.offset != offset) {
    // The walk started past the offset or stepped over it: either way the
    // offset is inside a code point.
    return GLECMA_ERR_INVALID;
  }
  *cursor = walk;
  return GLECMA_OK;
}

static void write_failure(GLECMA_Failure * failure, GLECMA_Result result,
    const char * message, const GLECMA_Location * location,
    const char * limit) {
  glecma_failure_set(failure, result, message, location, limit, 0, NULL);
}

static void source_free(GLECMA_Source * source) {
  const GLECMA_Allocator allocator = source->allocator;
  if (source->text != NULL) {
    allocator.free_fn(allocator.ctx, (void *)source->text);
  }
  if (source->checkpoints != NULL) {
    allocator.free_fn(allocator.ctx, source->checkpoints);
  }
  allocator.free_fn(allocator.ctx, source);
}

// Walks the copy once: validates it, counts code points and lines, and
// records the checkpoints. On ill-formed input, writes the location of the
// first ill-formed sequence and the reason.
static GLECMA_Result source_scan(GLECMA_Source * source,
    GLECMA_Location * out_bad, const char ** out_problem) {
  GLECMA_SourceCursor cursor;
  glecma_source_cursor_start(&cursor, source);
  size_t next_checkpoint = 0;
  size_t code_points = 0;
  for (;;) {
    // A multi-unit code point can straddle a multiple of the interval;
    // the checkpoint then goes at the boundary just after it. The interval
    // is longer than any code point, so at most one is due per step.
    while (next_checkpoint < source->checkpoint_count &&
        next_checkpoint * GLECMA_SOURCE_CHECKPOINT_INTERVAL <= cursor.offset) {
      GLECMA_SourceCheckpoint * checkpoint =
          &source->checkpoints[next_checkpoint];
      checkpoint->offset = cursor.offset;
      checkpoint->line = cursor.line;
      checkpoint->column = cursor.column;
      next_checkpoint++;
    }
    if (cursor.offset >= source->units) {
      break;
    }
    uint32_t code_point = 0;
    size_t length = 0;
    if (!decode_at(source, cursor.offset, &code_point, &length)) {
      const uint8_t * bytes = (const uint8_t *)source->text + cursor.offset;
      *out_problem =
          utf8_problem(bytes, source->units - cursor.offset, length);
      glecma_source_cursor_location(&cursor, out_bad);
      return GLECMA_ERR_CORRUPT;
    }
    cursor_advance(&cursor, code_point, length);
    code_points++;
  }
  if (next_checkpoint != source->checkpoint_count) {
    return GLECMA_ERR_INTERNAL;
  }
  source->code_points = code_points;
  source->lines = cursor.line;
  return GLECMA_OK;
}

static GLECMA_Result source_create(GLECMA_Encoding encoding,
    const void * text, size_t length, size_t unit_size,
    const GLECMA_Limits * limits, const GLECMA_Allocator * allocator,
    GLECMA_Failure * failure, GLECMA_Source ** out_source) {
  // A failure record whose size does not cover its header cannot be written
  // to at all, so this is refused before anything else and writes nothing.
  if (failure != NULL && failure->size < GLECMA_FAILURE_HEADER_SIZE) {
    return GLECMA_ERR_INVALID;
  }
  if (out_source == NULL) {
    write_failure(failure, GLECMA_ERR_INVALID, "out_source is NULL", NULL,
        NULL);
    return GLECMA_ERR_INVALID;
  }
  if (text == NULL && length > 0) {
    write_failure(failure, GLECMA_ERR_INVALID,
        "text is NULL and length is not zero", NULL, NULL);
    return GLECMA_ERR_INVALID;
  }
  if (length > SIZE_MAX / unit_size) {
    write_failure(failure, GLECMA_ERR_INVALID,
        "length is longer than any buffer can be", NULL, NULL);
    return GLECMA_ERR_INVALID;
  }
  if (allocator == NULL) {
    allocator = glecma_allocator_default();
  }
  if (allocator->malloc_fn == NULL || allocator->calloc_fn == NULL ||
      allocator->realloc_fn == NULL || allocator->free_fn == NULL) {
    write_failure(failure, GLECMA_ERR_INVALID,
        "allocator has a NULL function", NULL, NULL);
    return GLECMA_ERR_INVALID;
  }
  GLECMA_Limits resolved;
  if (!glecma_limits_resolve(limits, &resolved)) {
    write_failure(failure, GLECMA_ERR_INVALID,
        "limits has a size below the header", NULL, NULL);
    return GLECMA_ERR_INVALID;
  }
  limits = &resolved;
  // Before any allocation: the cap is what keeps a hostile length from
  // costing anything.
  if ((uint64_t)length > limits->source_units) {
    write_failure(failure, GLECMA_ERR_LIMIT,
        "source is longer than the source_units limit", NULL,
        "source_units");
    return GLECMA_ERR_LIMIT;
  }

  GLECMA_Source * source =
      (GLECMA_Source *)allocator->calloc_fn(allocator->ctx, 1, sizeof(*source));
  if (source == NULL) {
    write_failure(failure, GLECMA_ERR_OOM, "allocation failed", NULL, NULL);
    return GLECMA_ERR_OOM;
  }
  source->allocator = *allocator;
  gcu_atomic_size_init(&source->refs, 1);
  source->encoding = encoding;
  source->units = length;
  source->checkpoint_count = length / GLECMA_SOURCE_CHECKPOINT_INTERVAL + 1;
  source->checkpoints = (GLECMA_SourceCheckpoint *)allocator->calloc_fn(
      allocator->ctx, source->checkpoint_count, sizeof(GLECMA_SourceCheckpoint));
  if (source->checkpoints == NULL) {
    source_free(source);
    write_failure(failure, GLECMA_ERR_OOM, "allocation failed", NULL, NULL);
    return GLECMA_ERR_OOM;
  }
  if (length > 0) {
    void * copy = allocator->malloc_fn(allocator->ctx, length * unit_size);
    if (copy == NULL) {
      source_free(source);
      write_failure(failure, GLECMA_ERR_OOM, "allocation failed", NULL, NULL);
      return GLECMA_ERR_OOM;
    }
    memcpy(copy, text, length * unit_size);
    source->text = copy;
  }

  GLECMA_Location bad = {0, 0, 0};
  const char * problem = NULL;
  GLECMA_Result result = source_scan(source, &bad, &problem);
  if (result != GLECMA_OK) {
    source_free(source);
    if (result == GLECMA_ERR_CORRUPT) {
      write_failure(failure, result, problem, &bad, NULL);
    } else {
      write_failure(failure, result, "source scan lost its place", NULL, NULL);
    }
    return result;
  }
  *out_source = source;
  write_failure(failure, GLECMA_OK, NULL, NULL, NULL);
  return GLECMA_OK;
}

GLECMA_Result glecma_source_create_utf8(const char * text, size_t length,
    const GLECMA_Limits * limits, const GLECMA_Allocator * allocator,
    GLECMA_Failure * failure, GLECMA_Source ** out_source) {
  return source_create(GLECMA_ENCODING_UTF8, text, length, 1, limits,
      allocator, failure, out_source);
}

GLECMA_Result glecma_source_create_utf16(const uint16_t * text,
    size_t length, const GLECMA_Limits * limits,
    const GLECMA_Allocator * allocator, GLECMA_Failure * failure,
    GLECMA_Source ** out_source) {
  return source_create(GLECMA_ENCODING_UTF16, text, length, sizeof(uint16_t),
      limits, allocator, failure, out_source);
}

GLECMA_Source * glecma_source_retain(GLECMA_Source * source) {
  if (source == NULL) {
    return NULL;
  }
  size_t before = gcu_atomic_size_fetch_add(&source->refs, 1);
  // A count of zero means the source was already released to nothing, and a
  // count this large means about 2^63 holders: either is a defect in the
  // caller, and carrying on would free the text under a reader.
  if (before == 0 || before > SIZE_MAX / 2) {
    abort();
  }
  return source;
}

void glecma_source_release(GLECMA_Source * source) {
  if (source == NULL) {
    return;
  }
  GLECMA_TSAN_RELEASE(&source->refs);
  size_t before = gcu_atomic_size_fetch_sub(&source->refs, 1);
  if (before == 0) {
    abort(); // released more often than it was held
  }
  if (before == 1) {
    GLECMA_TSAN_ACQUIRE(&source->refs);
    source_free(source);
  }
}

GLECMA_Encoding glecma_source_encoding(const GLECMA_Source * source) {
  return source != NULL ? source->encoding : (GLECMA_Encoding)0;
}

size_t glecma_source_units(const GLECMA_Source * source) {
  return source != NULL ? source->units : 0;
}

size_t glecma_source_code_points(const GLECMA_Source * source) {
  return source != NULL ? source->code_points : 0;
}

size_t glecma_source_lines(const GLECMA_Source * source) {
  return source != NULL ? source->lines : 0;
}

GLECMA_Result glecma_source_location(const GLECMA_Source * source,
    size_t offset, GLECMA_Location * out_location) {
  if (source == NULL || out_location == NULL) {
    return GLECMA_ERR_INVALID;
  }
  GLECMA_SourceCursor cursor;
  GLECMA_Result result = glecma_source_cursor_seek(&cursor, source, offset);
  if (result != GLECMA_OK) {
    return result;
  }
  glecma_source_cursor_location(&cursor, out_location);
  return GLECMA_OK;
}

GLECMA_Result glecma_source_dump(const GLECMA_Source * source, FILE * out) {
  if (source == NULL || out == NULL) {
    return GLECMA_ERR_INVALID;
  }
  const char * encoding =
      source->encoding == GLECMA_ENCODING_UTF8 ? "UTF-8" : "UTF-16";
  // Decided by this call's own writes, not by the stream's error flag,
  // which an earlier failure may already have set. The flush makes a write
  // that would fail only when buffered output reaches the file fail here.
  if (fprintf(out,
          "source %s: %zu units, %zu code points, %zu lines, %zu checkpoints\n",
          encoding, source->units, source->code_points, source->lines,
          source->checkpoint_count) < 0 ||
      fflush(out) != 0) {
    return GLECMA_ERR_IO;
  }
  return GLECMA_OK;
}

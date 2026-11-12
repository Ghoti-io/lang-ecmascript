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
 * The source's representation, and the cursor the tokenizer reads it with.
 * Never installed.
 *
 * A source is its text in the input encoding plus one checkpoint per
 * ::GLECMA_SOURCE_CHECKPOINT_INTERVAL input units. A checkpoint is a cursor
 * state - offset, line, column - at the first code-point boundary at or
 * after a multiple of the interval, so any offset's location is a walk of at
 * most one interval (plus the tail of one code point) from a checkpoint
 * found by division.
 *
 * The cursor never needs to look behind it. The one terminator whose
 * meaning depends on a neighbor is CR, and it is resolved by looking ahead
 * one unit: a CR followed by LF does not end the line (the LF does, and so
 * sits on the CR's line), and any other CR does. LF is one unit in both
 * encodings, so the look-ahead is one unit either way.
 */

#ifndef GHOTI_IO_GLECMA_SRC_SOURCE_SOURCE_INTERNAL_H
#define GHOTI_IO_GLECMA_SRC_SOURCE_SOURCE_INTERNAL_H

#include <ghoti.io/lang-ecmascript/macros.h>

#include <ghoti.io/lang-ecmascript/source.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Input units between two checkpoints. */
#define GLECMA_SOURCE_CHECKPOINT_INTERVAL 1024u

/** @brief A cursor state recorded at creation. */
typedef struct GLECMA_SourceCheckpoint {
  size_t offset; ///< A code-point boundary, in input units.
  size_t line;   ///< 1-based line of that boundary.
  size_t column; ///< 0-based column, in UTF-16 code units.
} GLECMA_SourceCheckpoint;

struct GLECMA_Source {
  GLECMA_Allocator allocator; ///< A copy of the creator's allocator.
  GLECMA_Encoding encoding;   ///< The input and storage encoding.
  const void * text;          ///< `uint8_t` or `uint16_t` units; NULL when
                              ///< `units` is 0.
  size_t units;               ///< Length of `text` in units.
  size_t code_points;         ///< Code points in `text`.
  size_t lines;               ///< Line terminators plus one.
  GLECMA_SourceCheckpoint * checkpoints; ///< `units / interval + 1` states;
                                         ///< checkpoint i is at the first
                                         ///< boundary at or after
                                         ///< `i * interval`.
  size_t checkpoint_count;    ///< Entries in `checkpoints`.
};

/**
 * @brief A read position in a source.
 *
 * A plain value: copy it to look ahead and keep the copy to back up. Its
 * location is always that of the code point it is about to read (or of the
 * end of input).
 */
typedef struct GLECMA_SourceCursor {
  const GLECMA_Source * source; ///< The source being read.
  size_t offset;                ///< Next code point's offset.
  size_t line;                  ///< Its 1-based line.
  size_t column;                ///< Its 0-based UTF-16 column.
} GLECMA_SourceCursor;

/**
 * @brief Decodes the UTF-8 sequence at the start of @p bytes.
 *
 * Well-formed means Table 3-7 of the Unicode Standard. An ill-formed start
 * reports the length of its maximal subpart (section 3.9): the longest
 * prefix that is the start of some well-formed sequence, or 1 if there is
 * none. Skipping that many bytes and decoding again is exactly the
 * segmentation Table 3-8 illustrates.
 *
 * @param bytes The bytes; at least one.
 * @param length Bytes available, at least 1.
 * @param out_code_point Receives the code point when well-formed.
 * @param out_length Receives the sequence length (1-4) when well-formed, or
 *   the maximal subpart's length (1-3) when not.
 * @return Whether the sequence is well-formed.
 */
GLECMA_INTERNAL_API bool glecma_source_utf8_decode(const uint8_t * bytes,
    size_t length, uint32_t * out_code_point, size_t * out_length);

/**
 * @brief Places @p cursor at the start of @p source.
 *
 * @param cursor The cursor to set.
 * @param source A valid source.
 */
GLECMA_INTERNAL_API void glecma_source_cursor_start(
    GLECMA_SourceCursor * cursor, const GLECMA_Source * source);

/**
 * @brief Places @p cursor at @p offset, by way of the nearest checkpoint.
 *
 * @param cursor The cursor to set; written only on success.
 * @param source A valid source.
 * @param offset A code-point boundary or the end of input.
 * @return ::GLECMA_OK, or ::GLECMA_ERR_INVALID for an offset past the end
 *   or inside a code point.
 */
GLECMA_INTERNAL_API GLECMA_Result glecma_source_cursor_seek(
    GLECMA_SourceCursor * cursor, const GLECMA_Source * source,
    size_t offset);

/**
 * @brief Reads the next code point and advances past it.
 *
 * @param cursor The cursor.
 * @param out_code_point Receives the code point (a lone surrogate from
 *   UTF-16 source is returned as itself). May be NULL.
 * @param out_location Receives the code point's location. May be NULL.
 * @return true when a code point was read. false with nothing written
 *   otherwise: at the end of input (`cursor->offset` equals the source's
 *   units), or, with `cursor->offset` still below the units, because the
 *   code point there did not decode. A validated source never decodes
 *   badly, so the second case is a broken invariant: the caller must report
 *   it as ::GLECMA_ERR_INTERNAL, never treat it as the end of the text.
 */
GLECMA_INTERNAL_API bool glecma_source_cursor_next(
    GLECMA_SourceCursor * cursor, uint32_t * out_code_point,
    GLECMA_Location * out_location);

/**
 * @brief The location of the cursor's position.
 *
 * @param cursor The cursor.
 * @param out_location Receives the location.
 */
GLECMA_INTERNAL_API void glecma_source_cursor_location(
    const GLECMA_SourceCursor * cursor, GLECMA_Location * out_location);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GLECMA_SRC_SOURCE_SOURCE_INTERNAL_H

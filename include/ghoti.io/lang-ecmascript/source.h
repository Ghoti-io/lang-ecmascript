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
 * @file source.h
 * @stability free
 *
 * Source text: an immutable, owned copy of a script's text, validated once,
 * with exact locations.
 *
 * Every parse starts here. A host hands the text as UTF-8 bytes or UTF-16
 * code units, and the library keeps a copy in that same encoding, so the
 * caller's buffer may be freed as soon as the call returns. Nothing is
 * stripped or replaced: a leading U+FEFF stays (ECMA-262 reads it as white
 * space), a leading `#!` stays (the grammar reads it as a hashbang comment
 * where the goal allows one), and ill-formed UTF-8 is refused rather than
 * turned into U+FFFD.
 *
 * ECMA-262 source text is a sequence of code points. UTF-8 input must be
 * well-formed by the Unicode Standard's Table 3-7 (no overlong form, no
 * encoded surrogate, nothing above U+10FFFF, no truncated sequence).
 * UTF-16 input is decoded as UTF16DecodeString does: a surrogate pair is one
 * code point, and a surrogate that is not part of a pair is a code point of
 * its own, because a string literal may hold one.
 *
 * A ::GLECMA_Location names a position by its offset in the input's own
 * units, and its line and column (see core.h). ::glecma_source_location
 * answers for any offset in time bounded by the distance to the nearest of
 * the source's checkpoints - one per 1024 input units - so a source costs
 * its copy plus a few percent, never an array of decoded code points or a
 * table of every line.
 *
 * A source is immutable once created and its holders are counted atomically
 * (::glecma_source_retain, ::glecma_source_release): any number of threads
 * may read, retain and release one at once, and the last release frees it.
 */

#ifndef GHOTI_IO_GLECMA_SOURCE_H
#define GHOTI_IO_GLECMA_SOURCE_H

#include <ghoti.io/lang-ecmascript/macros.h>

#include <ghoti.io/lang-ecmascript/allocator.h>
#include <ghoti.io/lang-ecmascript/core.h>
#include <ghoti.io/lang-ecmascript/limits.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief An owned, validated copy of source text. Opaque. */
typedef struct GLECMA_Source GLECMA_Source;

/**
 * @brief The encoding a source was created from, and is stored in.
 *
 * Zero names no encoding: it is what ::glecma_source_encoding answers for a
 * NULL source.
 */
typedef enum {
  GLECMA_ENCODING_UTF8 = 1,  ///< Bytes; an offset counts bytes.
  GLECMA_ENCODING_UTF16 = 2, ///< 16-bit code units in host byte order; an
                             ///< offset counts code units.
} GLECMA_Encoding;

/**
 * @brief Creates a source from UTF-8 text.
 *
 * The checks run in this order, and the first that fails decides the
 * result: the arguments (::GLECMA_ERR_INVALID); the ::GLECMA_Limits
 * `source_units` cap, against @p length, before anything is allocated
 * (::GLECMA_ERR_LIMIT); allocation (::GLECMA_ERR_OOM); then the encoding
 * (::GLECMA_ERR_CORRUPT). For ill-formed UTF-8 the failure's location is the
 * first byte of the first ill-formed subsequence - the start of the maximal
 * subpart the Unicode Standard (section 3.9) would replace - with the line
 * and column of that point.
 *
 * @param text The bytes. Copied; the caller keeps ownership. May be NULL
 *   only when @p length is 0. Need not be NUL-terminated, and a NUL byte is
 *   U+0000 like any other code point.
 * @param length Bytes in @p text.
 * @param limits Caps with `size` set (::GLECMA_LIMITS_INIT), or NULL for
 *   ::glecma_limits_default; a `size` below the header is
 *   ::GLECMA_ERR_INVALID.
 * @param allocator Allocator for everything the source holds, or NULL for
 *   the default. It is copied into the source; its context must outlive the
 *   source.
 * @param failure Optional; written on every return. Its `size` must be set
 *   (::GLECMA_FAILURE_INIT); a smaller one than the header is
 *   ::GLECMA_ERR_INVALID and nothing is written.
 * @param out_source Receives the source. Written only on success; on
 *   failure nothing is left allocated.
 * A NULL from the allocator is ::GLECMA_ERR_OOM, including when the
 * allocator is a runtime-core context's counting allocator refusing for the
 * context's memory budget: this call takes no context, so it cannot tell the
 * two apart. A host that passed such an allocator tells them apart through
 * the context.
 *
 * @return ::GLECMA_OK; ::GLECMA_ERR_INVALID for a NULL @p out_source, a NULL
 *   @p text with a non-zero @p length, or an allocator with a NULL function;
 *   ::GLECMA_ERR_LIMIT with `failure->limit` "source_units";
 *   ::GLECMA_ERR_OOM when the allocator returns NULL;
 *   ::GLECMA_ERR_CORRUPT with `failure->location`; ::GLECMA_ERR_INTERNAL if
 *   the library's own invariant fails while scanning the copy.
 */
GLECMA_API GLECMA_Result glecma_source_create_utf8(const char * text,
    size_t length, const GLECMA_Limits * limits,
    const GLECMA_Allocator * allocator, GLECMA_Failure * failure,
    GLECMA_Source ** out_source);

/**
 * @brief Creates a source from UTF-16 code units in host byte order.
 *
 * As ::glecma_source_create_utf8, except that every sequence of code units
 * is accepted: a surrogate that is not half of a pair is kept as a code
 * point of its own, so this never returns ::GLECMA_ERR_CORRUPT.
 *
 * @param text The code units. Copied. May be NULL only when @p length is 0.
 * @param length Code units in @p text. A length whose byte size does not
 *   fit in a `size_t` is ::GLECMA_ERR_INVALID: no buffer that long exists.
 * @param limits Caps, or NULL for the defaults.
 * @param allocator Allocator, or NULL for the default; copied into the
 *   source.
 * @param failure Optional; written on every return. Its `size` must be set
 *   (::GLECMA_FAILURE_INIT); a smaller one than the header is
 *   ::GLECMA_ERR_INVALID and nothing is written.
 * @param out_source Receives the source; written only on success.
 * @return ::GLECMA_OK, ::GLECMA_ERR_INVALID, ::GLECMA_ERR_LIMIT,
 *   ::GLECMA_ERR_OOM (a NULL from the allocator, a context's budget refusal
 *   included, as for ::glecma_source_create_utf8) or ::GLECMA_ERR_INTERNAL.
 */
GLECMA_API GLECMA_Result glecma_source_create_utf16(const uint16_t * text,
    size_t length, const GLECMA_Limits * limits,
    const GLECMA_Allocator * allocator, GLECMA_Failure * failure,
    GLECMA_Source ** out_source);

/**
 * @brief Takes another hold on a source.
 *
 * A source is immutable after it is created and its holders are counted
 * atomically: it may be read, retained and released from any thread without
 * a lock. Whatever is built from a source and borrows its text holds one
 * count of its own.
 *
 * @param source The source, or NULL (which does nothing).
 * @return @p source, so a hold can be taken in an initializer.
 */
GLECMA_API GLECMA_Source * glecma_source_retain(GLECMA_Source * source);

/**
 * @brief Gives up a hold; the last one frees the source and what it holds.
 *
 * The creator holds the first count, so a source made by
 * ::glecma_source_create_utf8 is released once for that and once for each
 * ::glecma_source_retain. Releasing more often than the source was held is
 * a defect in the caller and aborts the process.
 *
 * @param source The source, or NULL (which does nothing).
 */
GLECMA_API void glecma_source_release(GLECMA_Source * source);

/**
 * @brief The encoding the source was created from.
 *
 * @param source The source.
 * @return Its encoding; `(GLECMA_Encoding)0` for a NULL source.
 */
GLECMA_API GLECMA_Encoding glecma_source_encoding(const GLECMA_Source * source);

/**
 * @brief The length of the source in input units.
 *
 * @param source The source.
 * @return Bytes for UTF-8, code units for UTF-16; 0 for a NULL source.
 */
GLECMA_API size_t glecma_source_units(const GLECMA_Source * source);

/**
 * @brief The number of code points in the source.
 *
 * A UTF-16 surrogate pair is one code point and a lone surrogate is one.
 *
 * @param source The source.
 * @return The count; 0 for a NULL source.
 */
GLECMA_API size_t glecma_source_code_points(const GLECMA_Source * source);

/**
 * @brief The number of lines in the source.
 *
 * One more than the number of line terminators, a CRLF counting once, so an
 * empty source has one line and a source ending in a terminator ends on an
 * empty last line.
 *
 * @param source The source.
 * @return The count, at least 1; 0 for a NULL source.
 */
GLECMA_API size_t glecma_source_lines(const GLECMA_Source * source);

/**
 * @brief The location of an offset.
 *
 * @p offset must be the start of a code point or the end of the input
 * (::glecma_source_units). An offset inside a multi-unit code point - a
 * UTF-8 continuation byte, or the low half of a UTF-16 surrogate pair - is
 * not a position and is refused. Takes time bounded by the 1024-unit
 * checkpoint interval, whatever the source's length.
 *
 * @param source The source.
 * @param offset An offset in input units.
 * @param out_location Receives the location; written only on success.
 * @return ::GLECMA_OK; ::GLECMA_ERR_INVALID for a NULL argument, an offset
 *   past the end, or an offset inside a code point.
 */
GLECMA_API GLECMA_Result glecma_source_location(const GLECMA_Source * source,
    size_t offset, GLECMA_Location * out_location);

/**
 * @brief Writes a human-readable summary of the source, for debugging.
 *
 * One line naming the encoding and the unit, code point, line and checkpoint
 * counts. The format is for people and may change; a program reads the
 * accessors instead. The stream is flushed before the call returns, so a
 * write that fails only when buffered output reaches its file is reported
 * here.
 *
 * @param source The source.
 * @param out The stream to write to.
 * @return ::GLECMA_OK; ::GLECMA_ERR_INVALID for a NULL argument;
 *   ::GLECMA_ERR_IO if writing or flushing fails. An error flag the stream
 *   already carried does not by itself make this call fail.
 */
GLECMA_API GLECMA_Result glecma_source_dump(const GLECMA_Source * source,
    FILE * out);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GLECMA_SOURCE_H

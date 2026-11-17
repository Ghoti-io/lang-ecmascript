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
 * @file stream.h
 * @stability free
 *
 * The library's input stream (CONVENTIONS section 5), and the constructor
 * that makes a source from one.
 *
 * A file, a block of memory, a host's own reader and a fuzz harness's bytes
 * all go down one path: a ::GLECMA_Stream hands out bytes, and
 * ::glecma_source_create_stream reads them to the end into a source. In this
 * version nothing is parsed before the stream has ended: parsing that starts
 * while the stream is still being read is planned, and changes nothing here.
 *
 * The stream is a byte stream. It has no `read_line`: the tokenizer reads
 * units, never lines, so the function would have no caller.
 */

#ifndef GHOTI_IO_GLECMA_STREAM_H
#define GHOTI_IO_GLECMA_STREAM_H

#include <ghoti.io/lang-ecmascript/macros.h>

#include <ghoti.io/lang-ecmascript/allocator.h>
#include <ghoti.io/lang-ecmascript/core.h>
#include <ghoti.io/lang-ecmascript/limits.h>
#include <ghoti.io/lang-ecmascript/source.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief An input byte stream. Opaque; destroy with ::glecma_stream_destroy. */
typedef struct GLECMA_Stream GLECMA_Stream;

/**
 * @brief A host's reader, for ::glecma_stream_create_callback.
 *
 * Writes up to @p capacity bytes into @p buffer and the count into
 * @p out_read. A count of zero with ::GLECMA_OK is the end of the stream; a
 * reader that has bytes must return at least one. Any other result is an
 * error, which the stream reports as ::GLECMA_ERR_IO.
 */
typedef GLECMA_Result (*GLECMA_StreamReadFn)(void * context, void * buffer,
    size_t capacity, size_t * out_read);

/** @brief Called once when a callback stream is destroyed; may be NULL. */
typedef void (*GLECMA_StreamCloseFn)(void * context);

/**
 * @brief A stream over a block of memory.
 *
 * The bytes are borrowed, not copied: they must stay valid and unchanged
 * until the stream is destroyed.
 *
 * @param data The bytes; may be NULL when @p length is zero.
 * @param length Their count.
 * @param allocator Allocator, or NULL for the default.
 * @param out_stream Receives the stream.
 * @return ::GLECMA_OK; ::GLECMA_ERR_INVALID for a NULL output or a NULL
 *   @p data with a non-zero length; ::GLECMA_ERR_OOM.
 */
GLECMA_API GLECMA_Result glecma_stream_create_memory(const void * data,
    size_t length, const GLECMA_Allocator * allocator,
    GLECMA_Stream ** out_stream);

/**
 * @brief A stream over a file, opened for reading in binary mode.
 *
 * @param path The file's path.
 * @param allocator Allocator, or NULL for the default.
 * @param out_stream Receives the stream.
 * @return ::GLECMA_OK; ::GLECMA_ERR_INVALID for a NULL argument;
 *   ::GLECMA_ERR_IO when the file cannot be opened; ::GLECMA_ERR_OOM.
 */
GLECMA_API GLECMA_Result glecma_stream_create_file(const char * path,
    const GLECMA_Allocator * allocator, GLECMA_Stream ** out_stream);

/**
 * @brief A stream over a host's reader.
 *
 * Such a stream cannot seek and does not know its size.
 *
 * @param read The reader; required.
 * @param close Called when the stream is destroyed, or NULL.
 * @param context Passed to both.
 * @param allocator Allocator, or NULL for the default.
 * @param out_stream Receives the stream.
 * @return ::GLECMA_OK; ::GLECMA_ERR_INVALID for a NULL @p read or output;
 *   ::GLECMA_ERR_OOM (in which case @p close is not called).
 */
GLECMA_API GLECMA_Result glecma_stream_create_callback(GLECMA_StreamReadFn read,
    GLECMA_StreamCloseFn close, void * context,
    const GLECMA_Allocator * allocator, GLECMA_Stream ** out_stream);

/**
 * @brief Reads up to @p capacity bytes.
 *
 * @param stream The stream.
 * @param buffer Receives the bytes.
 * @param capacity Room in @p buffer. Zero reads nothing and succeeds.
 * @param out_read Receives the count read. Zero with ::GLECMA_OK and a
 *   non-zero @p capacity means the end of the stream.
 * @return ::GLECMA_OK; ::GLECMA_ERR_INVALID for a NULL argument;
 *   ::GLECMA_ERR_IO when the stream failed, and then on every later call.
 */
GLECMA_API GLECMA_Result glecma_stream_read(GLECMA_Stream * stream,
    void * buffer, size_t capacity, size_t * out_read);

/** @brief The number of bytes read or skipped so far. */
GLECMA_API size_t glecma_stream_tell(const GLECMA_Stream * stream);

/** @brief Whether a read has reached the end of the stream. */
GLECMA_API bool glecma_stream_eof(const GLECMA_Stream * stream);

/**
 * @brief The stream's total size in bytes.
 *
 * @return ::GLECMA_OK; ::GLECMA_ERR_UNSUPPORTED for a callback stream, whose
 *   size is not known; ::GLECMA_ERR_IO.
 */
GLECMA_API GLECMA_Result glecma_stream_size(GLECMA_Stream * stream,
    size_t * out_size);

/**
 * @brief Moves to a byte offset.
 *
 * @param offset From the start; past the end is ::GLECMA_ERR_INVALID.
 * @return ::GLECMA_OK; ::GLECMA_ERR_UNSUPPORTED for a callback stream;
 *   ::GLECMA_ERR_INVALID; ::GLECMA_ERR_IO.
 */
GLECMA_API GLECMA_Result glecma_stream_seek(GLECMA_Stream * stream,
    size_t offset);

/** @brief Destroys a stream, closing what it owns. NULL does nothing. */
GLECMA_API void glecma_stream_destroy(GLECMA_Stream * stream);

/**
 * @brief Makes a source from a stream, read to the end.
 *
 * The result equals the source ::glecma_source_create_utf8 or
 * ::glecma_source_create_utf16 would make from the same bytes: the same
 * counts, the same locations and, for the same bad input, the same failure.
 * The stream is read from its current position and left at its end; the
 * caller still owns it. The size cap is checked as bytes arrive, so a stream
 * longer than ::GLECMA_Limits::source_units units is refused before the
 * library holds more than one unit past the cap.
 *
 * A UTF-16 stream supplies code units in host byte order; an odd number of
 * bytes is ::GLECMA_ERR_CORRUPT, located at the unit the stray byte begins.
 *
 * @param stream The stream.
 * @param encoding ::GLECMA_ENCODING_UTF8 or ::GLECMA_ENCODING_UTF16.
 * @param limits Limits, or NULL for the defaults.
 * @param allocator Allocator, or NULL for the default.
 * @param failure Optional; written on every return.
 * @param out_source Receives the source; release with
 *   ::glecma_source_release.
 * @return ::GLECMA_OK; ::GLECMA_ERR_INVALID; ::GLECMA_ERR_IO when the stream
 *   reports an error; ::GLECMA_ERR_LIMIT naming `source_units`;
 *   ::GLECMA_ERR_CORRUPT; ::GLECMA_ERR_OOM. On every failure nothing the
 *   call allocated survives.
 */
GLECMA_API GLECMA_Result glecma_source_create_stream(GLECMA_Stream * stream,
    GLECMA_Encoding encoding, const GLECMA_Limits * limits,
    const GLECMA_Allocator * allocator, GLECMA_Failure * failure,
    GLECMA_Source ** out_source);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GLECMA_STREAM_H

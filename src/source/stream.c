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
 * The input stream, and the source constructor that reads one to its end.
 */

#include <ghoti.io/lang-ecmascript/macros.h>

#include <ghoti.io/lang-ecmascript/stream.h>

#include "core/core_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef enum { STREAM_MEMORY = 1, STREAM_FILE, STREAM_CALLBACK } StreamKind;

struct GLECMA_Stream {
  GLECMA_Allocator allocator;
  StreamKind kind;
  const uint8_t * data; // memory
  size_t length;        // memory
  FILE * file;          // file
  size_t file_size;     // file
  GLECMA_StreamReadFn read_fn;   // callback
  GLECMA_StreamCloseFn close_fn; // callback
  void * context;                // callback
  size_t position;
  bool at_end;
  bool failed;
};

// The allocator the caller passed, or the default; NULL when one of its
// functions is missing.
static const GLECMA_Allocator * pick_allocator(
    const GLECMA_Allocator * allocator) {
  if (allocator == NULL) {
    return glecma_allocator_default();
  }
  if (allocator->malloc_fn == NULL || allocator->calloc_fn == NULL ||
      allocator->realloc_fn == NULL || allocator->free_fn == NULL) {
    return NULL;
  }
  return allocator;
}

static GLECMA_Result stream_new(StreamKind kind,
    const GLECMA_Allocator * allocator, GLECMA_Stream ** out_stream) {
  if (out_stream == NULL) {
    return GLECMA_ERR_INVALID;
  }
  const GLECMA_Allocator * use = pick_allocator(allocator);
  if (use == NULL) {
    return GLECMA_ERR_INVALID;
  }
  GLECMA_Stream * stream =
      (GLECMA_Stream *)use->calloc_fn(use->ctx, 1, sizeof(*stream));
  if (stream == NULL) {
    return GLECMA_ERR_OOM;
  }
  stream->allocator = *use;
  stream->kind = kind;
  *out_stream = stream;
  return GLECMA_OK;
}

GLECMA_Result glecma_stream_create_memory(const void * data, size_t length,
    const GLECMA_Allocator * allocator, GLECMA_Stream ** out_stream) {
  if (out_stream == NULL || (data == NULL && length > 0)) {
    return GLECMA_ERR_INVALID;
  }
  GLECMA_Stream * stream = NULL;
  GLECMA_Result result = stream_new(STREAM_MEMORY, allocator, &stream);
  if (result != GLECMA_OK) {
    return result;
  }
  stream->data = (const uint8_t *)data;
  stream->length = length;
  *out_stream = stream;
  return GLECMA_OK;
}

GLECMA_Result glecma_stream_create_file(const char * path,
    const GLECMA_Allocator * allocator, GLECMA_Stream ** out_stream) {
  if (path == NULL || out_stream == NULL) {
    return GLECMA_ERR_INVALID;
  }
  GLECMA_Stream * stream = NULL;
  GLECMA_Result result = stream_new(STREAM_FILE, allocator, &stream);
  if (result != GLECMA_OK) {
    return result;
  }
  stream->file = fopen(path, "rb");
  if (stream->file == NULL) {
    stream->allocator.free_fn(stream->allocator.ctx, stream);
    return GLECMA_ERR_IO;
  }
  // The size is found now, so that a later failure to ask is not possible.
  if (fseek(stream->file, 0, SEEK_END) != 0) {
    fclose(stream->file);
    stream->allocator.free_fn(stream->allocator.ctx, stream);
    return GLECMA_ERR_IO;
  }
  long end = ftell(stream->file);
  if (end < 0 || fseek(stream->file, 0, SEEK_SET) != 0) {
    fclose(stream->file);
    stream->allocator.free_fn(stream->allocator.ctx, stream);
    return GLECMA_ERR_IO;
  }
  stream->file_size = (size_t)end;
  *out_stream = stream;
  return GLECMA_OK;
}

GLECMA_Result glecma_stream_create_callback(GLECMA_StreamReadFn read,
    GLECMA_StreamCloseFn close, void * context,
    const GLECMA_Allocator * allocator, GLECMA_Stream ** out_stream) {
  if (read == NULL || out_stream == NULL) {
    return GLECMA_ERR_INVALID;
  }
  GLECMA_Stream * stream = NULL;
  GLECMA_Result result = stream_new(STREAM_CALLBACK, allocator, &stream);
  if (result != GLECMA_OK) {
    return result;
  }
  stream->read_fn = read;
  stream->close_fn = close;
  stream->context = context;
  *out_stream = stream;
  return GLECMA_OK;
}

GLECMA_Result glecma_stream_read(GLECMA_Stream * stream, void * buffer,
    size_t capacity, size_t * out_read) {
  if (stream == NULL || out_read == NULL || (buffer == NULL && capacity > 0)) {
    return GLECMA_ERR_INVALID;
  }
  *out_read = 0;
  if (stream->failed) {
    return GLECMA_ERR_IO;
  }
  if (capacity == 0) {
    return GLECMA_OK;
  }
  size_t got = 0;
  switch (stream->kind) {
    case STREAM_MEMORY: {
      size_t left = stream->length - stream->position;
      got = capacity < left ? capacity : left;
      if (got > 0) {
        memcpy(buffer, stream->data + stream->position, got);
      }
      break;
    }
    case STREAM_FILE: {
      got = fread(buffer, 1, capacity, stream->file);
      if (got < capacity && ferror(stream->file)) {
        stream->failed = true;
        return GLECMA_ERR_IO;
      }
      break;
    }
    case STREAM_CALLBACK: {
      GLECMA_Result result =
          stream->read_fn(stream->context, buffer, capacity, &got);
      if (result != GLECMA_OK || got > capacity) {
        stream->failed = true;
        return GLECMA_ERR_IO;
      }
      break;
    }
  }
  stream->position += got;
  if (got == 0) {
    stream->at_end = true;
  }
  *out_read = got;
  return GLECMA_OK;
}

size_t glecma_stream_tell(const GLECMA_Stream * stream) {
  return stream == NULL ? 0 : stream->position;
}

bool glecma_stream_eof(const GLECMA_Stream * stream) {
  return stream != NULL && stream->at_end;
}

GLECMA_Result glecma_stream_size(GLECMA_Stream * stream, size_t * out_size) {
  if (stream == NULL || out_size == NULL) {
    return GLECMA_ERR_INVALID;
  }
  switch (stream->kind) {
    case STREAM_MEMORY:
      *out_size = stream->length;
      return GLECMA_OK;
    case STREAM_FILE:
      *out_size = stream->file_size;
      return GLECMA_OK;
    case STREAM_CALLBACK:
      break;
  }
  return GLECMA_ERR_UNSUPPORTED;
}

GLECMA_Result glecma_stream_seek(GLECMA_Stream * stream, size_t offset) {
  if (stream == NULL) {
    return GLECMA_ERR_INVALID;
  }
  if (stream->failed) {
    return GLECMA_ERR_IO;
  }
  switch (stream->kind) {
    case STREAM_MEMORY:
      if (offset > stream->length) {
        return GLECMA_ERR_INVALID;
      }
      break;
    case STREAM_FILE:
      if (offset > stream->file_size) {
        return GLECMA_ERR_INVALID;
      }
      if (fseek(stream->file, (long)offset, SEEK_SET) != 0) {
        stream->failed = true;
        return GLECMA_ERR_IO;
      }
      break;
    case STREAM_CALLBACK:
      return GLECMA_ERR_UNSUPPORTED;
  }
  stream->position = offset;
  stream->at_end = false;
  return GLECMA_OK;
}

void glecma_stream_destroy(GLECMA_Stream * stream) {
  if (stream == NULL) {
    return;
  }
  if (stream->file != NULL) {
    fclose(stream->file);
  }
  if (stream->kind == STREAM_CALLBACK && stream->close_fn != NULL) {
    stream->close_fn(stream->context);
  }
  const GLECMA_Allocator allocator = stream->allocator;
  allocator.free_fn(allocator.ctx, stream);
}

/* ---- A source from a stream ---------------------------------------------- */

#define READ_CHUNK ((size_t)1 << 16)

static GLECMA_Result fail(GLECMA_Failure * failure, GLECMA_Result result,
    const char * message, const GLECMA_Location * location,
    const char * limit) {
  glecma_failure_set(failure, result, message, location, limit, 0, NULL);
  return result;
}

GLECMA_Result glecma_source_create_stream(GLECMA_Stream * stream,
    GLECMA_Encoding encoding, const GLECMA_Limits * limits,
    const GLECMA_Allocator * allocator, GLECMA_Failure * failure,
    GLECMA_Source ** out_source) {
  if (failure != NULL && failure->size < GLECMA_FAILURE_HEADER_SIZE) {
    return GLECMA_ERR_INVALID;
  }
  if (stream == NULL || out_source == NULL) {
    return fail(failure, GLECMA_ERR_INVALID, "stream or out_source is NULL",
        NULL, NULL);
  }
  if (encoding != GLECMA_ENCODING_UTF8 && encoding != GLECMA_ENCODING_UTF16) {
    return fail(failure, GLECMA_ERR_INVALID, "unknown encoding", NULL, NULL);
  }
  const GLECMA_Allocator * use = pick_allocator(allocator);
  if (use == NULL) {
    return fail(failure, GLECMA_ERR_INVALID, "allocator has a NULL function",
        NULL, NULL);
  }
  GLECMA_Limits resolved;
  if (!glecma_limits_resolve(limits, &resolved)) {
    return fail(failure, GLECMA_ERR_INVALID,
        "limits has a size below the header", NULL, NULL);
  }
  const size_t unit_size = encoding == GLECMA_ENCODING_UTF8 ? 1u : 2u;
  const size_t max_bytes = resolved.source_units > SIZE_MAX / unit_size
      ? SIZE_MAX - 1u
      : (size_t)resolved.source_units * unit_size;

  uint8_t * buffer = NULL;
  size_t capacity = 0;
  size_t used = 0;
  GLECMA_Result result = GLECMA_OK;
  for (;;) {
    if (used == capacity) {
      // Grow, but never past one byte beyond the cap: that byte is how a
      // stream longer than the cap is told from one exactly as long.
      size_t room = max_bytes + 1u;
      if (capacity >= room) {
        result = fail(failure, GLECMA_ERR_LIMIT,
            "source is longer than the source_units limit", NULL,
            "source_units");
        break;
      }
      size_t grown = capacity == 0 ? READ_CHUNK : capacity;
      if (grown <= SIZE_MAX / 2 && capacity != 0) {
        grown = capacity * 2;
      }
      if (grown > room) {
        grown = room;
      }
      uint8_t * bigger = (uint8_t *)use->realloc_fn(use->ctx, buffer, grown);
      if (bigger == NULL) {
        result = fail(failure, GLECMA_ERR_OOM, "allocation failed", NULL,
            NULL);
        break;
      }
      buffer = bigger;
      capacity = grown;
    }
    size_t got = 0;
    GLECMA_Result read = glecma_stream_read(stream, buffer + used,
        capacity - used, &got);
    if (read != GLECMA_OK) {
      result = fail(failure, GLECMA_ERR_IO, "the input stream reported an error",
          NULL, NULL);
      break;
    }
    if (got == 0) {
      break;
    }
    used += got;
    if (used > max_bytes) {
      result = fail(failure, GLECMA_ERR_LIMIT,
          "source is longer than the source_units limit", NULL,
          "source_units");
      break;
    }
  }

  if (result == GLECMA_OK) {
    if (encoding == GLECMA_ENCODING_UTF8) {
      result = glecma_source_create_utf8((const char *)buffer, used, &resolved,
          use, failure, out_source);
    } else if (used % 2u != 0) {
      // The stray byte begins the unit after the last whole one; its place is
      // the end of the whole units.
      size_t units = used / 2u;
      GLECMA_Source * prefix = NULL;
      GLECMA_Result made = glecma_source_create_utf16(
          (const uint16_t *)(const void *)buffer, units, &resolved, use,
          NULL, &prefix);
      if (made != GLECMA_OK) {
        result = fail(failure, made, "allocation failed", NULL, NULL);
      } else {
        GLECMA_Location where = {units, 1, 0};
        glecma_source_location(prefix, units, &where);
        glecma_source_release(prefix);
        result = fail(failure, GLECMA_ERR_CORRUPT,
            "a UTF-16 stream has an odd number of bytes", &where, NULL);
      }
    } else {
      result = glecma_source_create_utf16(
          (const uint16_t *)(const void *)buffer, used / 2u, &resolved, use,
          failure, out_source);
    }
  }
  if (buffer != NULL) {
    use->free_fn(use->ctx, buffer);
  }
  return result;
}

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
 * What the fuzz harnesses share: making a source from the input either from
 * a buffer or from the library's input stream, as an option bit chooses, so
 * that both constructors are fuzzed and every harness reads a source made
 * either way.
 */

#ifndef GHOTI_IO_GLECMA_TESTS_FUZZ_FUZZ_COMMON_H
#define GHOTI_IO_GLECMA_TESTS_FUZZ_FUZZ_COMMON_H

#include <ghoti.io/lang-ecmascript/lang-ecmascript.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// A reader that returns its bytes in pieces whose size the input picks, so
// that a stream read in many short pieces is the common case.
typedef struct {
  const uint8_t * data;
  size_t size;
  size_t position;
  size_t piece;
} FuzzReader;

static GLECMA_Result fuzz_reader_read(void * context, void * buffer,
    size_t capacity, size_t * out_read) {
  FuzzReader * reader = (FuzzReader *)context;
  size_t left = reader->size - reader->position;
  size_t n = left < reader->piece ? left : reader->piece;
  if (n > capacity) {
    n = capacity;
  }
  memcpy(buffer, reader->data + reader->position, n);
  reader->position += n;
  *out_read = n;
  return GLECMA_OK;
}

/**
 * Makes a source from @p data: UTF-16 when @p wide (a trailing odd byte is
 * left off, as the buffer path does), UTF-8 otherwise; by the stream
 * constructor when @p stream. The result is what the chosen constructor
 * returned.
 */
static GLECMA_Result fuzz_make_source(int wide, int stream,
    const uint8_t * data, size_t size, const GLECMA_Limits * limits,
    GLECMA_Failure * failure, GLECMA_Source ** out_source) {
  if (stream) {
    size_t bytes = wide ? size - (size & 1u) : size;
    FuzzReader reader = {data, bytes, 0, 1 + (size % 13u)};
    GLECMA_Stream * s = NULL;
    GLECMA_Result made = glecma_stream_create_callback(&fuzz_reader_read, NULL,
        &reader, NULL, &s);
    if (made != GLECMA_OK) {
      abort();
    }
    GLECMA_Result r = glecma_source_create_stream(s,
        wide ? GLECMA_ENCODING_UTF16 : GLECMA_ENCODING_UTF8, limits, NULL,
        failure, out_source);
    glecma_stream_destroy(s);
    return r;
  }
  if (wide) {
    size_t count = size / 2;
    uint16_t * units = (uint16_t *)malloc(count * sizeof(uint16_t) + 1);
    if (units == NULL) {
      abort();
    }
    memcpy(units, data, count * sizeof(uint16_t));
    GLECMA_Result r = glecma_source_create_utf16(units, count, limits, NULL,
        failure, out_source);
    free(units);
    return r;
  }
  return glecma_source_create_utf8((const char *)data, size, limits, NULL,
      failure, out_source);
}

#endif // GHOTI_IO_GLECMA_TESTS_FUZZ_FUZZ_COMMON_H

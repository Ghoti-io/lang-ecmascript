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
 * @file source_locations.c
 *
 * Handing source text to the library and asking where things are.
 *
 * A host creates a source from the bytes it has - UTF-8 here - and from then
 * on names positions by offset. The library answers with the line and the
 * column a debugger shows: lines split at every ECMA-262 line terminator and
 * columns counted in UTF-16 code units, so the emoji below is two columns
 * wide although it is four bytes. Ill-formed UTF-8 is refused with the
 * position of the first bad byte, and a source over the size cap is refused
 * before anything is copied.
 */

#include <ghoti.io/lang-ecmascript/lang-ecmascript.h>

#include <stdio.h>
#include <string.h>

// Prints where an offset is, and fails unless it is where it should be.
static int show(const GLECMA_Source * source, const char * what,
    size_t offset, size_t line, size_t column) {
  GLECMA_Location location;
  if (glecma_source_location(source, offset, &location) != GLECMA_OK) {
    fprintf(stderr, "no location for offset %zu\n", offset);
    return 1;
  }
  printf("  %-26s offset %2zu  line %zu  column %zu\n", what,
      location.offset, location.line, location.column);
  if (location.offset != offset || location.line != line ||
      location.column != column) {
    fprintf(stderr, "%s: expected line %zu, column %zu\n", what, line,
        column);
    return 1;
  }
  return 0;
}

int main(void) {
  // A CRLF line, a line ending in U+2028, and a line with an emoji.
  static const char text[] =
      "let a = 1;\r\n"
      "let b = 2;\xE2\x80\xA8"
      "f(\"\xF0\x9F\x98\x80\", b);";
  GLECMA_Source * source = NULL;
  GLECMA_Failure failure;
  GLECMA_Result result = glecma_source_create_utf8(text, sizeof(text) - 1,
      NULL, NULL, &failure, &source);
  if (result != GLECMA_OK) {
    fprintf(stderr, "create: %s\n", glecma_result_string(result));
    return 1;
  }
  int status = 0;
  if (glecma_source_dump(source, stdout) != GLECMA_OK) {
    fprintf(stderr, "dump failed\n");
    status = 1;
  }
  // Line 1 is 12 bytes with its CRLF; line 2 ends at the U+2028 (bytes
  // 22-24); the emoji is four bytes but two UTF-16 columns.
  status |= show(source, "\"b\" on line 2", 16, 2, 4);
  status |= show(source, "the emoji", 28, 3, 3);
  status |= show(source, "the comma after it", 33, 3, 6);
  status |= show(source, "the end of the input", sizeof(text) - 1, 3, 11);
  glecma_source_destroy(source);

  // An overlong encoding of '/' (C0 AF) is refused, never decoded.
  static const char bad[] = "a = 1;\nb = \"\xC0\xAF\";";
  result = glecma_source_create_utf8(bad, sizeof(bad) - 1, NULL, NULL,
      &failure, &source);
  if (result != GLECMA_ERR_CORRUPT || failure.location.offset != 12 ||
      failure.location.line != 2 || failure.location.column != 5) {
    fprintf(stderr, "ill-formed UTF-8 was not refused at line 2, column 5\n");
    return 1;
  }
  printf("refused: %s (%s) at line %zu, column %zu\n",
      glecma_result_string(result),
      failure.message != NULL ? failure.message : "no detail",
      failure.location.line, failure.location.column);

  // A host that takes text from strangers caps its size.
  GLECMA_Limits limits;
  glecma_limits_default(&limits);
  limits.source_units = 8;
  result = glecma_source_create_utf8(text, sizeof(text) - 1, &limits, NULL,
      &failure, &source);
  if (result != GLECMA_ERR_LIMIT || strcmp(failure.limit, "source_units")) {
    fprintf(stderr, "the size cap was not applied\n");
    return 1;
  }
  printf("refused: %s (%s)\n", glecma_result_string(result), failure.limit);
  return status;
}

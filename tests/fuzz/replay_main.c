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
 * Replay driver for the fuzz harnesses: feeds each file named on the command
 * line once through LLVMFuzzerTestOneInput, with no libFuzzer and no clang.
 * `make fuzz-replay` runs it over every seed and every input a campaign once
 * found, so a regression a fuzzer caught is a failing test and not a campaign
 * to repeat. It is not a campaign.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size);

int main(int argc, char * argv[]) {
  if (argc < 2) {
    fprintf(stderr, "usage: replay FILE...\n");
    return 2;
  }
  size_t replayed = 0;
  for (int i = 1; i < argc; i++) {
    FILE * file = fopen(argv[i], "rb");
    if (file == NULL) {
      fprintf(stderr, "replay: cannot open %s\n", argv[i]);
      return 2;
    }
    size_t capacity = 4096;
    size_t length = 0;
    uint8_t * buffer = malloc(capacity);
    while (buffer != NULL) {
      if (length == capacity) {
        capacity *= 2;
        uint8_t * grown = realloc(buffer, capacity);
        if (grown == NULL) {
          free(buffer);
          buffer = NULL;
          break;
        }
        buffer = grown;
      }
      size_t got = fread(buffer + length, 1, capacity - length, file);
      if (got == 0) {
        break;
      }
      length += got;
    }
    fclose(file);
    if (buffer == NULL) {
      fprintf(stderr, "replay: out of memory reading %s\n", argv[i]);
      return 2;
    }
    LLVMFuzzerTestOneInput(buffer, length);
    free(buffer);
    replayed++;
  }
  printf("replay: %zu inputs, no crash\n", replayed);
  return 0;
}

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
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size);

int main(int argc, char * argv[]) {
  if (argc < 2) {
    fprintf(stderr, "usage: replay FILE...\n");
    return 2;
  }
  /* An argument `@list` names a file that lists the inputs, one path per line: a command line holds
   * only so many characters on some systems (32,767 on Windows), and a corpus has thousands of files. */
  size_t count = 0;
  char ** names = (char **)malloc(sizeof(char *) * (size_t)argc);
  char ** owned = NULL; /* the names read from a list, which are ours to free */
  size_t owned_count = 0;
  if (names == NULL) {
    return 2;
  }
  for (int i = 1; i < argc; i++) {
    if (argv[i][0] != '@') {
      names[count++] = argv[i];
      continue;
    }
    FILE * list = fopen(argv[i] + 1, "r");
    if (list == NULL) {
      fprintf(stderr, "replay: cannot open the list %s\n", argv[i] + 1);
      return 2;
    }
    char line[4096];
    while (fgets(line, sizeof line, list) != NULL) {
      size_t n = strlen(line);
      while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
        line[--n] = '\0';
      }
      if (n == 0) {
        continue;
      }
      char ** grown = (char **)realloc(names, sizeof(char *) * (count + (size_t)argc + 1));
      char * copy = (char *)malloc(n + 1);
      if (grown == NULL || copy == NULL) {
        return 2;
      }
      names = grown;
      char ** own = (char **)realloc(owned, sizeof(char *) * (owned_count + 1));
      if (own == NULL) {
        return 2;
      }
      owned = own;
      memcpy(copy, line, n + 1);
      owned[owned_count++] = copy;
      names[count++] = copy;
    }
    fclose(list);
  }
  size_t replayed = 0;
  for (size_t i = 0; i < count; i++) {
    FILE * file = fopen(names[i], "rb");
    if (file == NULL) {
      fprintf(stderr, "replay: cannot open %s\n", names[i]);
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
      fprintf(stderr, "replay: out of memory reading %s\n", names[i]);
      return 2;
    }
    LLVMFuzzerTestOneInput(buffer, length);
    free(buffer);
    replayed++;
  }
  for (size_t i = 0; i < owned_count; i++) {
    free(owned[i]);
  }
  free(owned);
  free(names);
  printf("replay: %zu inputs, no crash\n", replayed);
  return 0;
}

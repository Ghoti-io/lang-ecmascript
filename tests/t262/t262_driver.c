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
 * The test262 gate's driver (tools/check-test262.py runs it).
 *
 * Reads "mode<TAB>path" lines on standard input, one per (file, mode) pair,
 * and writes one result name per line on standard output: OK, or the name of
 * the ::GLECMA_Result the parse refused with (ERR_SYNTAX, ERR_UNSUPPORTED,
 * ERR_CORRUPT ...). The modes are `sloppy` (a Script), `strict` (a Script
 * with `"use strict";` and a newline put in front of the text, as test262's
 * INTERPRETING.md says), `module` (a Module) and `raw` (a Script exactly as
 * written).
 *
 * With `--differential` it answers a different question for each pair: it
 * parses the text with the tree builder and again with the checking builder
 * (which builds no function body), and answers `SAME <result>` with the rule
 * and place when it was a syntax error, or `DIFF <tree> | <checking>`. No
 * check reads a node, so the two must agree (tools/check-differential.py).
 *
 * With `--reparse` it parses each pair with the tree builder and, when the parse
 * is accepted, reads every function of the tree again from its range
 * (glecma_parse_function) in the context the tree says it had, and requires the
 * tree that gives to equal the first parse's: `REPARSED <functions> <by kind>`
 * or `FAIL <what differed>`, and `SKIP <result>` for a pair that was refused
 * (tests/reparse_support.h; tools/check-reparse.py).
 *
 * It is the only place the gate calls the library: `parse_pair` is a call of
 * the public parse entry. Work not built answers ERR_UNSUPPORTED, which is
 * never an acceptance and never a SyntaxError.
 */

#include <ghoti.io/lang-ecmascript/lang-ecmascript.h>

#include "parse/parse_internal.h"

#include "../reparse_support.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char * result_name(GLECMA_Result result) {
  switch (result) {
    case GLECMA_OK: return "OK";
    case GLECMA_ERR_IO: return "ERR_IO";
    case GLECMA_ERR_FORMAT: return "ERR_FORMAT";
    case GLECMA_ERR_UNSUPPORTED: return "ERR_UNSUPPORTED";
    case GLECMA_ERR_LIMIT: return "ERR_LIMIT";
    case GLECMA_ERR_CORRUPT: return "ERR_CORRUPT";
    case GLECMA_ERR_OOM: return "ERR_OOM";
    case GLECMA_ERR_INVALID: return "ERR_INVALID";
    case GLECMA_ERR_INTERNAL: return "ERR_INTERNAL";
    case GLECMA_ERR_SYNTAX: return "ERR_SYNTAX";
    default: return "ERR_UNKNOWN";
  }
}

typedef enum { MODE_SLOPPY, MODE_STRICT, MODE_MODULE, MODE_RAW } Mode;

static int parse_mode(const char * name, Mode * mode) {
  if (strcmp(name, "sloppy") == 0) { *mode = MODE_SLOPPY; return 1; }
  if (strcmp(name, "strict") == 0) { *mode = MODE_STRICT; return 1; }
  if (strcmp(name, "module") == 0) { *mode = MODE_MODULE; return 1; }
  if (strcmp(name, "raw") == 0) { *mode = MODE_RAW; return 1; }
  return 0;
}

// The parse of one pair: the library's public parse entry, for the Script or
// the Module goal.
static GLECMA_Result parse_pair(GLECMA_Source * source, Mode mode) {
  GLECMA_ParseOptions options = GLECMA_PARSE_OPTIONS_INIT;
  options.goal = mode == MODE_MODULE ? GLECMA_PARSE_MODULE : GLECMA_PARSE_SCRIPT;
  GLECMA_Tree * tree = NULL;
  GLECMA_Result result = glecma_parse(source, &options, NULL, NULL, &tree);
  glecma_tree_destroy(tree);
  return result;
}

// The result, and for a syntax error the rule and where, of one parse with
// the tree builder or the checking builder, as text.
static void describe(GLECMA_Source * source, Mode mode, uint32_t internal,
    char * out, size_t size) {
  GLECMA_ParseOptions options = GLECMA_PARSE_OPTIONS_INIT;
  options.goal = mode == MODE_MODULE ? GLECMA_PARSE_MODULE : GLECMA_PARSE_SCRIPT;
  GLECMA_Tree * tree = NULL;
  GLECMA_Failure failure = GLECMA_FAILURE_INIT;
  GLECMA_Result result = glecma_parse_internal(source, &options, NULL, internal,
      &failure, &tree);
  glecma_tree_destroy(tree);
  if (result == GLECMA_ERR_SYNTAX) {
    snprintf(out, size, "%s %s@%zu", result_name(result), glecma_rule_name(failure.rule),
        failure.location.offset);
  } else if (result == GLECMA_ERR_LIMIT) {
    snprintf(out, size, "%s %s", result_name(result),
        failure.limit != NULL ? failure.limit : "?");
  } else {
    snprintf(out, size, "%s", result_name(result));
  }
}

static int read_file(const char * path, char ** text, size_t * length) {
  FILE * file = fopen(path, "rb");
  if (file == NULL) {
    return 0;
  }
  size_t capacity = 1 << 16, used = 0;
  char * buffer = malloc(capacity);
  if (buffer == NULL) {
    fclose(file);
    return 0;
  }
  for (;;) {
    if (used == capacity) {
      capacity *= 2;
      char * grown = realloc(buffer, capacity);
      if (grown == NULL) {
        free(buffer);
        fclose(file);
        return 0;
      }
      buffer = grown;
    }
    size_t got = fread(buffer + used, 1, capacity - used, file);
    used += got;
    if (got == 0) {
      break;
    }
  }
  int failed = ferror(file);
  fclose(file);
  if (failed) {
    free(buffer);
    return 0;
  }
  *text = buffer;
  *length = used;
  return 1;
}

// The function re-parse check of one pair: the tree is read, and every function
// of it is read again.
static void reparse_pair(GLECMA_Source * source, Mode mode) {
  GLECMA_ParseOptions options = GLECMA_PARSE_OPTIONS_INIT;
  options.goal = mode == MODE_MODULE ? GLECMA_PARSE_MODULE : GLECMA_PARSE_SCRIPT;
  GLECMA_Tree * tree = NULL;
  GLECMA_Result result = glecma_parse(source, &options, NULL, NULL, &tree);
  if (result != GLECMA_OK) {
    printf("SKIP %s\n", result_name(result));
    return;
  }
  RpStats stats;
  memset(&stats, 0, sizeof stats);
  char message[600];
  if (rp_check_tree(source, tree, &options, &stats, message, sizeof message) != 0) {
    // One line: the message may hold a newline from a failure text.
    for (char * c = message; *c != '\0'; c++) {
      if (*c == '\n') {
        *c = ' ';
      }
    }
    printf("FAIL %s\n", message);
  } else {
    printf("REPARSED %llu", (unsigned long long)stats.functions);
    for (unsigned k = 1; k < GLECMA_FNKIND_COUNT; k++) {
      printf(" %llu", (unsigned long long)stats.kinds[k]);
    }
    for (unsigned f = 0; f < RP_FLAG_COUNT; f++) {
      printf(" %llu", (unsigned long long)stats.flags[f]);
    }
    printf(" %llu\n", (unsigned long long)stats.with_names);
  }
  glecma_tree_destroy(tree);
}

int main(int argc, char ** argv) {
  const int differential = argc > 1 && strcmp(argv[1], "--differential") == 0;
  const int reparse = argc > 1 && strcmp(argv[1], "--reparse") == 0;
  static const char directive[] = "\"use strict\";\n";
  static char line[1 << 14];
  while (fgets(line, sizeof line, stdin) != NULL) {
    size_t line_length = strlen(line);
    if (line_length == sizeof line - 1 && line[line_length - 1] != '\n') {
      fprintf(stderr, "t262_driver: a line longer than %zu bytes\n", sizeof line - 1);
      return 2;
    }
    while (line_length > 0 &&
           (line[line_length - 1] == '\n' || line[line_length - 1] == '\r')) {
      line[--line_length] = '\0';
    }
    char * tab = strchr(line, '\t');
    Mode mode;
    if (tab == NULL) {
      fprintf(stderr, "t262_driver: a line without a tab: %s\n", line);
      return 2;
    }
    *tab = '\0';
    if (!parse_mode(line, &mode)) {
      fprintf(stderr, "t262_driver: unknown mode %s\n", line);
      return 2;
    }
    char * text;
    size_t length;
    if (!read_file(tab + 1, &text, &length)) {
      fprintf(stderr, "t262_driver: cannot read %s\n", tab + 1);
      return 2;
    }
    const char * begin = text;
    size_t begin_length = length;
    char * prefixed = NULL;
    if (mode == MODE_STRICT) {
      prefixed = malloc(sizeof directive - 1 + length);
      if (prefixed == NULL) {
        free(text);
        return 2;
      }
      memcpy(prefixed, directive, sizeof directive - 1);
      memcpy(prefixed + sizeof directive - 1, text, length);
      begin = prefixed;
      begin_length = sizeof directive - 1 + length;
    }
    GLECMA_Failure failure = GLECMA_FAILURE_INIT;
    GLECMA_Source * source = NULL;
    GLECMA_Result result = glecma_source_create_utf8(
        begin, begin_length, NULL, NULL, &failure, &source);
    if (result == GLECMA_OK && reparse) {
      reparse_pair(source, mode);
      glecma_source_release(source);
      free(prefixed);
      free(text);
      continue;
    }
    if (result == GLECMA_OK && differential) {
      char tree_answer[160], checking_answer[160];
      describe(source, mode, 0, tree_answer, sizeof tree_answer);
      describe(source, mode, GLECMA_PARSE_INTERNAL_CHECKING, checking_answer,
          sizeof checking_answer);
      if (strcmp(tree_answer, checking_answer) == 0) {
        printf("SAME %s\n", tree_answer);
      } else {
        printf("DIFF %s | %s\n", tree_answer, checking_answer);
      }
      glecma_source_release(source);
      free(prefixed);
      free(text);
      continue;
    }
    if (result == GLECMA_OK) {
      result = parse_pair(source, mode);
      glecma_source_release(source);
    }
    puts(result_name(result));
    free(prefixed);
    free(text);
  }
  return fflush(stdout) == 0 ? 0 : 2;
}

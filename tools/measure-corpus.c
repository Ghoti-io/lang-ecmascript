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
 * Measures what the real-world corpus (tools/corpus/CORPUS.tsv) asks of the
 * defaults of `GLECMA_Limits`, and prints it: for each file the tokens it has
 * (and per byte), the longest literal in code points, the nodes its tree
 * holds (and per byte), the deepest nesting it reaches and the peak memory of
 * its parse. Each of the four counts is the least value of the field that
 * still admits the file, found by bisection through the public parse entry, so
 * it is what the field would have to be and not what a tool computes beside
 * the parser. It is how the defaults were re-measured when the corpus was
 * pinned (documentation/design.md, "Limits").
 *
 * Build and run it by hand against the built library, from the library root
 * with the corpus fetched, for example:
 *
 *   cc -std=c17 -O2 -I include -I src -I build/<target>/generated \
 *      $(pkg-config --cflags ghoti.io-cutil-0 ghoti.io-runtime-core-0 \
 *        ghoti.io-unicode-0 ghoti.io-regex-0) tools/measure-corpus.c \
 *      -Wl,--whole-archive build/<target>/apps/libghoti.io-lang-ecmascript-0.a \
 *      -Wl,--no-whole-archive $(pkg-config --libs ghoti.io-runtime-core-0 \
 *        ghoti.io-unicode-0 ghoti.io-regex-0 ghoti.io-cutil-0) -lm -pthread
 */

#include <ghoti.io/lang-ecmascript/lang-ecmascript.h>

#include "lex/lexer.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  GLECMA_Allocator allocator;
  size_t live;
  size_t peak;
} Meter;

typedef union {
  size_t size;
  max_align_t align;
} Header;

static void * meter_malloc(void * ctx, size_t size) {
  Meter * m = (Meter *)ctx;
  Header * h = (Header *)malloc(sizeof(Header) + size);
  if (h == NULL) {
    return NULL;
  }
  h->size = size;
  m->live += size;
  if (m->live > m->peak) {
    m->peak = m->live;
  }
  return h + 1;
}

static void * meter_calloc(void * ctx, size_t count, size_t size) {
  void * p = meter_malloc(ctx, count * size);
  if (p != NULL) {
    memset(p, 0, count * size);
  }
  return p;
}

static void * meter_realloc(void * ctx, void * old, size_t size) {
  Meter * m = (Meter *)ctx;
  if (old == NULL) {
    return meter_malloc(ctx, size);
  }
  Header * h = (Header *)old - 1;
  const size_t before = h->size;
  Header * grown = (Header *)realloc(h, sizeof(Header) + size);
  if (grown == NULL) {
    return NULL;
  }
  grown->size = size;
  m->live = m->live - before + size;
  if (m->live > m->peak) {
    m->peak = m->live;
  }
  return grown + 1;
}

static void meter_free(void * ctx, void * p) {
  Meter * m = (Meter *)ctx;
  if (p != NULL) {
    Header * h = (Header *)p - 1;
    m->live -= h->size;
    free(h);
  }
}

static void meter_init(Meter * m) {
  m->allocator.ctx = m;
  m->allocator.malloc_fn = meter_malloc;
  m->allocator.calloc_fn = meter_calloc;
  m->allocator.realloc_fn = meter_realloc;
  m->allocator.free_fn = meter_free;
  m->live = m->peak = 0;
}

static bool count_token(void * context, const GLECMA_Token * token) {
  (void)token;
  ++*(uint64_t *)context;
  return true;
}

typedef enum { F_LITERAL, F_NODES, F_NESTING } Field;

// Whether the source parses with `field` set to `value` (every other cap at its default, the
// nesting and the native stack out of the way when they are not the field).
static bool accepted(GLECMA_Source * source, bool module, Field field, uint64_t value) {
  GLECMA_Limits limits = GLECMA_LIMITS_INIT;
  glecma_limits_default(&limits);
  limits.native_stack_bytes = (uint64_t)1 << 30;
  limits.nesting = 100000;
  limits.nodes = (uint64_t)1 << 40;
  limits.literal_length = (uint64_t)1 << 40;
  limits.tokens = (uint64_t)1 << 40;
  switch (field) {
    case F_LITERAL: limits.literal_length = value; break;
    case F_NODES: limits.nodes = value; break;
    case F_NESTING: limits.nesting = value; break;
  }
  GLECMA_ParseOptions options = GLECMA_PARSE_OPTIONS_INIT;
  options.goal = module ? GLECMA_PARSE_MODULE : GLECMA_PARSE_SCRIPT;
  options.limits = &limits;
  GLECMA_Tree * tree = NULL;
  const GLECMA_Result r = glecma_parse(source, &options, NULL, NULL, &tree);
  glecma_tree_destroy(tree);
  return r == GLECMA_OK;
}

static uint64_t least(GLECMA_Source * source, bool module, Field field, uint64_t high) {
  uint64_t low = 0;
  while (low + 1 < high) {
    const uint64_t middle = low + (high - low) / 2;
    if (accepted(source, module, field, middle)) {
      high = middle;
    } else {
      low = middle;
    }
  }
  return high;
}

int main(int argc, char ** argv) {
  const char * corpus_dir = argc > 1 ? argv[1] : "third_party/corpus";
  const char * table_path = argc > 2 ? argv[2] : "tools/corpus/CORPUS.tsv";
  FILE * table = fopen(table_path, "r");
  if (table == NULL) {
    fprintf(stderr, "measure-corpus: cannot open %s\n", table_path);
    return 1;
  }
  printf("%-44s %10s %10s %7s %10s %9s %7s %8s %9s %7s\n", "file", "bytes", "tokens", "tok/B", "literal", "nodes",
      "node/B", "nesting", "peak", "peak/B");
  char line[2048];
  uint64_t total_bytes = 0;
  double max_tokens = 0, max_nodes = 0, max_peak = 0;
  uint64_t max_literal = 0, max_nesting = 0;
  while (fgets(line, sizeof line, table) != NULL) {
    if (line[0] == '#' || line[0] == '\n') {
      continue;
    }
    char * fields[10];
    size_t n = 0;
    for (char * c = line; n < 10;) {
      fields[n++] = c;
      char * tab = strchr(c, '\t');
      if (tab == NULL) {
        break;
      }
      *tab = '\0';
      c = tab + 1;
    }
    char directory[160];
    snprintf(directory, sizeof directory, "%s-%s", fields[0], fields[1]);
    for (char * c = directory; *c != '\0'; c++) {
      if (*c == '/') {
        *c = '_';
      }
    }
    char path[512];
    snprintf(path, sizeof path, "%s/%s/%s", corpus_dir, directory, fields[3]);
    FILE * file = fopen(path, "rb");
    if (file == NULL) {
      fprintf(stderr, "measure-corpus: cannot read %s (tools/corpus/fetch.sh fetches the corpus)\n", path);
      return 1;
    }
    size_t capacity = 1u << 20, used = 0;
    char * text = (char *)malloc(capacity);
    for (;;) {
      if (used == capacity) {
        capacity *= 2;
        text = (char *)realloc(text, capacity);
      }
      size_t got = fread(text + used, 1, capacity - used, file);
      used += got;
      if (got == 0) {
        break;
      }
    }
    fclose(file);
    const bool module = strcmp(fields[5], "module") == 0;
    GLECMA_Source * source = NULL;
    if (glecma_source_create_utf8(text, used, NULL, NULL, NULL, &source) != GLECMA_OK) {
      fprintf(stderr, "measure-corpus: %s was refused by the decoder\n", path);
      return 1;
    }
    uint64_t tokens = 0;
    GLECMA_Lexer lexer;
    if (glecma_lexer_open(&lexer, source, NULL, 0, NULL) != GLECMA_OK ||
        glecma_lexer_run(&lexer, count_token, &tokens) != GLECMA_OK) {
      fprintf(stderr, "measure-corpus: %s was refused by the tokenizer\n", path);
      return 1;
    }
    const uint64_t literal = least(source, module, F_LITERAL, (uint64_t)used + 1);
    const uint64_t nodes = least(source, module, F_NODES, (uint64_t)used * 2 + 16);
    const uint64_t nesting = least(source, module, F_NESTING, 100000);
    Meter meter;
    meter_init(&meter);
    GLECMA_ParseOptions options = GLECMA_PARSE_OPTIONS_INIT;
    options.goal = module ? GLECMA_PARSE_MODULE : GLECMA_PARSE_SCRIPT;
    GLECMA_Source * metered = NULL;
    GLECMA_Tree * tree = NULL;
    if (glecma_source_create_utf8(text, used, NULL, &meter.allocator, NULL, &metered) != GLECMA_OK ||
        glecma_parse(metered, &options, &meter.allocator, NULL, &tree) != GLECMA_OK) {
      fprintf(stderr, "measure-corpus: %s was refused\n", path);
      return 1;
    }
    glecma_tree_destroy(tree);
    glecma_source_release(metered);
    char name[64];
    snprintf(name, sizeof name, "%s/%s", fields[0], fields[3]);
    printf("%-44s %10zu %10llu %7.3f %10llu %9llu %7.3f %8llu %9zu %7.1f\n", name, used, (unsigned long long)tokens,
        (double)tokens / (double)used, (unsigned long long)literal, (unsigned long long)nodes,
        (double)nodes / (double)used, (unsigned long long)nesting, meter.peak, (double)meter.peak / (double)used);
    fflush(stdout);
    total_bytes += used;
    if ((double)tokens / (double)used > max_tokens) {
      max_tokens = (double)tokens / (double)used;
    }
    if ((double)nodes / (double)used > max_nodes) {
      max_nodes = (double)nodes / (double)used;
    }
    if ((double)meter.peak / (double)used > max_peak) {
      max_peak = (double)meter.peak / (double)used;
    }
    if (literal > max_literal) {
      max_literal = literal;
    }
    if (nesting > max_nesting) {
      max_nesting = nesting;
    }
    glecma_source_release(source);
    free(text);
  }
  fclose(table);
  printf("\n%llu bytes in all. The most per byte: %.3f tokens, %.3f nodes, %.1f bytes of peak memory; the longest literal %llu code points; "
         "the deepest nesting %llu levels.\n", (unsigned long long)total_bytes, max_tokens, max_nodes, max_peak,
      (unsigned long long)max_literal, (unsigned long long)max_nesting);
  return 0;
}

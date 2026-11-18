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

#include "parse/builder.h"

#include <stdint.h>
#include <stdio.h>
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

static inline GLECMA_Result fuzz_reader_read(void * context, void * buffer,
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
 * constructor when @p stream; with @p allocator (NULL for the default). The
 * result is what the chosen constructor returned.
 */
static inline GLECMA_Result fuzz_make_source_with(int wide, int stream,
    const uint8_t * data, size_t size, const GLECMA_Limits * limits,
    const GLECMA_Allocator * allocator, GLECMA_Failure * failure,
    GLECMA_Source ** out_source) {
  if (stream) {
    size_t bytes = wide ? size - (size & 1u) : size;
    FuzzReader reader = {data, bytes, 0, 1 + (size % 13u)};
    GLECMA_Stream * s = NULL;
    GLECMA_Result made = glecma_stream_create_callback(&fuzz_reader_read, NULL,
        &reader, allocator, &s);
    if (made == GLECMA_ERR_OOM) {
      return made;
    }
    if (made != GLECMA_OK) {
      abort();
    }
    GLECMA_Result r = glecma_source_create_stream(s,
        wide ? GLECMA_ENCODING_UTF16 : GLECMA_ENCODING_UTF8, limits, allocator,
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
    GLECMA_Result r = glecma_source_create_utf16(units, count, limits,
        allocator, failure, out_source);
    free(units);
    return r;
  }
  return glecma_source_create_utf8((const char *)data, size, limits, allocator,
      failure, out_source);
}

static inline GLECMA_Result fuzz_make_source(int wide, int stream,
    const uint8_t * data, size_t size, const GLECMA_Limits * limits,
    GLECMA_Failure * failure, GLECMA_Source ** out_source) {
  return fuzz_make_source_with(wide, stream, data, size, limits, NULL, failure,
      out_source);
}

/**
 * An allocator that counts its requests and live blocks and refuses the
 * request whose zero-based index is `fail_at` (SIZE_MAX refuses none): how a
 * harness fails each allocation of an operation in turn, and sees that
 * nothing it allocated is left.
 */
typedef struct {
  GLECMA_Allocator allocator;
  size_t calls;
  size_t live;
  size_t fail_at;
} FuzzAlloc;

static inline void * fuzz_alloc_malloc(void * ctx, size_t size) {
  FuzzAlloc * a = (FuzzAlloc *)ctx;
  if (a->calls++ == a->fail_at) {
    return NULL;
  }
  void * p = malloc(size == 0 ? 1 : size);
  if (p != NULL) {
    a->live++;
  }
  return p;
}

static inline void * fuzz_alloc_calloc(void * ctx, size_t count, size_t size) {
  FuzzAlloc * a = (FuzzAlloc *)ctx;
  if (a->calls++ == a->fail_at) {
    return NULL;
  }
  if (size != 0 && count > SIZE_MAX / size) {
    return NULL;
  }
  void * p = calloc(count == 0 ? 1 : count, size == 0 ? 1 : size);
  if (p != NULL) {
    a->live++;
  }
  return p;
}

static inline void * fuzz_alloc_realloc(void * ctx, void * old, size_t size) {
  FuzzAlloc * a = (FuzzAlloc *)ctx;
  if (a->calls++ == a->fail_at) {
    return NULL;
  }
  void * p = realloc(old, size == 0 ? 1 : size);
  if (p != NULL && old == NULL) {
    a->live++;
  }
  return p;
}

static inline void fuzz_alloc_free(void * ctx, void * p) {
  FuzzAlloc * a = (FuzzAlloc *)ctx;
  if (p != NULL) {
    a->live--;
    free(p);
  }
}

static inline void fuzz_alloc_init(FuzzAlloc * a, size_t fail_at) {
  a->allocator.ctx = a;
  a->allocator.malloc_fn = fuzz_alloc_malloc;
  a->allocator.calloc_fn = fuzz_alloc_calloc;
  a->allocator.realloc_fn = fuzz_alloc_realloc;
  a->allocator.free_fn = fuzz_alloc_free;
  a->calls = 0;
  a->live = 0;
  a->fail_at = fail_at;
}

/* ---- Checks the parser harnesses share: what a tree and a failure must hold ---- */

static inline void check(int condition) {
  if (!condition) {
    abort();
  }
}

typedef struct Walk {
  const GLECMA_Node ** stack;
  size_t count;
  size_t capacity;
} Walk;

static inline void push(Walk * walk, const GLECMA_Node * node) {
  if (walk->count == walk->capacity) {
    walk->capacity = walk->capacity ? walk->capacity * 2 : 256;
    walk->stack = (const GLECMA_Node **)realloc(walk->stack,
        walk->capacity * sizeof(*walk->stack));
    check(walk->stack != NULL);
  }
  walk->stack[walk->count++] = node;
}

// Every node, its children inside it, its kind known, its texts readable.
static inline size_t walk_tree(const GLECMA_Tree * tree, size_t units) {
  Walk walk = {NULL, 0, 0};
  const GLECMA_Node * root = glecma_tree_root(tree);
  check(root != NULL && (glecma_node_kind(root) == GLECMA_NODE_SCRIPT ||
      glecma_node_kind(root) == GLECMA_NODE_MODULE ||
      glecma_node_kind(root) == GLECMA_NODE_FUNCTION_EXPRESSION));
  push(&walk, root);
  size_t nodes = 0;
  while (walk.count > 0) {
    const GLECMA_Node * node = walk.stack[--walk.count];
    nodes++;
    check(glecma_node_kind(node) > 0 && glecma_node_kind(node) < GLECMA_NODE_KIND_COUNT);
    check(glecma_node_start(node) <= glecma_node_end(node));
    check(glecma_node_end(node) <= units);
    const uint16_t * text;
    size_t length;
    for (size_t i = 0; i < 2; i++) {
      if (glecma_node_text(node, i, &text, &length)) {
        check(text != NULL || length == 0);
        for (size_t k = 0; k < length; k++) {
          (void)text[k];
        }
      }
    }
    for (size_t i = 0; i < glecma_node_child_count(node); i++) {
      const GLECMA_Node * child = glecma_node_child(node, i);
      if (child == NULL) {
        continue;
      }
      check(glecma_node_start(child) >= glecma_node_start(node));
      check(glecma_node_end(child) <= glecma_node_end(node));
      push(&walk, child);
    }
  }
  free(walk.stack);
  return nodes;
}

static inline void check_events(const GLECMA_Tree * tree, size_t units) {
  uint32_t open[4096];
  size_t depth = 0;
  for (size_t i = 0; i < glecma_tree_event_count(tree); i++) {
    const GLECMA_BEvent * e = glecma_tree_event(tree, i);
    check(e != NULL && e->start <= e->end && e->end <= units);
    if (e->type == GLECMA_BEV_SCOPE_ENTER) {
      if (depth < sizeof open / sizeof open[0]) {
        open[depth] = e->what;
      }
      depth++;
    } else if (e->type == GLECMA_BEV_SCOPE_LEAVE) {
      check(depth > 0);
      depth--;
      if (depth < sizeof open / sizeof open[0]) {
        check(open[depth] == e->what);
      }
    } else if (e->type == GLECMA_BEV_DECLARE || e->type == GLECMA_BEV_REFERENCE) {
      check(e->name.units != NULL && e->name.length > 0);
    } else {
      check(e->type == GLECMA_BEV_USE);
    }
  }
  check(depth == 0);
}

static inline int same_events(const GLECMA_Tree * a, const GLECMA_Tree * b) {
  if (glecma_tree_event_count(a) != glecma_tree_event_count(b)) {
    return 0;
  }
  for (size_t i = 0; i < glecma_tree_event_count(a); i++) {
    const GLECMA_BEvent * x = glecma_tree_event(a, i);
    const GLECMA_BEvent * y = glecma_tree_event(b, i);
    if (x->type != y->type || x->what != y->what || x->start != y->start ||
        x->end != y->end || x->name.length != y->name.length ||
        (x->name.length > 0 &&
            memcmp(x->name.units, y->name.units, x->name.length * 2) != 0)) {
      return 0;
    }
  }
  return 1;
}

static const char * const fuzz_limit_names[] = {
  "tokens", "literal_length", "nesting", "nodes", "native_stack_bytes",
  "source_units",
};

static inline void check_failure(GLECMA_Result r, const GLECMA_Failure * f, size_t units) {
  check(f->result == (uint32_t)r);
  if (r == GLECMA_ERR_SYNTAX) {
    check(f->rule >= 1 && f->rule < (uint32_t)GLECMA_RULE_COUNT);
    check(f->message != NULL && f->location.offset <= units && f->location.line >= 1);
    check(f->limit == NULL);
  } else if (r == GLECMA_ERR_LIMIT) {
    check(f->limit != NULL);
    int known = 0;
    for (size_t i = 0; i < sizeof fuzz_limit_names / sizeof fuzz_limit_names[0]; i++) {
      known |= strcmp(f->limit, fuzz_limit_names[i]) == 0;
    }
    check(known && f->rule == 0);
  } else {
    check(f->rule == 0 && f->limit == NULL);
  }
}

#endif // GHOTI_IO_GLECMA_TESTS_FUZZ_FUZZ_COMMON_H

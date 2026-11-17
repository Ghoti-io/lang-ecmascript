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
 * The parser's fuzz harness.
 *
 * The first byte chooses what the rest is read as and how:
 *
 *   bit 0       the rest is UTF-16 code units in host order (else UTF-8)
 *   bit 1       strict code
 *   bit 2       the source is made from the library's input stream, read in
 *               short pieces, instead of from a buffer
 *   bit 3       the internal built-in entry (else the public one)
 *   bit 4       parse with the checking builder (else the tree builder)
 *   bits 5-6    the limits: 0 the defaults; 1 tiny nesting and node caps;
 *               2 a small native stack grant and a huge nesting cap, so that
 *               the stack, and not the nesting, is what stops a deep input;
 *               3 a small token cap
 *   bit 7       a feature set that names a feature (refused before any work)
 *
 * It checks what a hostile input must not break: the result is one of the few
 * a parser may return and the failure record is consistent with it; a tree
 * is walked and dumped without a fault, every child lies inside its parent,
 * every kind is known and every scope event is closed; the other builder
 * gives the same result, rule and place, and the same events; and parsing
 * again gives the same answer. An invariant that does not hold aborts.
 *
 * Built twice: by `make fuzz-parser` under libFuzzer, and by `make
 * fuzz-replay` as a plain program over the seed corpus.
 */

#include <ghoti.io/lang-ecmascript/lang-ecmascript.h>

#include "fuzz_common.h"
#include "parse/builder.h"
#include "parse/parse_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(int condition) {
  if (!condition) {
    abort();
  }
}

typedef struct Walk {
  const GLECMA_Node ** stack;
  size_t count;
  size_t capacity;
} Walk;

static void push(Walk * walk, const GLECMA_Node * node) {
  if (walk->count == walk->capacity) {
    walk->capacity = walk->capacity ? walk->capacity * 2 : 256;
    walk->stack = (const GLECMA_Node **)realloc(walk->stack,
        walk->capacity * sizeof(*walk->stack));
    check(walk->stack != NULL);
  }
  walk->stack[walk->count++] = node;
}

// Every node, its children inside it, its kind known, its texts readable.
static size_t walk_tree(const GLECMA_Tree * tree, size_t units) {
  Walk walk = {NULL, 0, 0};
  const GLECMA_Node * root = glecma_tree_root(tree);
  check(root != NULL && glecma_node_kind(root) == GLECMA_NODE_SCRIPT);
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

static void check_events(const GLECMA_Tree * tree, size_t units) {
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

static int same_events(const GLECMA_Tree * a, const GLECMA_Tree * b) {
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

static const char * const limit_names[] = {
  "tokens", "literal_length", "nesting", "nodes", "native_stack_bytes",
};

static void check_failure(GLECMA_Result r, const GLECMA_Failure * f, size_t units) {
  check(f->result == (uint32_t)r);
  if (r == GLECMA_ERR_SYNTAX) {
    check(f->rule >= 1 && f->rule < (uint32_t)GLECMA_RULE_COUNT);
    check(f->message != NULL && f->location.offset <= units && f->location.line >= 1);
    check(f->limit == NULL);
  } else if (r == GLECMA_ERR_LIMIT) {
    check(f->limit != NULL);
    int known = 0;
    for (size_t i = 0; i < sizeof limit_names / sizeof limit_names[0]; i++) {
      known |= strcmp(f->limit, limit_names[i]) == 0;
    }
    check(known && f->rule == 0);
  } else {
    check(f->rule == 0 && f->limit == NULL);
  }
}

int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
  if (size == 0) {
    return 0;
  }
  const uint8_t options_byte = data[0];
  data++;
  size--;
  const int wide = options_byte & 1;
  const int strict = (options_byte & 2) != 0;
  const int stream = (options_byte & 4) != 0;
  const int builtin = (options_byte & 8) != 0;
  const int checking = (options_byte & 16) != 0;
  const int limit_choice = (options_byte >> 5) & 3;
  const int features = (options_byte & 128) != 0;

  GLECMA_Limits limits = GLECMA_LIMITS_INIT;
  switch (limit_choice) {
    case 1: limits.nesting = 6; limits.nodes = 64; break;
    case 2: limits.nesting = 1000000; limits.native_stack_bytes = 40 * 1024; break;
    case 3: limits.tokens = 24; break;
    default: break;
  }
  GLECMA_FeatureSet feature_set = GLECMA_FEATURE_SET_INIT;
  GLECMA_ParseOptions options = GLECMA_PARSE_OPTIONS_INIT;
  options.limits = &limits;
  options.context = strict ? GLECMA_CONTEXT_STRICT : 0u;
  if (features) {
    glecma_feature_set_add(&feature_set, "fuzz-feature");
    options.features = &feature_set;
  }

  GLECMA_Failure failure = GLECMA_FAILURE_INIT;
  GLECMA_Source * source = NULL;
  GLECMA_Result created = fuzz_make_source(wide, stream, data, size, &limits,
      &failure, &source);
  if (created != GLECMA_OK) {
    check(created == GLECMA_ERR_CORRUPT || created == GLECMA_ERR_LIMIT);
    check(source == NULL);
    return 0;
  }
  const size_t units = glecma_source_units(source);
  const uint32_t first = (builtin ? GLECMA_PARSE_INTERNAL_BUILTIN : 0u) |
      (checking ? GLECMA_PARSE_INTERNAL_CHECKING : 0u);
  const uint32_t second = first ^ GLECMA_PARSE_INTERNAL_CHECKING;

  GLECMA_Tree * tree = NULL;
  GLECMA_Result r = glecma_parse_internal(source, &options, NULL, first, &failure,
      &tree);
  if (features) {
    check(r == GLECMA_ERR_UNSUPPORTED && tree == NULL);
    check(strcmp(failure.feature, "fuzz-feature") == 0);
    glecma_source_release(source);
    return 0;
  }
  check(r == GLECMA_OK || r == GLECMA_ERR_SYNTAX || r == GLECMA_ERR_LIMIT);
  check_failure(r, &failure, units);
  check((r == GLECMA_OK) == (tree != NULL));

  GLECMA_Tree * other = NULL;
  GLECMA_Failure other_failure = GLECMA_FAILURE_INIT;
  GLECMA_Result r2 = glecma_parse_internal(source, &options, NULL, second,
      &other_failure, &other);
  // The two builders agree on what the input is: the same result, and for a
  // refusal the same rule and the same place.
  check(r2 == r);
  if (r != GLECMA_OK) {
    check(other_failure.rule == failure.rule);
    check(other_failure.location.offset == failure.location.offset);
    check(other_failure.limit == failure.limit ||
        (other_failure.limit != NULL && failure.limit != NULL &&
            strcmp(other_failure.limit, failure.limit) == 0));
  }
  if (r == GLECMA_OK) {
    size_t nodes = walk_tree(tree, units);
    check(nodes > 0);
    check_events(tree, units);
    check(same_events(tree, other));
    check(glecma_tree_source(tree) == source);
    FILE * sink = fopen("/dev/null", "w");
    if (sink != NULL) {
      check(glecma_tree_dump(tree, sink) == GLECMA_OK);
      fclose(sink);
    }
    walk_tree(other, units);
    // The same input gives the same tree.
    GLECMA_Tree * again = NULL;
    GLECMA_Failure again_failure = GLECMA_FAILURE_INIT;
    check(glecma_parse_internal(source, &options, NULL, first, &again_failure,
              &again) == GLECMA_OK);
    check(walk_tree(again, units) == nodes);
    check(same_events(tree, again));
    glecma_tree_destroy(again);
  }
  glecma_tree_destroy(other);
  glecma_tree_destroy(tree);
  glecma_source_release(source);
  return 0;
}

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
 * The fuzz harness for the goals beyond a Script: the Module goal, direct and
 * indirect eval with the context a caller supplies, the dynamic-function goal,
 * and the function re-parse goal, which every accepted input here and in
 * fuzz_parser.c runs over each of its functions.
 *
 * The first byte chooses what the rest is read as and how:
 *
 *   bits 0-2    the goal: 0 a Script, 1 a Module, 2 direct eval, 3 indirect
 *               eval, 4 to 7 a dynamic function of the kind (goal - 4): a
 *               plain function, a generator, an async function, an async
 *               generator
 *   bit 3       UTF-16 code units in host order (else UTF-8)
 *   bit 4       the source is made from the library's input stream
 *   bits 5-6    the limits: 0 the defaults; 1 tiny nesting and node caps;
 *               2 a small native stack grant and a huge nesting cap; 3 a
 *               small token cap
 *   bit 7       a feature set that names a feature (refused before any work)
 *
 * The second byte is the goal's own choice. For a Script or a Module: bit 0
 * strict, bit 7 the checking builder. For direct eval: bits 0 to 4 the
 * context flags (some combinations the library must refuse), bit 5 private
 * names in scope, bit 6 strict by the options, bit 7 the checking builder. For
 * a dynamic function: where the rest of the input is split into the parameters
 * and the body.
 *
 * What it checks: the result is one a goal may return and the failure record is
 * consistent with it; a context the library should refuse is refused and one it
 * should accept is not; a tree is walked and dumped without a fault with every
 * child inside its parent; for every goal but a dynamic function the other
 * builder gives the same result, rule and place; every function of an accepted
 * program reads again as the tree it was; and the same input gives the same
 * answer twice. An invariant that does not hold aborts.
 *
 * Built twice: by `make fuzz-goals` under libFuzzer, and by `make fuzz-replay`
 * as a plain program over the seed corpus.
 */

#include <ghoti.io/lang-ecmascript/lang-ecmascript.h>

#include "fuzz_common.h"
#include "../reparse_support.h"
#include "parse/parse_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The private names a caller may put in scope.
static const uint16_t kNameX[] = {'x'};
static const uint16_t kNameY[] = {'y', 'y'};
static const uint16_t kNameWide[] = {0xD835, 0xDC9C, 'z'};

static int context_flags_valid(uint32_t flags) {
  const uint32_t strict_mask = GLECMA_CONTEXT_EVAL_MASK;
  if ((flags & ~strict_mask) != 0) {
    return 0;
  }
  if ((flags & GLECMA_CONTEXT_SUPER_CALL) != 0 && (flags & GLECMA_CONTEXT_SUPER_PROPERTY) == 0) {
    return 0;
  }
  if ((flags & GLECMA_CONTEXT_FIELD_INITIALIZER) != 0 && (flags & GLECMA_CONTEXT_SUPER_PROPERTY) == 0) {
    return 0;
  }
  if ((flags & GLECMA_CONTEXT_SUPER_PROPERTY) != 0 && (flags & GLECMA_CONTEXT_NEW_TARGET) == 0) {
    return 0;
  }
  return 1;
}

// A tree's source, which the re-parse takes as a mutable pointer because it
// retains it; the harness only reads through it.
static GLECMA_Source * mutable_source(const GLECMA_Source * source) {
  union {
    const GLECMA_Source * read_only;
    GLECMA_Source * writable;
  } view;
  view.read_only = source;
  return view.writable;
}

// Everything an accepted tree must hold.
static void check_tree(GLECMA_Source * source, const GLECMA_Tree * tree,
    const GLECMA_ParseOptions * options, int reparse) {
  const size_t units = glecma_source_units(glecma_tree_source(tree));
  check(walk_tree(tree, units) > 0);
  check_events(tree, units);
  FILE * sink = fopen("/dev/null", "w");
  if (sink != NULL) {
    check(glecma_tree_dump(tree, sink) == GLECMA_OK);
    fclose(sink);
  }
  if (reparse) {
    RpStats stats;
    char message[600];
    memset(&stats, 0, sizeof stats);
    check(rp_check_tree(source, tree, options, &stats, message, sizeof message) == 0);
  }
}

static void check_dynamic(const uint8_t * data, size_t size, uint32_t first_byte,
    const GLECMA_Limits * limits, int wide, int features) {
  const uint32_t kind = (first_byte & 7u) - 4u;
  const size_t split = size == 0 ? 0 : (size_t)data[0] % (size + 1);
  data++;
  size = size > 0 ? size - 1 : 0;
  const size_t cut = split > size ? size : split;
  GLECMA_Failure failure = GLECMA_FAILURE_INIT;
  GLECMA_Source * parameters = NULL;
  GLECMA_Source * body = NULL;
  if (fuzz_make_source(wide, 0, data, cut, limits, &failure, &parameters) != GLECMA_OK) {
    return;
  }
  if (fuzz_make_source(wide, 0, data + cut, size - cut, limits, &failure, &body) != GLECMA_OK) {
    glecma_source_release(parameters);
    return;
  }
  GLECMA_FeatureSet feature_set = GLECMA_FEATURE_SET_INIT;
  GLECMA_ParseOptions options = GLECMA_PARSE_OPTIONS_INIT;
  options.limits = limits;
  if (features) {
    glecma_feature_set_add(&feature_set, "fuzz-feature");
    options.features = &feature_set;
  }
  GLECMA_DynamicFunction function = GLECMA_DYNAMIC_FUNCTION_INIT;
  function.kind = kind;
  function.parameters = parameters;
  function.body = body;
  GLECMA_Tree * tree = NULL;
  GLECMA_Result r = glecma_parse_dynamic_function(&function, &options, NULL, &failure, &tree);
  if (features) {
    check(r == GLECMA_ERR_UNSUPPORTED && tree == NULL);
    check(strcmp(failure.feature, "fuzz-feature") == 0);
  } else {
    check(r == GLECMA_OK || r == GLECMA_ERR_SYNTAX || r == GLECMA_ERR_LIMIT);
    check((r == GLECMA_OK) == (tree != NULL));
    check_failure(r, &failure, glecma_source_units(parameters) + glecma_source_units(body) + 64);
    if (r == GLECMA_OK) {
      check(glecma_tree_goal(tree) == GLECMA_PARSE_DYNAMIC_FUNCTION);
      check(glecma_node_kind(glecma_tree_root(tree)) == GLECMA_NODE_FUNCTION_EXPRESSION);
      GLECMA_Source * assembled = mutable_source(glecma_tree_source(tree));
      check_tree(assembled, tree, &options, 1);
      // The same pieces give the same function.
      GLECMA_Tree * again = NULL;
      GLECMA_Failure again_failure = GLECMA_FAILURE_INIT;
      check(glecma_parse_dynamic_function(&function, &options, NULL, &again_failure, &again) == GLECMA_OK);
      check(walk_tree(again, glecma_source_units(glecma_tree_source(again))) ==
          walk_tree(tree, glecma_source_units(glecma_tree_source(tree))));
      glecma_tree_destroy(again);
    }
  }
  glecma_tree_destroy(tree);
  glecma_source_release(parameters);
  glecma_source_release(body);
}

int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
  if (size < 2) {
    return 0;
  }
  const uint8_t first = data[0];
  const uint8_t second = data[1];
  data += 2;
  size -= 2;
  const uint32_t goal = first & 7u;
  const int wide = (first & 8) != 0;
  const int stream = (first & 16) != 0;
  const int limit_choice = (first >> 5) & 3;
  const int features = (first & 128) != 0;

  GLECMA_Limits limits = GLECMA_LIMITS_INIT;
  switch (limit_choice) {
    case 1: limits.nesting = 6; limits.nodes = 64; break;
    case 2: limits.nesting = 1000000; limits.native_stack_bytes = 40 * 1024; break;
    case 3: limits.tokens = 24; break;
    default: break;
  }
  if (goal >= 4) {
    // The dynamic function reads its own pieces; the second byte is the split.
    check_dynamic(data - 1, size + 1, first, &limits, wide, features);
    return 0;
  }

  GLECMA_FeatureSet feature_set = GLECMA_FEATURE_SET_INIT;
  GLECMA_ParseOptions options = GLECMA_PARSE_OPTIONS_INIT;
  options.limits = &limits;
  static const uint32_t goals[] = {GLECMA_PARSE_SCRIPT, GLECMA_PARSE_MODULE,
      GLECMA_PARSE_DIRECT_EVAL, GLECMA_PARSE_INDIRECT_EVAL};
  options.goal = goals[goal];
  int checking = 0;
  GLECMA_EvalContext context = GLECMA_EVAL_CONTEXT_INIT;
  GLECMA_PrivateName names[3];
  int expect_invalid = 0;
  if (goal == 2) {
    context.flags = second & 0x1Fu;
    expect_invalid = !context_flags_valid(context.flags);
    if ((second & 0x20) != 0) {
      names[0].units = kNameX; names[0].length = 1; names[0].reserved = 0;
      names[1].units = kNameY; names[1].length = 2; names[1].reserved = 0;
      names[2].units = kNameWide; names[2].length = 3; names[2].reserved = 0;
      context.private_names = names;
      context.private_name_count = 3;
    }
    options.context = (second & 0x40) != 0 ? GLECMA_CONTEXT_STRICT : 0u;
    options.eval_context = &context;
    checking = (second & 0x80) != 0;
  } else {
    options.context = (second & 1) != 0 && goal != 3 ? GLECMA_CONTEXT_STRICT : 0u;
    checking = (second & 0x80) != 0;
  }
  if (features) {
    glecma_feature_set_add(&feature_set, "fuzz-feature");
    options.features = &feature_set;
  }

  GLECMA_Failure failure = GLECMA_FAILURE_INIT;
  GLECMA_Source * source = NULL;
  GLECMA_Result created = fuzz_make_source(wide, stream, data, size, &limits, &failure, &source);
  if (created != GLECMA_OK) {
    check(created == GLECMA_ERR_CORRUPT || created == GLECMA_ERR_LIMIT);
    check(source == NULL);
    return 0;
  }
  const size_t units = glecma_source_units(source);
  const uint32_t first_internal = checking ? GLECMA_PARSE_INTERNAL_CHECKING : 0u;
  const uint32_t second_internal = first_internal ^ GLECMA_PARSE_INTERNAL_CHECKING;

  GLECMA_Tree * tree = NULL;
  GLECMA_Result r = glecma_parse_internal(source, &options, NULL, first_internal, &failure, &tree);
  if (features) {
    check(r == GLECMA_ERR_UNSUPPORTED && tree == NULL);
    check(strcmp(failure.feature, "fuzz-feature") == 0);
    glecma_source_release(source);
    return 0;
  }
  if (expect_invalid) {
    check(r == GLECMA_ERR_INVALID && tree == NULL);
    glecma_source_release(source);
    return 0;
  }
  check(r == GLECMA_OK || r == GLECMA_ERR_SYNTAX || r == GLECMA_ERR_LIMIT);
  check_failure(r, &failure, units);
  check((r == GLECMA_OK) == (tree != NULL));

  GLECMA_Tree * other = NULL;
  GLECMA_Failure other_failure = GLECMA_FAILURE_INIT;
  GLECMA_Result r2 = glecma_parse_internal(source, &options, NULL, second_internal, &other_failure, &other);
  check(r2 == r);
  if (r != GLECMA_OK) {
    check(other_failure.rule == failure.rule);
    check(other_failure.location.offset == failure.location.offset);
  } else {
    check(glecma_tree_goal(tree) == options.goal);
    check(glecma_tree_source(tree) == source);
    if (goal == 1) {
      check(glecma_tree_strict(tree));
      check(glecma_node_kind(glecma_tree_root(tree)) == GLECMA_NODE_MODULE);
    }
    if (goal == 2) {
      const GLECMA_EvalContext * kept = glecma_tree_eval_context(tree);
      // The context the tree keeps says the code was strict however the caller said it.
      const uint32_t expected = context.flags |
          (options.context != 0 ? GLECMA_CONTEXT_STRICT : 0u);
      check(kept != NULL && kept->flags == expected &&
          kept->private_name_count == context.private_name_count);
      if ((expected & GLECMA_CONTEXT_STRICT) != 0) {
        check(glecma_tree_strict(tree));
      }
    } else {
      check(glecma_tree_eval_context(tree) == NULL);
    }
    // Every function reads again, in whichever tree has its bodies: the checking
    // builder's has none.
    check_tree(source, tree, &options, !checking);
    check_tree(source, other, &options, checking);
    check(same_events(tree, other));
    // The same input gives the same tree.
    GLECMA_Tree * again = NULL;
    GLECMA_Failure again_failure = GLECMA_FAILURE_INIT;
    check(glecma_parse_internal(source, &options, NULL, first_internal, &again_failure, &again) == GLECMA_OK);
    check(walk_tree(again, units) == walk_tree(tree, units));
    glecma_tree_destroy(again);
  }
  glecma_tree_destroy(other);
  glecma_tree_destroy(tree);
  glecma_source_release(source);
  return 0;
}

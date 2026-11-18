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
 * The function re-parse goal: one function read again from its range of the
 * source it was first read in, in the context it had, to give the tree the
 * first parse gave it. A function is compiled on its first call, after the
 * tree of the script that holds it is gone, so this is how a function that was
 * never called is read when it is.
 *
 * The context is the parser's own flags at the place the function's text
 * begins, so that the same parser code makes the same decisions: the first
 * parse already refused everything that is an error, and what this reads is
 * a function it accepted. The tokenizer is told the range ends where the
 * function does, so what lies after it (a `/` that begins a regular
 * expression after a function declaration, which the first parse rescanned) is
 * never read at all.
 */

#include <ghoti.io/lang-ecmascript/macros.h>

#include <ghoti.io/lang-ecmascript/goals.h>

#include "parse/parser.h"
#include "parse/parse_internal.h"

#include <string.h>

typedef struct ReparseData {
  uint32_t kind;
  bool declaration;
} ReparseData;

/* ---- What each kind reads ------------------------------------------------------------- */

// `async` before `function`, and a `*` after it, as the text says: the kind
// must say the same, because a function that is read as another one is not
// the function the first parse read.
static void function_text(Parser * p, bool * is_async, bool * is_generator) {
  GLECMA_Token first, second;
  *is_async = false;
  *is_generator = false;
  if (parser_is_kw(p, GLECMA_KW_ASYNC)) {
    if (!parser_peek2(p, &first, &second) || !token_is_kw(&first, GLECMA_KW_FUNCTION)) {
      parser_unexpected(p);
    }
    *is_async = true;
    *is_generator = second.kind == GLECMA_TOK_STAR;
  } else if (parser_is_kw(p, GLECMA_KW_FUNCTION)) {
    *is_generator = parser_peek(p, &first) && first.kind == GLECMA_TOK_STAR;
  } else {
    parser_unexpected(p);
  }
}

static GLECMA_BNode read_function(Parser * p, const ReparseData * data) {
  const size_t start = p->tok.start.offset;
  bool is_async, is_generator;
  function_text(p, &is_async, &is_generator);
  const uint32_t kind = data->kind;
  if (is_async != (kind == GLECMA_FNKIND_ASYNC_FUNCTION || kind == GLECMA_FNKIND_ASYNC_GENERATOR) ||
      is_generator != (kind == GLECMA_FNKIND_GENERATOR || kind == GLECMA_FNKIND_ASYNC_GENERATOR)) {
    parser_fail_token(p, GLECMA_RULE_UNEXPECTED_TOKEN);
  }
  if (is_async) {
    parser_next(p); // `async`
  }
  // A declaration binds its name in the code around it, which here is the scope
  // of the read; its name may be missing only in `export default function () {}`.
  return data->declaration
      ? parse_function_declaration(p, start, is_async, true, true).node
      : parse_function_expression(p, start, is_async).node;
}

static GLECMA_BNode read_arrow(Parser * p, const ReparseData * data) {
  const size_t start = p->tok.start.offset;
  // `async` is an async arrow's first word, unless it is the name of the one
  // parameter (`async => 1`).
  GLECMA_Token after;
  const bool async_text = parser_is_kw(p, GLECMA_KW_ASYNC) && parser_peek(p, &after) &&
      after.kind != GLECMA_TOK_ARROW;
  if (async_text != (data->kind == GLECMA_FNKIND_ASYNC_ARROW)) {
    parser_fail_token(p, GLECMA_RULE_UNEXPECTED_TOKEN);
  }
  PExpr arrow = parse_assignment(p, 0);
  if (arrow.shape != SH_ARROW || arrow.paren || arrow.start != start) {
    parser_fail_token(p, GLECMA_RULE_INVALID_ARROW_PARAMETERS);
  }
  return arrow.node;
}

// The initializer of a class field: an expression, which is the body of the
// function that evaluates it. Read as an Expression, because the range of a
// parenthesized initializer holds what is inside the parentheses.
static GLECMA_BNode read_initializer(Parser * p) {
  const size_t start = p->tok.start.offset;
  // The initializer's own code: a method's `super.x` and `new.target`, no
  // `arguments`, no `yield` or `await` operator of the code around, and strict
  // like the class (clause 15.7.1).
  p->flags = (p->flags & (PF_STRICT | PF_BUILTIN | PF_MODULE)) | PF_STRICT |
      PF_SUPER_PROP | PF_NEW_TARGET | PF_NO_ARGUMENTS |
      ((p->flags & PF_MODULE) != 0 ? PF_AWAIT_RESERVED : 0u);
  parser_event(p, GLECMA_BEV_SCOPE_ENTER, GLECMA_BSCOPE_FIELD_INIT, NULL, start,
      start);
  PExpr value = parse_expression(p);
  parser_event(p, GLECMA_BEV_SCOPE_LEAVE, GLECMA_BSCOPE_FIELD_INIT, NULL, start,
      value.end);
  p->last_body_strict = true;
  return value.node;
}

static void run_function(Parser * p, GLECMA_Tree * tree, const ParseJob * job,
    bool * strict) {
  const ReparseData * data = (const ReparseData *)job->data;
  const size_t units = glecma_source_units(p->source);
  parser_next(p);
  // The code around the function: a scope of its own, where a declaration's
  // name is bound.
  parser_event(p, GLECMA_BEV_SCOPE_ENTER, GLECMA_BSCOPE_SCRIPT, NULL, 0, units);
  GLECMA_BNode node = NULL;
  switch (data->kind) {
    case GLECMA_FNKIND_FUNCTION:
    case GLECMA_FNKIND_GENERATOR:
    case GLECMA_FNKIND_ASYNC_FUNCTION:
    case GLECMA_FNKIND_ASYNC_GENERATOR:
      node = read_function(p, data);
      break;
    case GLECMA_FNKIND_ARROW:
    case GLECMA_FNKIND_ASYNC_ARROW:
      node = read_arrow(p, data);
      break;
    case GLECMA_FNKIND_METHOD:
    case GLECMA_FNKIND_GETTER:
    case GLECMA_FNKIND_SETTER:
      node = parse_method_range(p, false, false);
      break;
    case GLECMA_FNKIND_CONSTRUCTOR:
    case GLECMA_FNKIND_DERIVED_CONSTRUCTOR:
      node = parse_method_range(p, true,
          data->kind == GLECMA_FNKIND_DERIVED_CONSTRUCTOR);
      break;
    case GLECMA_FNKIND_FIELD_INITIALIZER:
      node = read_initializer(p);
      break;
    default: // GLECMA_FNKIND_STATIC_BLOCK
      node = parse_static_block_range(p);
      p->last_body_strict = true;
      break;
  }
  // Exactly the range: nothing of the text is left over.
  if (p->tok.kind != GLECMA_TOK_EOF) {
    parser_unexpected(p);
  }
  early_finish(p);
  parser_event(p, GLECMA_BEV_SCOPE_LEAVE, GLECMA_BSCOPE_SCRIPT, NULL, 0, units);
  *strict = p->last_body_strict || (p->flags & PF_STRICT) != 0;
  glecma_tree_finish(tree, node, job->goal, *strict);
}

/* ---- The entry ------------------------------------------------------------------------- */

#define CONTEXT_WITHIN(size, member)                                           \
  ((size_t)(size) >= offsetof(GLECMA_FunctionContext, member) +                \
          sizeof(((GLECMA_FunctionContext *)0)->member))

// Whether two feature sets name the same features, in any order.
static bool same_features(const GLECMA_FeatureSet * a, const GLECMA_FeatureSet * b) {
  const uint32_t count_a = a != NULL ? a->count : 0u;
  const uint32_t count_b = b != NULL ? b->count : 0u;
  if (count_a != count_b) {
    return false;
  }
  for (uint32_t i = 0; i < count_a; i++) {
    if (!glecma_feature_set_has(b, a->names[i])) {
      return false;
    }
  }
  return true;
}

GLECMA_Result glecma_parse_function_internal(GLECMA_Source * source,
    const GLECMA_FunctionContext * context, const GLECMA_ParseOptions * options,
    const GLECMA_Allocator * allocator, uint32_t internal,
    GLECMA_Failure * failure, GLECMA_Tree ** out_tree) {
  GLECMA_Limits limits;
  if (failure != NULL && failure->size < GLECMA_FAILURE_HEADER_SIZE) {
    return GLECMA_ERR_INVALID;
  }
  if (source == NULL || context == NULL || out_tree == NULL) {
    return parser_refuse(failure, GLECMA_ERR_INVALID,
        "source, context or out_tree is NULL");
  }
  GLECMA_Result checked = parser_prologue(options, failure, &limits);
  if (checked != GLECMA_OK) {
    return checked;
  }
  if (context->size < GLECMA_FUNCTION_CONTEXT_HEADER_SIZE) {
    return parser_refuse(failure, GLECMA_ERR_INVALID,
        "the function context's size is below the header");
  }
  // A member beyond the caller's size takes its default.
  GLECMA_FunctionContext c = GLECMA_FUNCTION_CONTEXT_INIT;
  if (CONTEXT_WITHIN(context->size, kind)) {
    c.kind = context->kind;
  }
  if (CONTEXT_WITHIN(context->size, start) && CONTEXT_WITHIN(context->size, end)) {
    c.start = context->start;
    c.end = context->end;
  }
  if (CONTEXT_WITHIN(context->size, flags)) {
    c.flags = context->flags;
  }
  if (CONTEXT_WITHIN(context->size, private_name_count) &&
      CONTEXT_WITHIN(context->size, private_names)) {
    c.private_name_count = context->private_name_count;
    c.private_names = context->private_names;
  }
  if (CONTEXT_WITHIN(context->size, features)) {
    c.features = context->features;
  }
  const uint32_t flag_mask = GLECMA_CONTEXT_FUNCTION_MASK | GLECMA_FNCTX_DECLARATION;
  const uint32_t declaration_kinds = (1u << GLECMA_FNKIND_FUNCTION) |
      (1u << GLECMA_FNKIND_GENERATOR) | (1u << GLECMA_FNKIND_ASYNC_FUNCTION) |
      (1u << GLECMA_FNKIND_ASYNC_GENERATOR);
  if (c.kind == 0 || c.kind >= GLECMA_FNKIND_COUNT ||
      (c.flags & ~flag_mask) != 0 ||
      !glecma_context_flags_valid(c.flags & GLECMA_CONTEXT_FUNCTION_MASK,
          GLECMA_CONTEXT_FUNCTION_MASK) ||
      ((c.flags & GLECMA_FNCTX_DECLARATION) != 0 &&
          ((1u << c.kind) & declaration_kinds) == 0) ||
      !glecma_private_names_valid(c.private_names, c.private_name_count)) {
    return parser_refuse(failure, GLECMA_ERR_INVALID,
        "the function context has a kind, a flag or a private name that is not defined");
  }
  // The feature set of the first parse is the one this parse reads with.
  const GLECMA_FeatureSet * features = options != NULL &&
          options->size >= offsetof(GLECMA_ParseOptions, features) +
              sizeof(options->features)
      ? options->features : NULL;
  if (!same_features(c.features, features)) {
    return parser_refuse(failure, GLECMA_ERR_INVALID,
        "the function context and the parse options name different feature sets");
  }
  uint32_t goal, options_context;
  const GLECMA_EvalContext * eval;
  glecma_parse_options_raw(options, &goal, &options_context, &eval);
  if ((goal != GLECMA_PARSE_SCRIPT && goal != GLECMA_PARSE_FUNCTION) ||
      options_context != 0 || eval != NULL) {
    return parser_refuse(failure, GLECMA_ERR_INVALID,
        "a function read again takes no goal, strictness or eval context of its own");
  }
  const size_t units = glecma_source_units(source);
  if (c.start >= c.end || c.end > units) {
    return parser_refuse(failure, GLECMA_ERR_INVALID,
        "the function's range is empty, backwards or past the source");
  }

  ReparseData data;
  data.kind = c.kind;
  data.declaration = (c.flags & GLECMA_FNCTX_DECLARATION) != 0;
  const bool builtin = (internal & GLECMA_PARSE_INTERNAL_BUILTIN) != 0;
  // The context of the code around the function, as parser flags.
  const bool module = (c.flags & GLECMA_CONTEXT_MODULE) != 0;
  // The kinds that exist only in a class are class code, which is strict; a
  // method or an accessor may be an object literal's, so its strictness is the
  // context's.
  const bool strict = (c.flags & GLECMA_CONTEXT_STRICT) != 0 || module ||
      builtin || c.kind == GLECMA_FNKIND_CONSTRUCTOR ||
      c.kind == GLECMA_FNKIND_DERIVED_CONSTRUCTOR ||
      c.kind == GLECMA_FNKIND_FIELD_INITIALIZER ||
      c.kind == GLECMA_FNKIND_STATIC_BLOCK;
  ParseJob job;
  memset(&job, 0, sizeof(job));
  job.source = source;
  job.options = options;
  job.allocator = allocator;
  job.goal = GLECMA_PARSE_FUNCTION;
  job.internal = internal;
  job.flags = (strict ? PF_STRICT : 0u) | (builtin ? PF_BUILTIN : 0u) |
      (module ? (PF_MODULE | PF_AWAIT_RESERVED) : 0u) |
      ((c.flags & GLECMA_CONTEXT_NEW_TARGET) != 0 ? PF_NEW_TARGET : 0u) |
      ((c.flags & GLECMA_CONTEXT_SUPER_PROPERTY) != 0 ? PF_SUPER_PROP : 0u) |
      ((c.flags & GLECMA_CONTEXT_SUPER_CALL) != 0 ? PF_SUPER_CALL : 0u) |
      ((c.flags & GLECMA_CONTEXT_YIELD) != 0 ? PF_YIELD : 0u) |
      ((c.flags & GLECMA_CONTEXT_AWAIT) != 0 ? (PF_AWAIT | PF_AWAIT_RESERVED) : 0u) |
      ((c.flags & GLECMA_CONTEXT_FIELD_INITIALIZER) != 0 ? PF_NO_ARGUMENTS : 0u) |
      ((c.flags & GLECMA_CONTEXT_STATIC_BLOCK) != 0
          ? (PF_NO_ARGUMENTS | PF_AWAIT_RESERVED) : 0u);
  job.strict_lexer = strict;
  job.module_lexer = module;
  job.outer_private = c.private_names;
  job.outer_private_count = c.private_name_count;
  job.bounded = true;
  job.start = (size_t)c.start;
  job.end = (size_t)c.end;
  job.run = run_function;
  job.data = &data;
  GLECMA_Result result = parser_execute(&job, failure, out_tree);
  if (result == GLECMA_ERR_SYNTAX) {
    // The first parse reported every early error: a function it accepted that
    // does not read again is a defect, and is not the program's syntax error.
    GLECMA_Location where = {0, 0, 0};
    if (failure != NULL && failure->size >= GLECMA_FAILURE_HEADER_SIZE) {
      where = failure->location;
    }
    glecma_failure_set(failure, GLECMA_ERR_INTERNAL,
        "a function the first parse accepted does not read again from its range",
        &where, NULL, 0, NULL);
    return GLECMA_ERR_INTERNAL;
  }
  return result;
}

GLECMA_Result glecma_parse_function(GLECMA_Source * source,
    const GLECMA_FunctionContext * context, const GLECMA_ParseOptions * options,
    const GLECMA_Allocator * allocator, GLECMA_Failure * failure,
    GLECMA_Tree ** out_tree) {
  return glecma_parse_function_internal(source, context, options, allocator, 0,
      failure, out_tree);
}

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
 * The parse entries that read a whole source: a Script, a Module and the two
 * eval goals, and the one place every goal's entry runs the parser
 * (::parser_execute): the builder, the tokenizer, the limits, the cleanup
 * and the failure record are the same for all of them, so the goals that read
 * less than a whole source (a dynamic function, one function again) are a
 * few lines each over this.
 */

#include <ghoti.io/lang-ecmascript/macros.h>

#include "parse/parser.h"
#include "parse/parse_internal.h"

#include <string.h>

GLECMA_Result parser_refuse(GLECMA_Failure * failure, GLECMA_Result result,
    const char * message) {
  glecma_failure_set(failure, result, message, NULL, NULL, 0, NULL);
  return result;
}

GLECMA_Result parser_prologue(const GLECMA_ParseOptions * options,
    GLECMA_Failure * failure, GLECMA_Limits * limits) {
  if (failure != NULL && failure->size < GLECMA_FAILURE_HEADER_SIZE) {
    return GLECMA_ERR_INVALID;
  }
  const GLECMA_FeatureSet * features = NULL;
  if (!glecma_parse_options_resolve(options, limits, &features)) {
    return parser_refuse(failure, GLECMA_ERR_INVALID,
        "options or limits has a size below the header");
  }
  // Before any work: a feature the build lacks is refused, never ignored.
  const char * unbuilt = glecma_feature_set_first_unbuilt(features);
  if (unbuilt != NULL) {
    glecma_failure_set(failure, GLECMA_ERR_UNSUPPORTED,
        "the feature set names a feature this build lacks", NULL, NULL, 0,
        unbuilt);
    return GLECMA_ERR_UNSUPPORTED;
  }
  return GLECMA_OK;
}

// The private names of the code around, as interned names, so that a reference
// is matched to them by pointer like every other name.
static void intern_outer_names(Parser * p) {
  for (uint32_t i = 0; i < p->outer_private_count; i++) {
    GLECMA_BText name;
    GLECMA_Result r = p->builder->ops->text(p->builder,
        p->outer_private[i].units, p->outer_private[i].length, true, &name);
    if (r != GLECMA_OK) {
      parser_fail_result(p, r);
    }
    (void)parser_ptrset_add(p, &p->outer_names, name.units);
  }
}

GLECMA_Result parser_execute(const ParseJob * job, GLECMA_Failure * failure,
    GLECMA_Tree ** out_tree) {
  GLECMA_Limits limits;
  GLECMA_Result prologue = parser_prologue(job->options, failure, &limits);
  if (prologue != GLECMA_OK) {
    return prologue;
  }
  const bool builtin = (job->flags & PF_BUILTIN) != 0;
  const uint32_t mode = (job->internal & GLECMA_PARSE_INTERNAL_CHECKING) != 0
      ? GLECMA_BUILDER_CHECKING : GLECMA_BUILDER_TREE;

  GLECMA_Tree * tree = NULL;
  GLECMA_Result opened = glecma_tree_open(mode, job->source, job->allocator,
      &tree);
  if (opened != GLECMA_OK) {
    return parser_refuse(failure, opened,
        opened == GLECMA_ERR_OOM ? "allocation failed"
                                 : "the allocator has a NULL function");
  }
  const GLECMA_PrivateName * outer = job->outer_private;
  if (job->eval != NULL) {
    // The tree records the context the code was parsed in, in memory of its own.
    GLECMA_Result kept = glecma_tree_set_eval_context(tree, job->eval);
    if (kept != GLECMA_OK) {
      glecma_tree_destroy(tree);
      return parser_refuse(failure, kept, "allocation failed");
    }
    outer = glecma_tree_eval_context(tree)->private_names;
  }

  Parser parser;
  memset(&parser, 0, sizeof(parser));
  Parser * p = &parser;
  p->source = job->source;
  p->builder = glecma_tree_builder(tree);
  p->allocator = job->allocator != NULL ? job->allocator
                                        : glecma_allocator_default();
  glecma_lex_text_init(&p->text, job->source);
  p->max_nesting = limits.nesting;
  p->max_nodes = limits.nodes;
  p->native_stack_bytes = limits.native_stack_bytes;
  p->pending = NO_PENDING;
  p->pending_rule = GLECMA_RULE_COVER_INITIALIZED_NAME;
  p->body_simple = true; // a Script has no parameters
  p->params_simple = true;
  p->flags = job->flags;
  p->outer_private = outer;
  p->outer_private_count = job->outer_private_count;
  GLECMA_Result lexed = builtin
      ? glecma_lexer_open_builtin(&p->lex, job->source, job->options, failure)
      : glecma_lexer_open(&p->lex, job->source, job->options,
            job->module_lexer ? GLECMA_LEXF_MODULE
                              : job->strict_lexer ? GLECMA_LEXF_STRICT : 0u,
            failure);
  if (lexed == GLECMA_OK && job->bounded) {
    lexed = glecma_lexer_set_range(&p->lex, job->start, job->end);
    if (lexed != GLECMA_OK) {
      glecma_failure_set(failure, lexed, "the source range is not one", NULL,
          NULL, 0, NULL);
    }
  }
  if (lexed != GLECMA_OK) {
    glecma_tree_destroy(tree);
    return lexed;
  }
  p->stack_base = PARSER_STACK_POSITION();

  bool strict = false;
  if (setjmp(p->jump) == 0) {
    intern_outer_names(p);
    job->run(p, tree, job, &strict);
  }

  if (p->kids != NULL) {
    p->allocator->free_fn(p->allocator->ctx, p->kids);
  }
  if (p->memo != NULL) {
    p->allocator->free_fn(p->allocator->ctx, p->memo);
  }
  early_free(p);
  module_free(p);
  if (p->result != GLECMA_OK) {
    GLECMA_Result result = p->result;
    glecma_failure_set(failure, result, p->message,
        result == GLECMA_ERR_OOM ? NULL : &p->where, p->limit,
        result == GLECMA_ERR_SYNTAX ? p->rule : 0u, NULL);
    glecma_tree_destroy(tree);
    return result;
  }
  glecma_failure_set(failure, GLECMA_OK, NULL, NULL, NULL, 0, NULL);
  *out_tree = tree;
  return GLECMA_OK;
}

/* ---- A whole source: a Script, a Module, eval code -------------------------- */

// Parses the top level into `tree`; the failure, if any, is left in `p`.
static void run_source(Parser * p, GLECMA_Tree * tree, const ParseJob * job,
    bool * strict) {
  parser_next(p);
  size_t units = glecma_source_units(p->source);
  const bool module = job->goal == GLECMA_PARSE_MODULE;
  parser_event(p, GLECMA_BEV_SCOPE_ENTER,
      module ? GLECMA_BSCOPE_MODULE : GLECMA_BSCOPE_SCRIPT, NULL, 0, units);
  size_t mark = p->kids_len;
  bool is_strict = (p->flags & PF_STRICT) != 0;
  if (module) {
    // A Module has no directive prologue: it is strict, and its items are
    // statements, imports and exports (clause 16.2.1).
    parse_module_items(p);
    module_finish(p);
  } else {
    size_t end = 0;
    parse_statement_list(p, GLECMA_TOK_EOF, true, &end, &is_strict);
  }
  early_finish(p);
  parser_event(p, GLECMA_BEV_SCOPE_LEAVE,
      module ? GLECMA_BSCOPE_MODULE : GLECMA_BSCOPE_SCRIPT, NULL, 0, units);
  GLECMA_BNode root = parser_make(p,
      module ? GLECMA_NODE_MODULE : GLECMA_NODE_SCRIPT,
      is_strict ? GLECMA_NODEF_STRICT : 0u, 0, 0, units, mark);
  *strict = is_strict;
  glecma_tree_finish(tree, root, job->goal, is_strict);
}

GLECMA_Result glecma_parse_internal(GLECMA_Source * source,
    const GLECMA_ParseOptions * options, const GLECMA_Allocator * allocator,
    uint32_t internal, GLECMA_Failure * failure, GLECMA_Tree ** out_tree) {
  GLECMA_Limits limits;
  if (failure != NULL && failure->size < GLECMA_FAILURE_HEADER_SIZE) {
    return GLECMA_ERR_INVALID;
  }
  if (source == NULL || out_tree == NULL) {
    return parser_refuse(failure, GLECMA_ERR_INVALID,
        "source or out_tree is NULL");
  }
  GLECMA_Result checked = parser_prologue(options, failure, &limits);
  if (checked != GLECMA_OK) {
    return checked;
  }
  uint32_t goal = 0;
  uint32_t context = 0;
  if (glecma_parse_options_goal(options, &goal, &context) != GLECMA_OK) {
    return parser_refuse(failure, GLECMA_ERR_INVALID,
        "the goal or the context of the options is not defined");
  }
  // Only direct eval code has a context to inherit; anything else that was
  // given one, or one that is malformed, is refused before any work.
  GLECMA_EvalContext eval;
  if (glecma_parse_options_eval(options, goal, &eval) != GLECMA_OK) {
    return parser_refuse(failure, GLECMA_ERR_INVALID,
        "the eval context is malformed, or the goal takes none");
  }
  const bool builtin = (internal & GLECMA_PARSE_INTERNAL_BUILTIN) != 0;
  const bool module = goal == GLECMA_PARSE_MODULE;
  const bool strict_context = (context & GLECMA_CONTEXT_STRICT) != 0 ||
      (eval.flags & GLECMA_CONTEXT_STRICT) != 0;

  ParseJob job;
  memset(&job, 0, sizeof(job));
  job.source = source;
  job.options = options;
  job.allocator = allocator;
  job.goal = goal;
  job.internal = internal;
  // A Module is strict, reserves `await` everywhere, and its top level may
  // use it as an operator (top-level await, clause 16.2.1.6.1.3).
  job.flags = (strict_context || builtin || module ? PF_STRICT : 0u) |
      (builtin ? PF_BUILTIN : 0u) |
      (module ? (PF_AWAIT_RESERVED | PF_MODULE | PF_AWAIT) : 0u) |
      // What the code that called `eval` allows (clause 19.2.1.1).
      ((eval.flags & GLECMA_CONTEXT_NEW_TARGET) != 0 ? PF_NEW_TARGET : 0u) |
      ((eval.flags & GLECMA_CONTEXT_SUPER_PROPERTY) != 0 ? PF_SUPER_PROP : 0u) |
      ((eval.flags & GLECMA_CONTEXT_SUPER_CALL) != 0 ? PF_SUPER_CALL : 0u) |
      ((eval.flags & GLECMA_CONTEXT_FIELD_INITIALIZER) != 0 ? PF_NO_ARGUMENTS : 0u);
  job.strict_lexer = strict_context;
  job.module_lexer = module;
  job.eval = goal == GLECMA_PARSE_DIRECT_EVAL ? &eval : NULL;
  job.outer_private = eval.private_names;
  job.outer_private_count = eval.private_name_count;
  job.run = run_source;
  return parser_execute(&job, failure, out_tree);
}

GLECMA_Result glecma_parse(GLECMA_Source * source,
    const GLECMA_ParseOptions * options, const GLECMA_Allocator * allocator,
    GLECMA_Failure * failure, GLECMA_Tree ** out_tree) {
  return glecma_parse_internal(source, options, allocator, 0, failure,
      out_tree);
}

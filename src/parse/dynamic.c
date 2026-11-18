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
 * The dynamic-function goal: the parsing steps of CreateDynamicFunction
 * (clause 20.2.1.1.1). The parameters are read alone, the body is read alone,
 * and then the text those two make is read as the kind's function expression,
 * so that each is valid by itself and the whole has every early error of both.
 *
 * Each of the three readings is one run of the parser over a source of its own
 * (::parser_execute), and only the last one's tree is kept.
 */

#include <ghoti.io/lang-ecmascript/macros.h>

#include <ghoti.io/lang-ecmascript/goals.h>

#include "parse/parser.h"

#include <string.h>

/* ---- The three readings ---------------------------------------------------------- */

typedef struct DynamicData {
  uint32_t fn; ///< FN_ASYNC and FN_GENERATOR.
} DynamicData;

// The parameters alone: FormalParameters[?Yield, ?Await] to the end of the text.
// A yield or await expression in them is an error (clauses 15.5.1, 15.8.1),
// which the parser's parameter flag finds as it reads.
static void run_parameters(Parser * p, GLECMA_Tree * tree, const ParseJob * job,
    bool * strict) {
  const DynamicData * data = (const DynamicData *)job->data;
  const size_t units = glecma_source_units(p->source);
  parser_next(p);
  parser_event(p, GLECMA_BEV_SCOPE_ENTER, GLECMA_BSCOPE_FUNCTION, NULL, 0, units);
  p->flags = parser_function_flags(data->fn, 0u) | PF_PARAMS;
  GLECMA_BNode params = parse_formal_parameters(p, GLECMA_BDECL_PARAM);
  // Whether a name may repeat is a question of the parameters being simple.
  early_scope_function(p, false, p->params_simple, false);
  parser_event(p, GLECMA_BEV_SCOPE_LEAVE, GLECMA_BSCOPE_FUNCTION, NULL, 0, units);
  *strict = false;
  glecma_tree_finish(tree, params, job->goal, false);
}

// The body alone: the kind's function body to the end of the text.
static void run_body(Parser * p, GLECMA_Tree * tree, const ParseJob * job,
    bool * strict) {
  const DynamicData * data = (const DynamicData *)job->data;
  const size_t units = glecma_source_units(p->source);
  parser_next(p);
  parser_event(p, GLECMA_BEV_SCOPE_ENTER, GLECMA_BSCOPE_FUNCTION, NULL, 0, units);
  p->flags = parser_function_flags(data->fn, 0u);
  GLECMA_Result began = p->builder->ops->body_begin(p->builder);
  if (began != GLECMA_OK) {
    parser_fail_result(p, began);
  }
  const size_t mark = p->kids_len;
  size_t end = 0;
  bool is_strict = false;
  parse_statement_list(p, GLECMA_TOK_EOF, true, &end, &is_strict);
  early_scope_function(p, false, true, is_strict);
  parser_event(p, GLECMA_BEV_SCOPE_LEAVE, GLECMA_BSCOPE_FUNCTION, NULL, 0, units);
  GLECMA_BNode body = parser_make_body(p, GLECMA_NODE_FUNCTION_BODY,
      is_strict ? GLECMA_NODEF_STRICT : 0u, 0, units, mark);
  *strict = is_strict;
  glecma_tree_finish(tree, body, job->goal, is_strict);
}

// The assembled text as the kind's function expression, to the end of the text.
static void run_whole(Parser * p, GLECMA_Tree * tree, const ParseJob * job,
    bool * strict) {
  const DynamicData * data = (const DynamicData *)job->data;
  const size_t units = glecma_source_units(p->source);
  parser_next(p);
  parser_event(p, GLECMA_BEV_SCOPE_ENTER, GLECMA_BSCOPE_SCRIPT, NULL, 0, units);
  PExpr function;
  if ((data->fn & FN_ASYNC) != 0) {
    parser_next(p); // `async`
    function = parse_function_expression(p, 0, true);
  } else {
    function = parse_function_expression(p, 0, false);
  }
  if (p->tok.kind != GLECMA_TOK_EOF) {
    parser_unexpected(p);
  }
  early_finish(p);
  parser_event(p, GLECMA_BEV_SCOPE_LEAVE, GLECMA_BSCOPE_SCRIPT, NULL, 0, units);
  *strict = p->last_body_strict;
  glecma_tree_finish(tree, function.node, job->goal, *strict);
}

/* ---- The texts ------------------------------------------------------------------------ */

// A growing run of UTF-16 code units.
typedef struct Units {
  uint16_t * data;
  size_t length;
  size_t capacity;
} Units;

static GLECMA_Result units_reserve(const GLECMA_Allocator * a, Units * u,
    size_t more) {
  if (more > SIZE_MAX - u->length || u->length + more > SIZE_MAX / sizeof(uint16_t)) {
    return GLECMA_ERR_OOM;
  }
  if (u->length + more <= u->capacity) {
    return GLECMA_OK;
  }
  size_t capacity = u->capacity == 0 ? 64 : u->capacity;
  while (capacity < u->length + more) {
    if (capacity > SIZE_MAX / 2) {
      return GLECMA_ERR_OOM;
    }
    capacity *= 2;
  }
  if (capacity > SIZE_MAX / sizeof(uint16_t)) {
    return GLECMA_ERR_OOM;
  }
  uint16_t * grown = (uint16_t *)a->realloc_fn(a->ctx, u->data,
      capacity * sizeof(uint16_t));
  if (grown == NULL) {
    return GLECMA_ERR_OOM;
  }
  u->data = grown;
  u->capacity = capacity;
  return GLECMA_OK;
}

static GLECMA_Result units_ascii(const GLECMA_Allocator * a, Units * u,
    const char * text) {
  size_t n = strlen(text);
  GLECMA_Result r = units_reserve(a, u, n);
  for (size_t i = 0; r == GLECMA_OK && i < n; i++) {
    u->data[u->length++] = (uint16_t)(unsigned char)text[i];
  }
  return r;
}

// The code units of a source, whichever encoding it holds: a UTF-8 source is
// decoded and re-encoded (a source's text is code points, clause 11).
static GLECMA_Result units_source(const GLECMA_Allocator * a, Units * u,
    const GLECMA_Source * source) {
  if (source == NULL || source->units == 0) {
    return GLECMA_OK;
  }
  GLECMA_LexText text;
  glecma_lex_text_init(&text, source);
  if (text.u16 != NULL) {
    GLECMA_Result r = units_reserve(a, u, source->units);
    if (r == GLECMA_OK) {
      memcpy(u->data + u->length, text.u16, source->units * sizeof(uint16_t));
      u->length += source->units;
    }
    return r;
  }
  size_t offset = 0;
  while (offset < source->units) {
    size_t length = 0;
    uint32_t cp = glecma_lex_cp(&text, offset, &length);
    if (cp == GLECMA_LEX_NONE || cp == GLECMA_LEX_BAD || length == 0) {
      return GLECMA_ERR_INTERNAL; // a validated source decodes
    }
    GLECMA_Result r = units_reserve(a, u, 2);
    if (r != GLECMA_OK) {
      return r;
    }
    if (cp > 0xFFFFu) {
      u->data[u->length++] = (uint16_t)(0xD800u + ((cp - 0x10000u) >> 10));
      u->data[u->length++] = (uint16_t)(0xDC00u + ((cp - 0x10000u) & 0x3FFu));
    } else {
      u->data[u->length++] = (uint16_t)cp;
    }
    offset += length;
  }
  return GLECMA_OK;
}

static GLECMA_Result make_source(const GLECMA_Allocator * allocator,
    const GLECMA_Limits * limits, const Units * u, GLECMA_Failure * failure,
    GLECMA_Source ** out) {
  return glecma_source_create_utf16(u->data, u->length, limits, allocator,
      failure, out);
}

/* ---- The entry ------------------------------------------------------------------------ */

// The offset in the piece's own units that a UTF-16 offset of the text that
// was read stands for: a UTF-16 piece is the text, and a UTF-8 piece is walked
// by code points. An offset inside a surrogate pair is the start of the pair.
static size_t piece_offset(const GLECMA_Source * piece, size_t units16) {
  if (piece == NULL || piece->units == 0) {
    return 0;
  }
  GLECMA_LexText text;
  glecma_lex_text_init(&text, piece);
  if (text.u16 != NULL) {
    return units16 < piece->units ? units16 : piece->units;
  }
  size_t offset = 0;
  size_t seen = 0;
  while (offset < piece->units) {
    size_t length = 0;
    uint32_t cp = glecma_lex_cp(&text, offset, &length);
    if (cp == GLECMA_LEX_NONE || cp == GLECMA_LEX_BAD || length == 0) {
      break;
    }
    const size_t width = cp > 0xFFFFu ? 2 : 1;
    if (seen + width > units16) {
      break;
    }
    seen += width;
    offset += length;
  }
  return offset;
}

// A failure that was found in a piece's own text is placed in the piece the
// caller gave: after the line feed the algorithm puts in front of the body
// (`leading` units, which are not the caller's), clamped to the piece, and
// as an offset, a line and a column of the caller's own source.
static void place_in_piece(GLECMA_Failure * failure, const GLECMA_Source * piece,
    size_t leading) {
  if (failure == NULL || failure->size < GLECMA_FAILURE_HEADER_SIZE ||
      (failure->result != GLECMA_ERR_SYNTAX && failure->result != GLECMA_ERR_LIMIT)) {
    return;
  }
  size_t units16 = failure->location.offset > leading
      ? failure->location.offset - leading : 0;
  GLECMA_Location at = {0, 1, 0};
  if (piece != NULL) {
    at.offset = piece_offset(piece, units16);
    (void)glecma_source_location(piece, at.offset, &at);
  }
  failure->location = at;
}

GLECMA_Result glecma_parse_dynamic_function(
    const GLECMA_DynamicFunction * function, const GLECMA_ParseOptions * options,
    const GLECMA_Allocator * allocator, GLECMA_Failure * failure,
    GLECMA_Tree ** out_tree) {
  GLECMA_Limits limits;
  if (failure != NULL && failure->size < GLECMA_FAILURE_HEADER_SIZE) {
    return GLECMA_ERR_INVALID;
  }
  if (function == NULL || out_tree == NULL) {
    return parser_refuse(failure, GLECMA_ERR_INVALID,
        "function or out_tree is NULL");
  }
  GLECMA_Result checked = parser_prologue(options, failure, &limits);
  if (checked != GLECMA_OK) {
    return checked;
  }
  if (function->size < 8u || function->kind > GLECMA_DYNFN_ASYNC_GENERATOR) {
    return parser_refuse(failure, GLECMA_ERR_INVALID,
        "the function's size is below the header, or its kind is not one");
  }
  const GLECMA_Source * parameters = function->size >=
          offsetof(GLECMA_DynamicFunction, parameters) + sizeof(function->parameters)
      ? function->parameters : NULL;
  const GLECMA_Source * body = function->size >=
          offsetof(GLECMA_DynamicFunction, body) + sizeof(function->body)
      ? function->body : NULL;
  uint32_t goal, context;
  const GLECMA_EvalContext * eval;
  glecma_parse_options_raw(options, &goal, &context, &eval);
  if ((goal != GLECMA_PARSE_SCRIPT && goal != GLECMA_PARSE_DYNAMIC_FUNCTION) ||
      context != 0 || eval != NULL) {
    return parser_refuse(failure, GLECMA_ERR_INVALID,
        "a dynamic function takes no goal, strictness or eval context of its own");
  }
  if (allocator == NULL) {
    allocator = glecma_allocator_default();
  }
  if (allocator->malloc_fn == NULL || allocator->calloc_fn == NULL ||
      allocator->realloc_fn == NULL || allocator->free_fn == NULL) {
    return parser_refuse(failure, GLECMA_ERR_INVALID,
        "the allocator has a NULL function");
  }

  // The kind: its prefix and what its code is.
  static const char * const prefixes[] = {"function", "function*",
      "async function", "async function*"};
  DynamicData data;
  data.fn = ((function->kind & 1u) != 0 ? FN_GENERATOR : 0u) |
      ((function->kind & 2u) != 0 ? FN_ASYNC : 0u);

  GLECMA_Result result = GLECMA_OK;
  Units parameter_units = {NULL, 0, 0};
  Units body_units = {NULL, 0, 0};
  Units whole_units = {NULL, 0, 0};
  GLECMA_Source * parameter_source = NULL;
  GLECMA_Source * body_source = NULL;
  GLECMA_Source * whole_source = NULL;
  GLECMA_Tree * discarded = NULL;

  // P, and the body with the line feeds the algorithm puts around it.
  result = units_source(allocator, &parameter_units, parameters);
  if (result == GLECMA_OK) {
    result = units_ascii(allocator, &body_units, "\n");
  }
  if (result == GLECMA_OK) {
    result = units_source(allocator, &body_units, body);
  }
  if (result == GLECMA_OK) {
    result = units_ascii(allocator, &body_units, "\n");
  }
  // The assembled text: prefix, " anonymous(", P, LF, ") {", the body with its
  // line feeds, "}".
  if (result == GLECMA_OK) {
    result = units_ascii(allocator, &whole_units, prefixes[function->kind]);
  }
  if (result == GLECMA_OK) {
    result = units_ascii(allocator, &whole_units, " anonymous(");
  }
  if (result == GLECMA_OK) {
    result = units_reserve(allocator, &whole_units, parameter_units.length);
    if (result == GLECMA_OK && parameter_units.length > 0) {
      memcpy(whole_units.data + whole_units.length, parameter_units.data,
          parameter_units.length * sizeof(uint16_t));
      whole_units.length += parameter_units.length;
    }
  }
  if (result == GLECMA_OK) {
    result = units_ascii(allocator, &whole_units, "\n) {");
  }
  if (result == GLECMA_OK) {
    result = units_reserve(allocator, &whole_units, body_units.length);
    if (result == GLECMA_OK) {
      memcpy(whole_units.data + whole_units.length, body_units.data,
          body_units.length * sizeof(uint16_t));
      whole_units.length += body_units.length;
    }
  }
  if (result == GLECMA_OK) {
    result = units_ascii(allocator, &whole_units, "}");
  }
  if (result != GLECMA_OK) {
    glecma_failure_set(failure, result,
        result == GLECMA_ERR_OOM ? "allocation failed"
                                 : "the library lost its place",
        NULL, NULL, 0, NULL);
  }
  // The sources the three readings are over (the limits cap the text).
  if (result == GLECMA_OK) {
    result = make_source(allocator, &limits, &parameter_units, failure,
        &parameter_source);
  }
  if (result == GLECMA_OK) {
    result = make_source(allocator, &limits, &body_units, failure, &body_source);
  }
  if (result == GLECMA_OK) {
    result = make_source(allocator, &limits, &whole_units, failure,
        &whole_source);
  }

  ParseJob job;
  memset(&job, 0, sizeof(job));
  job.options = options;
  job.allocator = allocator;
  job.goal = GLECMA_PARSE_DYNAMIC_FUNCTION;
  job.data = &data;
  if (result == GLECMA_OK) {
    // 1. The parameters, alone.
    job.source = parameter_source;
    job.run = run_parameters;
    result = parser_execute(&job, failure, &discarded);
    glecma_tree_destroy(discarded);
    discarded = NULL;
    if (result != GLECMA_OK) {
      place_in_piece(failure, parameters, 0);
    }
  }
  if (result == GLECMA_OK) {
    // 2. The body, alone.
    job.source = body_source;
    job.run = run_body;
    result = parser_execute(&job, failure, &discarded);
    glecma_tree_destroy(discarded);
    discarded = NULL;
    if (result != GLECMA_OK) {
      place_in_piece(failure, body, 1);
    }
  }
  if (result == GLECMA_OK) {
    // 3. The assembled text, as the kind's function expression, with every
    // early error of both.
    job.source = whole_source;
    job.run = run_whole;
    result = parser_execute(&job, failure, out_tree);
  }

  glecma_source_release(parameter_source);
  glecma_source_release(body_source);
  glecma_source_release(whole_source);
  for (size_t i = 0; i < 3; i++) {
    Units * u = i == 0 ? &parameter_units : i == 1 ? &body_units : &whole_units;
    if (u->data != NULL) {
      allocator->free_fn(allocator->ctx, u->data);
    }
  }
  return result;
}

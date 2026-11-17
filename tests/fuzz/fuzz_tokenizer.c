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
 * The tokenizer's fuzz harness.
 *
 * The first byte chooses what the rest is read as and how:
 *
 *   bit 0       the rest is UTF-16 code units in host order (else UTF-8)
 *   bit 1       strict code
 *   bit 2       the Module goal
 *   bit 3       the internal built-in entry (else the public one)
 *   bits 4-5    the limits: 0 the defaults; 1 tiny token and literal caps;
 *               2 small ones; 3 a small source size and token cap
 *   bits 6-7    how the goals are chosen: 0 by the driver's guess; 1 rotated
 *               through all four goals by the input's own bytes; 2 as 0, and
 *               every token is read twice from a saved copy, which must give
 *               the same token; 3 as 0, with a feature set naming a feature
 *               (which must be refused before a token is read)
 *
 * It checks what a hostile input must not break: the result is one of the
 * few a tokenizer may return, a failure record is consistent with it, tokens
 * lie inside the source in order, their positions equal the source's own
 * query, a failure is the same every time, and a token's value is made and
 * freed without a leak or a fault. An invariant that does not hold aborts.
 *
 * Built twice: by `make fuzz-tokenizer` under libFuzzer, and by `make
 * fuzz-replay` as a plain program over the seed corpus.
 */

#include <ghoti.io/lang-ecmascript/lang-ecmascript.h>

#include "lex/lexer.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  const GLECMA_Source * source;
  size_t previous_end;
  size_t index;
  int failed;
} Visit;

static void check(int condition) {
  if (!condition) {
    abort();
  }
}

static bool visit(void * context, const GLECMA_Token * token) {
  Visit * v = (Visit *)context;
  size_t units = glecma_source_units(v->source);
  check(token->start.offset <= token->end.offset);
  check(token->end.offset <= units);
  check(token->start.offset >= v->previous_end);
  check((unsigned)token->kind < (unsigned)GLECMA_TOK_COUNT);
  if (token->kind != GLECMA_TOK_EOF) {
    check(token->end.offset > token->start.offset);
  }
  // The tokenizer's own positions equal the source's query; sampled so a
  // long input stays fast.
  if ((v->index & 7u) == 0) {
    GLECMA_Location where;
    check(glecma_source_location(v->source, token->start.offset, &where) ==
        GLECMA_OK);
    check(where.line == token->start.line && where.column == token->start.column);
    check(glecma_source_location(v->source, token->end.offset, &where) ==
        GLECMA_OK);
    check(where.line == token->end.line && where.column == token->end.column);
  }
  // A value is made and freed; a template with an undefined cooked value is
  // refused its cooked form, not a fault.
  if (token->kind == GLECMA_TOK_NAME || token->kind == GLECMA_TOK_STRING ||
      token->kind == GLECMA_TOK_TEMPLATE ||
      token->kind == GLECMA_TOK_PRIVATE_NAME ||
      token->kind == GLECMA_TOK_INTRINSIC_NAME) {
    uint16_t * units16 = NULL;
    size_t length = 0;
    GLECMA_Result r = glecma_token_value(v->source, token, GLECMA_VALUE_COOKED,
        NULL, &units16, &length);
    if (r == GLECMA_OK) {
      check(length <= token->end.offset - token->start.offset + 1);
      glecma_token_value_free(NULL, units16);
    } else {
      check(token->kind == GLECMA_TOK_TEMPLATE &&
          (token->flags & GLECMA_TOKF_COOKED_UNDEFINED) != 0 &&
          r == GLECMA_ERR_INVALID);
    }
    if (token->kind == GLECMA_TOK_TEMPLATE) {
      check(glecma_token_value(v->source, token, GLECMA_VALUE_RAW, NULL,
                &units16, &length) == GLECMA_OK);
      glecma_token_value_free(NULL, units16);
    }
  }
  v->previous_end = token->end.offset;
  v->index++;
  return true;
}

static GLECMA_Result run_driver(GLECMA_Lexer * lexer, const GLECMA_Source * source) {
  Visit v = {source, 0, 0, 0};
  return glecma_lexer_run(lexer, visit, &v);
}

// Every goal, in an order the input's own bytes pick.
static GLECMA_Result run_rotating(GLECMA_Lexer * lexer,
    const GLECMA_Source * source, const uint8_t * data, size_t size) {
  Visit v = {source, 0, 0, 0};
  for (size_t i = 0;; i++) {
    uint32_t goal = size == 0 ? 0 : (uint32_t)(data[i % size] & 3u);
    GLECMA_Token token;
    GLECMA_Result r = glecma_lexer_next(lexer, goal, &token);
    if (r != GLECMA_OK) {
      return r;
    }
    visit(&v, &token);
    if (token.kind == GLECMA_TOK_EOF) {
      return GLECMA_OK;
    }
  }
}

// Each token read twice from a saved copy must come out the same.
static GLECMA_Result run_twice(GLECMA_Lexer * lexer, const GLECMA_Source * source) {
  Visit v = {source, 0, 0, 0};
  for (;;) {
    GLECMA_Lexer saved = *lexer;
    GLECMA_Token first;
    GLECMA_Result r = glecma_lexer_next(lexer, GLECMA_GOAL_REGEXP, &first);
    GLECMA_Lexer after = *lexer;
    GLECMA_Token second;
    *lexer = saved;
    GLECMA_Result r2 = glecma_lexer_next(lexer, GLECMA_GOAL_REGEXP, &second);
    check(r == r2);
    if (r != GLECMA_OK) {
      return r;
    }
    check(memcmp(&first, &second, sizeof(first)) == 0);
    check(after.position.offset == lexer->position.offset);
    visit(&v, &first);
    if (first.kind == GLECMA_TOK_EOF) {
      return GLECMA_OK;
    }
  }
}

int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
  if (size == 0) {
    return 0;
  }
  const uint8_t options = data[0];
  data++;
  size--;
  const int wide = options & 1;
  const uint32_t mode = ((options & 2) ? GLECMA_LEXF_STRICT : 0u) |
      ((options & 4) ? GLECMA_LEXF_MODULE : 0u);
  const int builtin = (options & 8) != 0;
  const int limit_choice = (options >> 4) & 3;
  const int variant = (options >> 6) & 3;

  GLECMA_Limits limits = GLECMA_LIMITS_INIT;
  switch (limit_choice) {
    case 1: limits.tokens = 16; limits.literal_length = 8; break;
    case 2: limits.tokens = 256; limits.literal_length = 64; break;
    case 3: limits.source_units = 64; limits.tokens = 40; break;
    default: break;
  }
  GLECMA_ParseOptions parse = GLECMA_PARSE_OPTIONS_INIT;
  parse.limits = &limits;
  GLECMA_FeatureSet features = GLECMA_FEATURE_SET_INIT;
  if (variant == 3) {
    glecma_feature_set_add(&features, "fuzz-feature");
    parse.features = &features;
  }

  GLECMA_Source * source = NULL;
  GLECMA_Failure failure = GLECMA_FAILURE_INIT;
  GLECMA_Result created;
  uint16_t * units16 = NULL;
  if (wide) {
    size_t count = size / 2;
    units16 = (uint16_t *)malloc(count * sizeof(uint16_t) + 1);
    if (units16 == NULL) {
      return 0;
    }
    memcpy(units16, data, count * sizeof(uint16_t));
    created = glecma_source_create_utf16(units16, count, &limits, NULL,
        &failure, &source);
  } else {
    created = glecma_source_create_utf8((const char *)data, size, &limits,
        NULL, &failure, &source);
  }
  free(units16);
  if (created != GLECMA_OK) {
    check(created == GLECMA_ERR_CORRUPT || created == GLECMA_ERR_LIMIT);
    check(source == NULL);
    return 0;
  }

  GLECMA_Lexer lexer;
  GLECMA_Result opened = builtin
      ? glecma_lexer_open_builtin(&lexer, source, &parse, &failure)
      : glecma_lexer_open(&lexer, source, &parse, mode, &failure);
  if (variant == 3) {
    // A feature this build lacks is refused before a token is read.
    check(opened == GLECMA_ERR_UNSUPPORTED);
    check(strcmp(failure.feature, "fuzz-feature") == 0);
    glecma_source_release(source);
    return 0;
  }
  check(opened == GLECMA_OK);

  GLECMA_Lexer first_copy = lexer;
  GLECMA_Result r;
  if (variant == 1) {
    r = run_rotating(&lexer, source, data, size);
  } else if (variant == 2) {
    r = run_twice(&lexer, source);
  } else {
    r = run_driver(&lexer, source);
  }
  check(r == GLECMA_OK || r == GLECMA_ERR_SYNTAX || r == GLECMA_ERR_LIMIT);
  GLECMA_Failure out = GLECMA_FAILURE_INIT;
  glecma_lexer_failure(&lexer, &out);
  check(out.result == (uint32_t)r);
  if (r == GLECMA_ERR_SYNTAX) {
    check(out.rule >= 1 && out.rule < (uint32_t)GLECMA_RULE_COUNT);
    check(out.message != NULL && out.location.offset <= glecma_source_units(source));
    check(out.limit == NULL);
  } else if (r == GLECMA_ERR_LIMIT) {
    check(out.limit != NULL &&
        (strcmp(out.limit, "tokens") == 0 ||
            strcmp(out.limit, "literal_length") == 0));
    check(out.rule == 0);
  } else {
    check(out.rule == 0 && out.limit == NULL);
  }
  // The same input gives the same failure.
  if (variant == 0) {
    GLECMA_Result again = run_driver(&first_copy, source);
    check(again == r);
    GLECMA_Failure out2 = GLECMA_FAILURE_INIT;
    glecma_lexer_failure(&first_copy, &out2);
    check(out2.rule == out.rule && out2.location.offset == out.location.offset &&
        out2.message == out.message && out2.limit == out.limit);
  }
  glecma_source_release(source);
  return 0;
}

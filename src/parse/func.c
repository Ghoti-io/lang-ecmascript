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
 * Functions and classes (clauses 15.1 to 15.8): parameter lists, function
 * bodies with their directive prologues, function declarations and
 * expressions, methods and class definitions.
 *
 * A function's range is exactly the text `Function.prototype.toString` gives
 * it. A strict directive turns strictness on for the rest of the body and
 * the lexer is told at once; it is turned off again before the token after
 * the closing brace is read, so that a legacy octal literal after a strict
 * function is not refused as if it were inside it.
 */

#include <ghoti.io/lang-ecmascript/macros.h>

#include "parse/parser.h"

#include <string.h>

/* ---- Parameters and bodies --------------------------------------------------------- */

GLECMA_BNode parse_function_params(Parser * p, uint32_t decl_kind, uint32_t fn,
    size_t * out_end) {
  size_t start = p->tok.start.offset;
  size_t mark = p->kids_len;
  parser_expect(p, GLECMA_TOK_LPAREN);
  if ((fn & FN_GETTER) != 0) {
    // A getter takes no parameter.
    if (p->tok.kind != GLECMA_TOK_RPAREN) {
      parser_unexpected(p);
    }
  } else if ((fn & FN_SETTER) != 0) {
    // A setter takes exactly one, which is not a rest element.
    parser_kid(p, parse_binding_element(p, decl_kind).node);
    if (p->tok.kind != GLECMA_TOK_RPAREN) {
      parser_unexpected(p);
    }
  } else {
    while (p->tok.kind != GLECMA_TOK_RPAREN) {
      if (p->tok.kind == GLECMA_TOK_ELLIPSIS) {
        size_t rest_start = p->tok.start.offset;
        parser_next(p);
        PExpr target = parse_binding_target(p, decl_kind);
        size_t inner = p->kids_len;
        parser_kid(p, target.node);
        parser_kid(p, parser_make(p, GLECMA_NODE_REST_ELEMENT, 0, 0, rest_start,
                           target.end, inner));
        if (p->tok.kind != GLECMA_TOK_RPAREN) {
          parser_fail_token(p, GLECMA_RULE_REST_ELEMENT_POSITION);
        }
        break;
      }
      parser_kid(p, parse_binding_element(p, decl_kind).node);
      if (p->tok.kind != GLECMA_TOK_RPAREN) {
        parser_expect(p, GLECMA_TOK_COMMA);
      }
    }
  }
  size_t end = p->tok.end.offset;
  parser_next(p);
  *out_end = end;
  return parser_make(p, GLECMA_NODE_PARAMETER_LIST, 0, 0, start, end, mark);
}

GLECMA_BNode parse_function_body(Parser * p, size_t * out_end,
    bool * out_strict) {
  if (p->tok.kind != GLECMA_TOK_LBRACE) {
    parser_unexpected(p);
  }
  size_t start = p->tok.start.offset;
  GLECMA_Result began = p->builder->ops->body_begin(p->builder);
  if (began != GLECMA_OK) {
    parser_fail_result(p, began);
  }
  const bool outer_strict = (p->flags & PF_STRICT) != 0;
  parser_next(p);
  size_t mark = p->kids_len;
  bool strict = outer_strict;
  size_t unused = 0;
  parse_statement_list(p, GLECMA_TOK_RBRACE, true, &unused, &strict);
  size_t end = p->tok.end.offset;
  if (strict != outer_strict) {
    // Read what follows the brace in the strictness of the code around.
    parser_set_strict(p, outer_strict);
  }
  parser_next(p);
  *out_end = end;
  *out_strict = strict;
  return parser_make_body(p, GLECMA_NODE_FUNCTION_BODY,
      strict ? GLECMA_NODEF_STRICT : 0u, start, end, mark);
}

/* ---- Functions ---------------------------------------------------------------------- */

static uint32_t function_flags(uint32_t fn, uint32_t strict_and_builtin) {
  return (strict_and_builtin & (PF_STRICT | PF_BUILTIN)) |
      ((fn & FN_GENERATOR) != 0 ? PF_YIELD : 0u) |
      ((fn & FN_ASYNC) != 0 ? (PF_AWAIT | PF_AWAIT_RESERVED) : 0u);
}

// Parameters and body of a function whose scope was entered by the caller;
// leaves the scope. The token is the `(`.
static PExpr function_rest(Parser * p, size_t start, uint32_t fn,
    GLECMA_BNode id, uint32_t node_kind) {
  const uint32_t saved = p->flags;
  const bool saved_in = p->no_in;
  p->flags = function_flags(fn, saved);
  p->no_in = false;
  size_t params_end = 0;
  GLECMA_BNode params = parse_function_params(p, GLECMA_BDECL_PARAM, fn,
      &params_end);
  size_t end = 0;
  bool strict = false;
  GLECMA_BNode body = parse_function_body(p, &end, &strict);
  p->flags = saved;
  p->no_in = saved_in;
  parser_event(p, GLECMA_BEV_SCOPE_LEAVE, GLECMA_BSCOPE_FUNCTION, NULL, start,
      end);
  size_t mark = p->kids_len;
  parser_kid(p, id);
  parser_kid(p, params);
  parser_kid(p, body);
  uint32_t flags = ((fn & FN_ASYNC) != 0 ? GLECMA_NODEF_ASYNC : 0u) |
      ((fn & FN_GENERATOR) != 0 ? GLECMA_NODEF_GENERATOR : 0u);
  GLECMA_BNode node = parser_make(p, node_kind, flags, 0, start, end, mark);
  PExpr e;
  e.node = node;
  e.start = start;
  e.end = end;
  e.shape = SH_FUNCTION;
  e.paren = 0;
  e.aux = 0;
  return e;
}

PExpr parse_function_declaration(Parser * p, size_t start, bool is_async,
    bool allow_generator) {
  parser_next(p); // `function`
  uint32_t fn = is_async ? FN_ASYNC : 0u;
  if (p->tok.kind == GLECMA_TOK_STAR) {
    if (!allow_generator) {
      parser_unexpected(p);
    }
    fn |= FN_GENERATOR;
    parser_next(p);
  }
  // The name is read in the code around the function, and declared there.
  PExpr id = parse_binding_identifier(p, GLECMA_BDECL_FUNCTION);
  parser_event(p, GLECMA_BEV_SCOPE_ENTER, GLECMA_BSCOPE_FUNCTION, NULL, start,
      start);
  return function_rest(p, start, fn, id.node, GLECMA_NODE_FUNCTION_DECLARATION);
}

PExpr parse_function_expression(Parser * p, size_t start, bool is_async) {
  parser_next(p); // `function`
  uint32_t fn = is_async ? FN_ASYNC : 0u;
  if (p->tok.kind == GLECMA_TOK_STAR) {
    fn |= FN_GENERATOR;
    parser_next(p);
  }
  parser_event(p, GLECMA_BEV_SCOPE_ENTER, GLECMA_BSCOPE_FUNCTION, NULL, start,
      start);
  GLECMA_BNode id = NULL;
  if (p->tok.kind == GLECMA_TOK_NAME) {
    // A function expression's name is read as the function's own code reads it.
    const uint32_t saved = p->flags;
    p->flags = function_flags(fn, saved);
    id = parse_binding_identifier(p, GLECMA_BDECL_FUNCTION_NAME).node;
    p->flags = saved;
  }
  return function_rest(p, start, fn, id, GLECMA_NODE_FUNCTION_EXPRESSION);
}

PExpr parse_method(Parser * p, size_t start, uint32_t fn) {
  parser_event(p, GLECMA_BEV_SCOPE_ENTER, GLECMA_BSCOPE_FUNCTION, NULL, start,
      start);
  return function_rest(p, start, fn, NULL, GLECMA_NODE_FUNCTION_EXPRESSION);
}

/* ---- Classes --------------------------------------------------------------------------- */

// Whether the token after a `static`, `async`, `get` or `set` makes that word
// the element's name and not a modifier.
static bool word_is_name(const GLECMA_Token * after) {
  switch (after->kind) {
    case GLECMA_TOK_LPAREN: case GLECMA_TOK_ASSIGN: case GLECMA_TOK_SEMICOLON:
    case GLECMA_TOK_RBRACE: case GLECMA_TOK_EOF:
      return true;
    default:
      return false;
  }
}

static void parse_static_block(Parser * p, size_t start) {
  size_t mark = p->kids_len;
  parser_event(p, GLECMA_BEV_SCOPE_ENTER, GLECMA_BSCOPE_STATIC_BLOCK, NULL,
      start, start);
  const uint32_t saved = p->flags;
  const bool saved_in = p->no_in;
  // A static block is function-like: `await` is reserved in it, and it is an
  // operator in no code of its own.
  p->flags = (saved & (PF_STRICT | PF_BUILTIN)) | PF_AWAIT_RESERVED;
  p->no_in = false;
  parser_next(p); // `{`
  size_t end_unused = 0;
  bool strict = true;
  parse_statement_list(p, GLECMA_TOK_RBRACE, false, &end_unused, &strict);
  size_t end = p->tok.end.offset;
  p->flags = saved;
  p->no_in = saved_in;
  parser_next(p);
  parser_event(p, GLECMA_BEV_SCOPE_LEAVE, GLECMA_BSCOPE_STATIC_BLOCK, NULL,
      start, end);
  parser_kid(p, parser_make(p, GLECMA_NODE_STATIC_BLOCK, 0, 0, start, end,
                     mark));
}

static void parse_class_member(Parser * p, bool derived) {
  const size_t member_start = p->tok.start.offset;
  bool is_static = false;
  GLECMA_Token after;
  if (parser_is_kw(p, GLECMA_KW_STATIC) && parser_peek(p, &after) &&
      !word_is_name(&after)) {
    if (after.kind == GLECMA_TOK_LBRACE) {
      parser_next(p);
      parse_static_block(p, member_start);
      return;
    }
    is_static = true;
    parser_next(p);
  }
  const size_t function_start = p->tok.start.offset;
  uint32_t fn = 0;
  if (parser_is_kw(p, GLECMA_KW_ASYNC) && parser_peek(p, &after) &&
      !word_is_name(&after) && (after.flags & GLECMA_TOKF_NEWLINE_BEFORE) == 0) {
    fn |= FN_ASYNC;
    parser_next(p);
  }
  if (p->tok.kind == GLECMA_TOK_STAR) {
    fn |= FN_GENERATOR;
    parser_next(p);
  } else if (fn == 0 && (parser_is_kw(p, GLECMA_KW_GET) ||
                parser_is_kw(p, GLECMA_KW_SET)) &&
      parser_peek(p, &after) && token_starts_property_name(&after)) {
    fn |= p->tok.keyword == GLECMA_KW_GET ? FN_GETTER : FN_SETTER;
    parser_next(p);
  }
  PKey key;
  parse_property_key(p, &key, true);
  uint32_t flags = (is_static ? GLECMA_NODEF_STATIC : 0u) |
      (key.computed ? GLECMA_NODEF_COMPUTED : 0u);
  size_t mark = p->kids_len;
  if (p->tok.kind == GLECMA_TOK_LPAREN) {
    uint32_t kind = fn | FN_METHOD;
    if (!is_static && fn == 0 && !key.computed && !key.is_private &&
        (key.tok.kind == GLECMA_TOK_NAME || key.tok.kind == GLECMA_TOK_STRING) &&
        token_is_ascii(p, &key.tok, "constructor")) {
      kind |= FN_CONSTRUCTOR | (derived ? FN_DERIVED : 0u);
      flags |= GLECMA_NODEF_CONSTRUCTOR;
    }
    flags |= (fn & FN_GETTER) ? GLECMA_NODEF_GETTER
           : (fn & FN_SETTER) ? GLECMA_NODEF_SETTER : GLECMA_NODEF_METHOD;
    parser_kid(p, key.node);
    PExpr function = parse_method(p, function_start, kind);
    parser_kid(p, function.node);
    GLECMA_BNode member = parser_make(p, GLECMA_NODE_METHOD_DEFINITION, flags, 0,
        member_start, function.end, mark);
    parser_kid(p, member);
    return;
  }
  if (fn != 0) {
    parser_unexpected(p);
  }
  // A field.
  size_t end = key.end;
  GLECMA_BNode value = NULL;
  parser_kid(p, key.node);
  if (p->tok.kind == GLECMA_TOK_ASSIGN) {
    parser_next(p);
    size_t initial_start = p->tok.start.offset;
    parser_event(p, GLECMA_BEV_SCOPE_ENTER, GLECMA_BSCOPE_FIELD_INIT, NULL,
        initial_start, initial_start);
    const uint32_t saved = p->flags;
    const bool saved_in = p->no_in;
    p->flags = saved & (PF_STRICT | PF_BUILTIN);
    p->no_in = false;
    PExpr initial = parse_assignment(p, 0);
    p->flags = saved;
    p->no_in = saved_in;
    parser_event(p, GLECMA_BEV_SCOPE_LEAVE, GLECMA_BSCOPE_FIELD_INIT, NULL,
        initial_start, initial.end);
    value = initial.node;
    end = initial.end;
  }
  parser_kid(p, value);
  parser_semicolon(p);
  GLECMA_BNode member = parser_make(p, GLECMA_NODE_FIELD_DEFINITION, flags, 0,
      member_start, end, mark);
  parser_kid(p, member);
}

static PExpr parse_class(Parser * p, bool is_declaration) {
  const size_t start = p->tok.start.offset;
  const uint32_t saved = p->flags;
  const bool saved_strict = (saved & PF_STRICT) != 0;
  parser_next(p); // `class`
  // All parts of a class are strict code, its name and heritage included.
  parser_set_strict(p, true);
  GLECMA_BNode id = NULL;
  GLECMA_BText class_name = {NULL, 0};
  GLECMA_Token name_token;
  memset(&name_token, 0, sizeof(name_token));
  bool named = false;
  if (!is_declaration) {
    // A class expression's name is bound inside the class's own scope.
    parser_event(p, GLECMA_BEV_SCOPE_ENTER, GLECMA_BSCOPE_CLASS, NULL, start,
        start);
  }
  if (p->tok.kind == GLECMA_TOK_NAME && !parser_is_kw(p, GLECMA_KW_EXTENDS)) {
    name_token = p->tok;
    named = true;
    PExpr name = parse_binding_identifier(p,
        is_declaration ? GLECMA_BDECL_CLASS : GLECMA_BDECL_CLASS_NAME);
    id = name.node;
    parser_text(p, &name_token, GLECMA_VALUE_COOKED, true, &class_name);
  } else if (is_declaration) {
    parser_unexpected(p);
  }
  if (is_declaration) {
    // A declaration's name is bound outside, and again inside the scope.
    parser_event(p, GLECMA_BEV_SCOPE_ENTER, GLECMA_BSCOPE_CLASS, NULL, start,
        start);
    if (named) {
      parser_event(p, GLECMA_BEV_DECLARE, GLECMA_BDECL_CLASS_NAME, &class_name,
          name_token.start.offset, name_token.end.offset);
    }
  }
  GLECMA_BNode heritage = NULL;
  bool derived = false;
  if (parser_is_kw(p, GLECMA_KW_EXTENDS)) {
    derived = true;
    parser_next(p);
    heritage = parse_lhs(p).node;
  }
  if (p->tok.kind != GLECMA_TOK_LBRACE) {
    parser_unexpected(p);
  }
  size_t body_start = p->tok.start.offset;
  size_t mark = p->kids_len;
  parser_next(p);
  const bool saved_in = p->no_in;
  p->no_in = false;
  while (p->tok.kind != GLECMA_TOK_RBRACE) {
    if (parser_eat(p, GLECMA_TOK_SEMICOLON)) {
      continue;
    }
    parse_class_member(p, derived);
  }
  p->no_in = saved_in;
  size_t end = p->tok.end.offset;
  if (!saved_strict) {
    parser_set_strict(p, false);
  }
  parser_next(p); // `}`
  parser_event(p, GLECMA_BEV_SCOPE_LEAVE, GLECMA_BSCOPE_CLASS, NULL, start, end);
  GLECMA_BNode body = parser_make(p, GLECMA_NODE_CLASS_BODY, 0, 0, body_start,
      end, mark);
  parser_kid(p, id);
  parser_kid(p, heritage);
  parser_kid(p, body);
  GLECMA_BNode node = parser_make(p,
      is_declaration ? GLECMA_NODE_CLASS_DECLARATION : GLECMA_NODE_CLASS_EXPRESSION,
      derived ? GLECMA_NODEF_DERIVED : 0u, 0, start, end, mark);
  PExpr e;
  e.node = node;
  e.start = start;
  e.end = end;
  e.shape = SH_FUNCTION;
  e.paren = 0;
  e.aux = 0;
  p->flags = saved;
  return e;
}

PExpr parse_class_declaration(Parser * p) {
  return parse_class(p, true);
}

PExpr parse_class_expression(Parser * p) {
  return parse_class(p, false);
}

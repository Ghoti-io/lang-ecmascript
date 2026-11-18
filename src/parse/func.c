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

// The elements of a parameter list up to the token that ends it, which is not
// read: the parameters of a function, and the whole text of the parameters of a
// dynamic function (clause 20.2.1.1.1). Returns whether every one was a plain
// identifier (clause 15.1.1).
static bool parameter_elements(Parser * p, uint32_t decl_kind, uint32_t fn,
    GLECMA_TokenKind end_kind) {
  bool simple = true;
  if ((fn & FN_GETTER) != 0) {
    // A getter takes no parameter.
    if (p->tok.kind != end_kind) {
      parser_unexpected(p);
    }
  } else if ((fn & FN_SETTER) != 0) {
    // A setter takes exactly one, which is not a rest element.
    PExpr only = parse_binding_element(p, decl_kind);
    simple = only.shape == SH_IDENT;
    parser_kid(p, only.node);
    if (p->tok.kind != end_kind) {
      parser_unexpected(p);
    }
  } else {
    while (p->tok.kind != end_kind) {
      if (p->tok.kind == GLECMA_TOK_EOF) {
        parser_unexpected(p);
      }
      if (p->tok.kind == GLECMA_TOK_ELLIPSIS) {
        size_t rest_start = p->tok.start.offset;
        simple = false;
        parser_next(p);
        PExpr target = parse_binding_target(p, decl_kind);
        size_t inner = p->kids_len;
        parser_kid(p, target.node);
        parser_kid(p, parser_make(p, GLECMA_NODE_REST_ELEMENT, 0, 0, rest_start,
                           target.end, inner));
        if (p->tok.kind != end_kind) {
          parser_fail_token(p, GLECMA_RULE_REST_ELEMENT_POSITION);
        }
        break;
      }
      PExpr element = parse_binding_element(p, decl_kind);
      if (element.shape != SH_IDENT) {
        simple = false;
      }
      parser_kid(p, element.node);
      if (p->tok.kind != end_kind) {
        parser_expect(p, GLECMA_TOK_COMMA);
      }
    }
  }
  return simple;
}

GLECMA_BNode parse_function_params(Parser * p, uint32_t decl_kind, uint32_t fn,
    size_t * out_end) {
  size_t start = p->tok.start.offset;
  size_t mark = p->kids_len;
  parser_expect(p, GLECMA_TOK_LPAREN);
  bool simple = parameter_elements(p, decl_kind, fn, GLECMA_TOK_RPAREN);
  size_t end = p->tok.end.offset;
  parser_next(p);
  p->params_simple = simple;
  *out_end = end;
  return parser_make(p, GLECMA_NODE_PARAMETER_LIST, 0, 0, start, end, mark);
}

GLECMA_BNode parse_formal_parameters(Parser * p, uint32_t decl_kind) {
  size_t mark = p->kids_len;
  bool simple = parameter_elements(p, decl_kind, 0, GLECMA_TOK_EOF);
  p->params_simple = simple;
  return parser_make(p, GLECMA_NODE_PARAMETER_LIST, 0, 0, 0,
      glecma_source_units(p->source), mark);
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

// The context of a function's code: its own, whatever the code around it
// allowed. A method may use `super.x` and a derived constructor `super()`;
// no function but an arrow inherits what the code around it allowed.
uint32_t parser_function_flags(uint32_t fn, uint32_t strict_and_builtin) {
  return (strict_and_builtin & (PF_STRICT | PF_BUILTIN)) | PF_RETURN |
      PF_NEW_TARGET |
      ((fn & FN_METHOD) != 0 ? PF_SUPER_PROP : 0u) |
      ((fn & FN_CONSTRUCTOR) != 0 && (fn & FN_DERIVED) != 0 ? PF_SUPER_CALL : 0u) |
      ((fn & FN_GENERATOR) != 0 ? PF_YIELD : 0u) |
      ((fn & FN_ASYNC) != 0 ? (PF_AWAIT | PF_AWAIT_RESERVED) : 0u) |
      ((strict_and_builtin & PF_MODULE) != 0 ? (PF_MODULE | PF_AWAIT_RESERVED) : 0u);
}

// Parameters and body of a function whose scope was entered by the caller;
// leaves the scope. The token is the `(`.
static PExpr function_rest(Parser * p, size_t start, uint32_t fn,
    GLECMA_BNode id, uint32_t node_kind) {
  const uint32_t saved = p->flags;
  const bool saved_in = p->no_in;
  const bool saved_simple = p->body_simple;
  const JumpCtx jumps = early_jump_enter(p);
  p->flags = parser_function_flags(fn, saved);
  p->no_in = false;
  size_t params_end = 0;
  // Parameters are not the body: no `yield` or `await` expression in them.
  p->flags |= PF_PARAMS;
  GLECMA_BNode params = parse_function_params(p, GLECMA_BDECL_PARAM, fn,
      &params_end);
  p->flags &= ~PF_PARAMS;
  p->body_simple = p->params_simple;
  const bool simple = p->params_simple;
  size_t end = 0;
  bool strict = false;
  GLECMA_BNode body = parse_function_body(p, &end, &strict);
  early_scope_function(p, (fn & FN_METHOD) != 0, simple, strict);
  p->last_body_strict = strict;
  early_jump_leave(p, jumps);
  p->body_simple = saved_simple;
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
  e.ref_start = start;
  e.shape = SH_FUNCTION;
  e.paren = 0;
  e.aux = 0;
  return e;
}

PExpr parse_function_declaration(Parser * p, size_t start, bool is_async,
    bool allow_generator, bool allow_anonymous) {
  const Retro outer = parser_retro_enter(p);
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
  // `export default function () {}` (clause 16.2.3) has no name to bind.
  PExpr id;
  if (allow_anonymous && p->tok.kind == GLECMA_TOK_LPAREN) {
    id.node = NULL;
  } else {
    p->decl_flags = fn == 0 ? DF_PLAIN_FUNCTION : 0u;
    id = parse_binding_identifier(p, GLECMA_BDECL_FUNCTION);
    p->decl_flags = 0;
  }
  parser_event(p, GLECMA_BEV_SCOPE_ENTER, GLECMA_BSCOPE_FUNCTION, NULL, start,
      start);
  PExpr function = function_rest(p, start, fn, id.node,
      GLECMA_NODE_FUNCTION_DECLARATION);
  parser_retro_leave(p, outer);
  return function;
}

PExpr parse_function_expression(Parser * p, size_t start, bool is_async) {
  const Retro outer = parser_retro_enter(p);
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
    p->flags = parser_function_flags(fn, saved);
    id = parse_binding_identifier(p, GLECMA_BDECL_FUNCTION_NAME).node;
    p->flags = saved;
  }
  PExpr function = function_rest(p, start, fn, id,
      GLECMA_NODE_FUNCTION_EXPRESSION);
  parser_retro_leave(p, outer);
  return function;
}

PExpr parse_method(Parser * p, size_t start, uint32_t fn) {
  const Retro outer = parser_retro_enter(p);
  parser_event(p, GLECMA_BEV_SCOPE_ENTER, GLECMA_BSCOPE_FUNCTION, NULL, start,
      start);
  PExpr function = function_rest(p, start, fn, NULL,
      GLECMA_NODE_FUNCTION_EXPRESSION);
  parser_retro_leave(p, outer);
  return function;
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
  const bool saved_simple = p->body_simple;
  const JumpCtx jumps = early_jump_enter(p);
  // A static block is function-like: `await` is reserved in it, it is an
  // operator in no code of its own, it has no `return`, and `super.x` and
  // `new.target` are allowed in it but not `super()` or `arguments`
  // (clause 15.7.1).
  p->flags = (saved & (PF_STRICT | PF_BUILTIN | PF_MODULE)) | PF_AWAIT_RESERVED |
      PF_SUPER_PROP | PF_NEW_TARGET | PF_NO_ARGUMENTS;
  p->no_in = false;
  p->body_simple = true;
  parser_next(p); // `{`
  size_t end_unused = 0;
  bool strict = true;
  parse_statement_list(p, GLECMA_TOK_RBRACE, false, &end_unused, &strict);
  size_t end = p->tok.end.offset;
  early_jump_leave(p, jumps);
  p->body_simple = saved_simple;
  p->flags = saved;
  p->no_in = saved_in;
  parser_next(p);
  parser_event(p, GLECMA_BEV_SCOPE_LEAVE, GLECMA_BSCOPE_STATIC_BLOCK, NULL,
      start, end);
  parser_kid(p, parser_make(p, GLECMA_NODE_STATIC_BLOCK, 0, 0, start, end,
                     mark));
}

/* ---- One method, or one static block, read again from its range ---------------- */

// A method read again from the text of its `toString`: the modifiers, the name
// and then the parameters and the body, with the context of the class around it
// in the parser's flags. The name is read to find where the parameters begin
// and is then forgotten: the builder is taken back to where it stood, so that a
// computed name's references and functions are not the method's. What the
// first parse made of the member is the function node alone, and so is this.
GLECMA_BNode parse_method_range(Parser * p, bool constructor, bool derived) {
  const size_t start = p->tok.start.offset;
  uint32_t fn = 0;
  GLECMA_Token after;
  // A word is a modifier unless the parameters follow it: the text is known to
  // be a method, so `get(){}` is a method called get.
  if (parser_is_kw(p, GLECMA_KW_ASYNC) && parser_peek(p, &after) &&
      after.kind != GLECMA_TOK_LPAREN) {
    fn |= FN_ASYNC;
    parser_next(p);
  }
  if (p->tok.kind == GLECMA_TOK_STAR) {
    fn |= FN_GENERATOR;
    parser_next(p);
  } else if (fn == 0 && (parser_is_kw(p, GLECMA_KW_GET) ||
                parser_is_kw(p, GLECMA_KW_SET)) &&
      parser_peek(p, &after) && after.kind != GLECMA_TOK_LPAREN) {
    fn |= p->tok.keyword == GLECMA_KW_GET ? FN_GETTER : FN_SETTER;
    parser_next(p);
  }
  Snap snap;
  parser_snap(p, &snap);
  PKey key;
  parse_property_key(p, &key, true);
  const GLECMA_Lexer after_key = p->lex;
  const GLECMA_Token token = p->tok;
  const size_t end_of_key = p->prev_end;
  parser_restore(p, &snap);
  p->lex = after_key;
  p->tok = token;
  p->prev_end = end_of_key;
  if (p->tok.kind != GLECMA_TOK_LPAREN) {
    parser_unexpected(p);
  }
  uint32_t kind = fn | FN_METHOD;
  if (constructor) {
    kind |= FN_CONSTRUCTOR | (derived ? FN_DERIVED : 0u);
  }
  return parse_method(p, start, kind).node;
}

// A static block read again from `static` to its closing brace.
GLECMA_BNode parse_static_block_range(Parser * p) {
  const size_t start = p->tok.start.offset;
  const size_t mark = p->kids_len;
  if (!parser_is_kw(p, GLECMA_KW_STATIC)) {
    parser_unexpected(p);
  }
  parser_next(p);
  if (p->tok.kind != GLECMA_TOK_LBRACE) {
    parser_unexpected(p);
  }
  parse_static_block(p, start);
  GLECMA_BNode node = p->kids[p->kids_len - 1];
  p->kids_len = mark;
  return node;
}

static void parse_class_member(Parser * p, bool derived, bool * has_constructor) {
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
  const bool is_method = p->tok.kind == GLECMA_TOK_LPAREN;
  // What the element is called (clause 15.7.1): a string names it as a word
  // does, and a computed name or a private one is neither.
  const bool plain_name = !key.computed && !key.is_private &&
      (key.tok.kind == GLECMA_TOK_NAME || key.tok.kind == GLECMA_TOK_STRING);
  const bool named_constructor = plain_name && token_value_is(p, &key.tok, "constructor");
  if (is_static && plain_name && token_value_is(p, &key.tok, "prototype")) {
    parser_fail(p, GLECMA_RULE_CLASS_STATIC_PROTOTYPE, &key.tok.start);
  }
  if (key.is_private) {
    if (token_value_is(p, &key.tok, "constructor")) {
      parser_fail(p, GLECMA_RULE_PRIVATE_NAME_CONSTRUCTOR, &key.tok.start);
    }
    GLECMA_BText name;
    parser_text(p, &key.tok, GLECMA_VALUE_COOKED, true, &name);
    early_private_declare(p, &name, &key.tok.start,
        !is_method ? PRIV_FIELD : (fn & FN_GETTER) != 0 ? PRIV_GETTER
            : (fn & FN_SETTER) != 0 ? PRIV_SETTER : PRIV_METHOD,
        is_static);
  }
  if (is_method) {
    uint32_t kind = fn | FN_METHOD;
    if (!is_static && named_constructor) {
      if ((fn & (FN_GETTER | FN_SETTER | FN_GENERATOR | FN_ASYNC)) != 0) {
        parser_fail(p, GLECMA_RULE_CLASS_CONSTRUCTOR_KIND, &key.tok.start);
      }
      if (*has_constructor) {
        parser_fail(p, GLECMA_RULE_CLASS_CONSTRUCTOR_DUPLICATE, &key.tok.start);
      }
      *has_constructor = true;
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
  // A field, which is never called `constructor`.
  if (named_constructor) {
    parser_fail(p, GLECMA_RULE_CLASS_CONSTRUCTOR_KIND, &key.tok.start);
  }
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
    p->flags = (saved & (PF_STRICT | PF_BUILTIN | PF_MODULE)) | PF_SUPER_PROP |
        PF_NEW_TARGET | PF_NO_ARGUMENTS |
        ((saved & PF_MODULE) != 0 ? PF_AWAIT_RESERVED : 0u);
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

static PExpr parse_class(Parser * p, bool is_declaration, bool allow_anonymous) {
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
  } else if (is_declaration && !allow_anonymous) {
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
    // A level of nesting: `class A extends class extends ...` recurses
    // through here and through no other counted function.
    parser_enter(p);
    // The heritage is a LeftHandSideExpression read on its own, so a pending
    // cover-grammar error in it (`extends { a = 1 }`) is its to rule on.
    const size_t outer_pending = p->pending;
    const GLECMA_Location outer_at = p->pending_at;
    const uint32_t outer_rule = p->pending_rule;
    p->pending = NO_PENDING;
    heritage = parse_lhs(p).node;
    if (p->pending != NO_PENDING) {
      parser_fail(p, p->pending_rule, &p->pending_at);
    }
    p->pending = outer_pending;
    p->pending_at = outer_at;
    p->pending_rule = outer_rule;
    parser_leave(p);
  }
  if (p->tok.kind != GLECMA_TOK_LBRACE) {
    parser_unexpected(p);
  }
  size_t body_start = p->tok.start.offset;
  size_t mark = p->kids_len;
  parser_next(p);
  const bool saved_in = p->no_in;
  p->no_in = false;
  early_class_enter(p);
  bool has_constructor = false;
  while (p->tok.kind != GLECMA_TOK_RBRACE) {
    if (parser_eat(p, GLECMA_TOK_SEMICOLON)) {
      continue;
    }
    parse_class_member(p, derived, &has_constructor);
  }
  early_class_leave(p);
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
  e.ref_start = start;
  e.shape = SH_FUNCTION;
  e.paren = 0;
  e.aux = 0;
  p->flags = saved;
  return e;
}

PExpr parse_class_declaration(Parser * p, bool allow_anonymous) {
  return parse_class(p, true, allow_anonymous);
}

PExpr parse_class_expression(Parser * p) {
  return parse_class(p, false, false);
}

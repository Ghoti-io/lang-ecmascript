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
 * Expressions (ECMA-262 clause 13), from the comma operator down to the
 * primary expressions, with the cover grammars.
 *
 * Two grammars are covered here, and neither is resolved by changing a node:
 *
 *  - `(` ... `)` may be a parenthesized expression or arrow parameters, and
 *    `async (` ... `)` a call or async arrow parameters. The group is parsed
 *    as an expression; if `=>` follows, the builder is rewound to the group's
 *    start and the group is parsed again as parameters, and the offset is
 *    remembered so that an enclosing group's second pass takes the decision
 *    without making it again (a chain of nested arrow parameters then costs
 *    quadratic work, bounded by the nesting cap, and not exponential).
 *  - An array or object literal that is followed by `=` is an assignment
 *    pattern: the builder is rewound to the literal's start and it is parsed
 *    again as a pattern.
 *
 * What an object literal may hold that an expression may not, a
 * CoverInitializedName (`{a = 1}`), is recorded as pending and is an error
 * unless the literal turns out to be a pattern.
 */

#include <ghoti.io/lang-ecmascript/macros.h>

#include "parse/parser.h"

#include <string.h>

/* ---- Small helpers --------------------------------------------------------- */

static PExpr expr_of(GLECMA_BNode node, size_t start, size_t end,
    uint8_t shape) {
  PExpr e;
  e.node = node;
  e.start = start;
  e.end = end;
  e.ref_start = start;
  e.shape = shape;
  e.paren = 0;
  e.aux = 0;
  return e;
}

static void emit_reference(Parser * p, const GLECMA_BText * name, size_t start,
    size_t end, uint32_t what) {
  parser_event(p, GLECMA_BEV_REFERENCE, what, name, start, end);
}

bool token_is_ascii(const Parser * p, const GLECMA_Token * token,
    const char * ascii) {
  size_t length = strlen(ascii);
  size_t begin = token->start.offset;
  if (token->kind == GLECMA_TOK_STRING) {
    // A string's value is compared as written: an escape is a different
    // spelling, and the callers that ask want a name spelled plainly.
    begin++;
    if (token->end.offset - begin - 1 != length) {
      return false;
    }
  } else if (token->end.offset - begin != length) {
    return false;
  }
  for (size_t i = 0; i < length; i++) {
    if (glecma_lex_unit(&p->text, begin + i) != (uint32_t)(unsigned char)ascii[i]) {
      return false;
    }
  }
  return true;
}

// Identifier reference: the name token is checked against the context and the
// node and event are made. The token is not consumed.
static PExpr identifier_node(Parser * p, GLECMA_BText * name) {
  uint32_t rule = glecma_token_identifier_check(&p->tok, parser_idf(p));
  if (rule != GLECMA_RULE_NONE) {
    parser_fail_token(p, rule);
  }
  parser_text(p, &p->tok, GLECMA_VALUE_COOKED, true, name);
  GLECMA_BNode node = parser_make_text(p, GLECMA_NODE_IDENTIFIER, 0,
      p->tok.start.offset, p->tok.end.offset, name, NULL);
  PExpr e = expr_of(node, p->tok.start.offset, p->tok.end.offset, SH_IDENT);
  if (p->tok.keyword == GLECMA_KW_EVAL) {
    e.aux |= PA_EVAL;
  }
  if (p->tok.keyword == GLECMA_KW_ARGUMENTS) {
    early_check_arguments(p, &p->tok);
    e.aux |= PA_ARGUMENTS;
  }
  if (parser_is_kw(p, GLECMA_KW_ASYNC)) {
    e.aux |= PA_ASYNC;
  }
  return e;
}

PExpr parse_ident_ref(Parser * p) {
  GLECMA_BText name;
  PExpr e = identifier_node(p, &name);
  emit_reference(p, &name, e.start, e.end, GLECMA_BREF_READ);
  parser_next(p);
  return e;
}

// The name after a `.`: any IdentifierName, and no reference.
static GLECMA_BNode property_name_node(Parser * p) {
  GLECMA_BText name;
  parser_text(p, &p->tok, GLECMA_VALUE_COOKED, true, &name);
  return parser_make_text(p, GLECMA_NODE_IDENTIFIER, 0, p->tok.start.offset,
      p->tok.end.offset, &name, NULL);
}

// The name of a private member in the class that declares it: a node, and no reference.
static GLECMA_BNode private_declaration_node(Parser * p) {
  GLECMA_BText name;
  parser_text(p, &p->tok, GLECMA_VALUE_COOKED, true, &name);
  return parser_make_text(p, GLECMA_NODE_PRIVATE_NAME, 0, p->tok.start.offset,
      p->tok.end.offset, &name, NULL);
}

// A use of a private name: the node, and the name recorded so that the class
// it belongs to can answer for it (clause 15.7.1).
static GLECMA_BNode private_name_node(Parser * p) {
  GLECMA_BText name;
  parser_text(p, &p->tok, GLECMA_VALUE_COOKED, true, &name);
  early_private_reference(p, &name, &p->tok.start);
  return parser_make_text(p, GLECMA_NODE_PRIVATE_NAME, 0, p->tok.start.offset,
      p->tok.end.offset, &name, NULL);
}

bool expr_valid_target(const Parser * p, const PExpr * e, bool allow_call) {
  switch (e->shape) {
    case SH_IDENT:
      // `eval` and `arguments` are not assignable in strict code (clause 13.15.1).
      return (p->flags & PF_STRICT) == 0 ||
          (e->aux & (PA_EVAL | PA_ARGUMENTS)) == 0;
    case SH_MEMBER:
      return true;
    case SH_CALL:
      // Annex B.3.9: in sloppy code a call is a target that throws when it is
      // reached (the logical assignments and the destructuring targets do not
      // take it, and the caller says so).
      return allow_call && (p->flags & PF_STRICT) == 0;
    default:
      return false;
  }
}

void expr_mark_write(Parser * p, const PExpr * e, bool also_read) {
  if (e->shape == SH_IDENT) {
    parser_amend_reference(p, e->ref_start, GLECMA_BREF_WRITE,
        also_read ? 0u : GLECMA_BREF_READ);
  }
}

/* ---- Literals --------------------------------------------------------------- */

static uint32_t literal_flags(const GLECMA_Token * t) {
  uint32_t flags = 0;
  if ((t->flags & GLECMA_TOKF_LEGACY_OCTAL) != 0) {
    flags |= GLECMA_NODEF_LEGACY_OCTAL;
  }
  if ((t->flags & GLECMA_TOKF_NON_OCTAL_DECIMAL) != 0) {
    flags |= GLECMA_NODEF_NON_OCTAL_DECIMAL;
  }
  return flags;
}

static PExpr string_literal(Parser * p) {
  GLECMA_BText value = {NULL, 0};
  GLECMA_BText * text = NULL;
  if (parser_materializing(p)) {
    parser_text(p, &p->tok, GLECMA_VALUE_COOKED, false, &value);
    text = &value;
  }
  GLECMA_BNode node = parser_make_text(p, GLECMA_NODE_STRING_LITERAL,
      literal_flags(&p->tok), p->tok.start.offset, p->tok.end.offset, text,
      NULL);
  PExpr e = expr_of(node, p->tok.start.offset, p->tok.end.offset, SH_STRING);
  parser_next(p);
  return e;
}

static PExpr number_literal(Parser * p, uint32_t kind) {
  GLECMA_BNode node = parser_make(p, kind, literal_flags(&p->tok), 0,
      p->tok.start.offset, p->tok.end.offset, p->kids_len);
  PExpr e = expr_of(node, p->tok.start.offset, p->tok.end.offset, SH_OTHER);
  parser_next(p);
  return e;
}

static PExpr regexp_literal(Parser * p) {
  parser_rescan(p, GLECMA_GOAL_REGEXP);
  if (p->tok.kind != GLECMA_TOK_REGEXP) {
    parser_unexpected(p);
  }
  // Clause 13.2.7.1: an early error whichever builder is running.
  parse_validate_regexp(p, &p->tok);
  GLECMA_BText pattern = {NULL, 0};
  GLECMA_BText flags = {NULL, 0};
  if (parser_materializing(p)) {
    parser_slice(p, p->tok.start.offset + 1, p->tok.regexp_flags_offset - 1,
        &pattern);
    parser_slice(p, p->tok.regexp_flags_offset, p->tok.end.offset, &flags);
  }
  GLECMA_BNode node = parser_make_text(p, GLECMA_NODE_REGEXP_LITERAL, 0,
      p->tok.start.offset, p->tok.end.offset, &pattern, &flags);
  PExpr e = expr_of(node, p->tok.start.offset, p->tok.end.offset, SH_OTHER);
  parser_next(p);
  return e;
}

PExpr parse_template(Parser * p, bool tagged) {
  size_t start = p->tok.start.offset;
  size_t mark = p->kids_len;
  size_t end = start;
  for (;;) {
    GLECMA_Token t = p->tok;
    if (t.kind != GLECMA_TOK_TEMPLATE) {
      parser_unexpected(p);
    }
    uint32_t flags = 0;
    GLECMA_BText cooked = {NULL, 0};
    GLECMA_BText raw = {NULL, 0};
    if ((t.flags & GLECMA_TOKF_COOKED_UNDEFINED) != 0) {
      if (!tagged) {
        // Only now is it known that nothing tags the template.
        parser_fail(p, t.escape_rule, &t.escape_location);
      }
      flags |= GLECMA_NODEF_COOKED_UNDEFINED;
    }
    if (parser_materializing(p)) {
      if ((flags & GLECMA_NODEF_COOKED_UNDEFINED) == 0) {
        parser_text(p, &t, GLECMA_VALUE_COOKED, false, &cooked);
      }
      parser_text(p, &t, GLECMA_VALUE_RAW, false, &raw);
    }
    const bool opens = t.template_part == GLECMA_TEMPLATE_HEAD ||
        t.template_part == GLECMA_TEMPLATE_MIDDLE;
    GLECMA_BNode element = parser_make_text(p, GLECMA_NODE_TEMPLATE_ELEMENT,
        flags, t.start.offset + 1, t.end.offset - (opens ? 2u : 1u), &cooked,
        &raw);
    parser_kid(p, element);
    if (!opens) {
      end = t.end.offset;
      parser_next(p);
      break;
    }
    parser_next(p);
    bool saved_in = p->no_in;
    p->no_in = false;
    PExpr substitution = parse_expression(p);
    p->no_in = saved_in;
    parser_kid(p, substitution.node);
    if (p->tok.kind != GLECMA_TOK_RBRACE) {
      parser_unexpected(p);
    }
    parser_rescan(p, GLECMA_GOAL_TEMPLATE_TAIL);
  }
  GLECMA_BNode node = parser_make(p, GLECMA_NODE_TEMPLATE_LITERAL, 0, 0, start,
      end, mark);
  return expr_of(node, start, end, SH_OTHER);
}

/* ---- Arguments ---------------------------------------------------------------- */

void parse_arguments(Parser * p, size_t * out_end) {
  parser_expect(p, GLECMA_TOK_LPAREN);
  bool saved_in = p->no_in;
  p->no_in = false;
  while (p->tok.kind != GLECMA_TOK_RPAREN) {
    if (p->tok.kind == GLECMA_TOK_ELLIPSIS) {
      size_t start = p->tok.start.offset;
      parser_next(p);
      PExpr arg = parse_assignment(p, 0);
      size_t mark = p->kids_len;
      parser_kid(p, arg.node);
      GLECMA_BNode spread = parser_make(p, GLECMA_NODE_SPREAD_ELEMENT, 0, 0,
          start, arg.end, mark);
      parser_kid(p, spread);
    } else {
      PExpr arg = parse_assignment(p, 0);
      parser_kid(p, arg.node);
    }
    if (p->tok.kind != GLECMA_TOK_RPAREN) {
      parser_expect(p, GLECMA_TOK_COMMA);
    }
  }
  p->no_in = saved_in;
  *out_end = p->tok.end.offset;
  parser_next(p);
}

/* ---- Property names, array and object literals ---------------------------------- */

void parse_property_key(Parser * p, PKey * key, bool allow_private) {
  memset(key, 0, sizeof(*key));
  key->tok = p->tok;
  key->start = p->tok.start.offset;
  key->end = p->tok.end.offset;
  switch (p->tok.kind) {
    case GLECMA_TOK_NAME:
      key->is_name = true;
      key->node = property_name_node(p);
      parser_next(p);
      return;
    case GLECMA_TOK_STRING: {
      PExpr s = string_literal(p);
      key->node = s.node;
      return;
    }
    case GLECMA_TOK_NUMBER:
      key->node = number_literal(p, GLECMA_NODE_NUMBER_LITERAL).node;
      return;
    case GLECMA_TOK_BIGINT:
      key->node = number_literal(p, GLECMA_NODE_BIGINT_LITERAL).node;
      return;
    case GLECMA_TOK_PRIVATE_NAME:
      if (!allow_private) {
        parser_unexpected(p);
      }
      key->is_private = true;
      key->node = private_declaration_node(p);
      parser_next(p);
      return;
    case GLECMA_TOK_LBRACKET: {
      key->computed = true;
      parser_next(p);
      bool saved_in = p->no_in;
      p->no_in = false;
      PExpr e = parse_assignment(p, 0);
      p->no_in = saved_in;
      key->node = e.node;
      key->end = p->tok.end.offset;
      parser_expect(p, GLECMA_TOK_RBRACKET);
      return;
    }
    default:
      parser_unexpected(p);
  }
}

static PExpr parse_array_literal(Parser * p) {
  size_t start = p->tok.start.offset;
  size_t mark = p->kids_len;
  parser_next(p);
  bool saved_in = p->no_in;
  p->no_in = false;
  while (p->tok.kind != GLECMA_TOK_RBRACKET) {
    if (p->tok.kind == GLECMA_TOK_COMMA) {
      parser_kid(p, NULL);
      parser_next(p);
      continue;
    }
    if (p->tok.kind == GLECMA_TOK_ELLIPSIS) {
      size_t spread_start = p->tok.start.offset;
      parser_next(p);
      PExpr arg = parse_assignment(p, EF_COVER);
      size_t inner = p->kids_len;
      parser_kid(p, arg.node);
      GLECMA_BNode spread = parser_make(p, GLECMA_NODE_SPREAD_ELEMENT, 0, 0,
          spread_start, arg.end, inner);
      parser_kid(p, spread);
    } else {
      PExpr element = parse_assignment(p, EF_COVER);
      parser_kid(p, element.node);
    }
    if (p->tok.kind != GLECMA_TOK_RBRACKET) {
      parser_expect(p, GLECMA_TOK_COMMA);
    }
  }
  p->no_in = saved_in;
  size_t end = p->tok.end.offset;
  parser_next(p);
  GLECMA_BNode node = parser_make(p, GLECMA_NODE_ARRAY_LITERAL, 0, 0, start,
      end, mark);
  return expr_of(node, start, end, SH_ARRAY);
}

// Whether the token after a `get`, `set` or `async` makes it a name and not a
// modifier: the word is then the property's name itself.
static bool modifier_is_name(const GLECMA_Token * after, bool need_same_line) {
  switch (after->kind) {
    case GLECMA_TOK_COMMA: case GLECMA_TOK_COLON: case GLECMA_TOK_LPAREN:
    case GLECMA_TOK_RBRACE: case GLECMA_TOK_ASSIGN: case GLECMA_TOK_SEMICOLON:
    case GLECMA_TOK_EOF:
      return true;
    default:
      return need_same_line && (after->flags & GLECMA_TOKF_NEWLINE_BEFORE) != 0;
  }
}

static void parse_object_property(Parser * p, bool * proto_seen) {
  size_t property_start = p->tok.start.offset;
  if (p->tok.kind == GLECMA_TOK_ELLIPSIS) {
    parser_next(p);
    PExpr arg = parse_assignment(p, EF_COVER);
    size_t mark = p->kids_len;
    parser_kid(p, arg.node);
    GLECMA_BNode spread = parser_make(p, GLECMA_NODE_SPREAD_ELEMENT, 0, 0,
        property_start, arg.end, mark);
    parser_kid(p, spread);
    return;
  }
  uint32_t fn = 0;
  GLECMA_Token after;
  if (parser_is_kw(p, GLECMA_KW_ASYNC) && parser_peek(p, &after) &&
      !modifier_is_name(&after, true)) {
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
  parse_property_key(p, &key, false);
  uint32_t flags = key.computed ? GLECMA_NODEF_COMPUTED : 0u;
  size_t mark = p->kids_len;
  if (fn != 0 || p->tok.kind == GLECMA_TOK_LPAREN) {
    if (fn == 0) {
      fn = FN_METHOD;
    }
    parser_kid(p, key.node);
    PExpr function = parse_method(p, property_start, fn | FN_METHOD);
    parser_kid(p, function.node);
    flags |= (fn & FN_GETTER) ? GLECMA_NODEF_GETTER
           : (fn & FN_SETTER) ? GLECMA_NODEF_SETTER : GLECMA_NODEF_METHOD;
    GLECMA_BNode property = parser_make(p, GLECMA_NODE_PROPERTY, flags, 0,
        property_start, function.end, mark);
    p->kids_len = mark;
    parser_kid(p, property);
    return;
  }
  if (p->tok.kind == GLECMA_TOK_COLON) {
    // A second `__proto__: value` is an error unless the literal turns out to
    // be a pattern (clause 13.2.5.1), which is the question `pending` answers.
    if (!key.computed && (key.tok.kind == GLECMA_TOK_NAME ||
            key.tok.kind == GLECMA_TOK_STRING) &&
        token_value_is(p, &key.tok, "__proto__")) {
      if (*proto_seen && p->pending == NO_PENDING) {
        p->pending = key.start;
        p->pending_at = key.tok.start;
        p->pending_rule = GLECMA_RULE_DUPLICATE_PROTO;
      }
      *proto_seen = true;
    }
    parser_next(p);
    PExpr value = parse_assignment(p, EF_COVER);
    parser_kid(p, key.node);
    parser_kid(p, value.node);
    GLECMA_BNode property = parser_make(p, GLECMA_NODE_PROPERTY, flags, 0,
        property_start, value.end, mark);
    parser_kid(p, property);
    return;
  }
  // A shorthand property: `{a}`, or the cover form `{a = 1}`.
  if (key.computed || !key.is_name) {
    parser_unexpected(p);
  }
  uint32_t rule = glecma_token_identifier_check(&key.tok, parser_idf(p));
  if (rule != GLECMA_RULE_NONE) {
    parser_fail(p, rule, &key.tok.start);
  }
  GLECMA_BText name;
  parser_text(p, &key.tok, GLECMA_VALUE_COOKED, true, &name);
  if (key.tok.keyword == GLECMA_KW_ARGUMENTS) {
    early_check_arguments(p, &key.tok);
  }
  emit_reference(p, &name, key.start, key.end, GLECMA_BREF_READ);
  size_t end = key.end;
  if (p->tok.kind == GLECMA_TOK_ASSIGN) {
    if (p->pending == NO_PENDING) {
      p->pending = p->tok.start.offset;
      p->pending_at = p->tok.start;
      p->pending_rule = GLECMA_RULE_COVER_INITIALIZED_NAME;
    }
    parser_next(p);
    PExpr initial = parse_assignment(p, 0);
    end = initial.end;
  }
  parser_kid(p, key.node);
  parser_kid(p, parser_make_text(p, GLECMA_NODE_IDENTIFIER, 0, key.start,
      key.end, &name, NULL));
  GLECMA_BNode property = parser_make(p, GLECMA_NODE_PROPERTY,
      flags | GLECMA_NODEF_SHORTHAND, 0, property_start, end, mark);
  parser_kid(p, property);
}

static PExpr parse_object_literal(Parser * p) {
  size_t start = p->tok.start.offset;
  size_t mark = p->kids_len;
  parser_next(p);
  bool saved_in = p->no_in;
  p->no_in = false;
  bool proto_seen = false;
  while (p->tok.kind != GLECMA_TOK_RBRACE) {
    parse_object_property(p, &proto_seen);
    if (p->tok.kind != GLECMA_TOK_RBRACE) {
      parser_expect(p, GLECMA_TOK_COMMA);
    }
  }
  p->no_in = saved_in;
  size_t end = p->tok.end.offset;
  parser_next(p);
  GLECMA_BNode node = parser_make(p, GLECMA_NODE_OBJECT_LITERAL, 0, 0, start,
      end, mark);
  return expr_of(node, start, end, SH_OBJECT);
}

/* ---- Arrow functions -------------------------------------------------------------- */

static PExpr finish_arrow(Parser * p, size_t start, bool is_async,
    GLECMA_BNode params, uint32_t saved_flags, bool saved_no_in) {
  if (p->tok.kind != GLECMA_TOK_ARROW) {
    parser_unexpected(p);
  }
  if ((p->tok.flags & GLECMA_TOKF_NEWLINE_BEFORE) != 0) {
    parser_fail_token(p, GLECMA_RULE_ARROW_LINE_TERMINATOR);
  }
  parser_next(p);
  // The body is the arrow's own code: it may `return`, it reads no `yield` or
  // `await` of the code around it, and it takes the rest of the context from
  // there (`super`, `new.target`, `arguments`).
  p->flags = (saved_flags & ~(PF_YIELD | PF_AWAIT | PF_AWAIT_RESERVED | PF_PARAMS)) |
      PF_RETURN | (is_async || (saved_flags & PF_MODULE) != 0
                       ? (is_async ? (PF_AWAIT | PF_AWAIT_RESERVED) : PF_AWAIT_RESERVED)
                       : 0u);
  const bool saved_simple = p->body_simple;
  const bool simple = p->params_simple;
  const JumpCtx jumps = early_jump_enter(p);
  p->body_simple = simple;
  bool body_strict = (saved_flags & PF_STRICT) != 0;
  uint32_t flags = is_async ? GLECMA_NODEF_ASYNC : 0u;
  size_t end;
  GLECMA_BNode body;
  if (p->tok.kind == GLECMA_TOK_LBRACE) {
    p->no_in = false;
    body = parse_function_body(p, &end, &body_strict);
  } else {
    p->no_in = saved_no_in;
    GLECMA_Result r = p->builder->ops->body_begin(p->builder);
    if (r != GLECMA_OK) {
      parser_fail_result(p, r);
    }
    PExpr e = parse_assignment(p, 0);
    size_t mark = p->kids_len;
    parser_kid(p, e.node);
    body = parser_make_body(p, 0, 0, e.start, e.end, mark);
    end = e.end;
    flags |= GLECMA_NODEF_EXPRESSION_BODY;
  }
  early_scope_function(p, true, simple, body_strict);
  early_jump_leave(p, jumps);
  p->body_simple = saved_simple;
  p->flags = saved_flags;
  p->no_in = saved_no_in;
  parser_event(p, GLECMA_BEV_SCOPE_LEAVE, GLECMA_BSCOPE_ARROW, NULL, start, end);
  size_t mark = p->kids_len;
  parser_kid(p, NULL);
  parser_kid(p, params);
  parser_kid(p, body);
  GLECMA_BNode node = parser_make(p, GLECMA_NODE_ARROW_FUNCTION, flags, 0,
      start, end, mark);
  return expr_of(node, start, end, SH_ARROW);
}

PExpr parse_arrow_paren(Parser * p, size_t start, bool is_async) {
  const Retro outer = parser_retro_enter(p);
  uint32_t saved_flags = p->flags;
  bool saved_no_in = p->no_in;
  parser_event(p, GLECMA_BEV_SCOPE_ENTER, GLECMA_BSCOPE_ARROW, NULL, start,
      start);
  if (is_async) {
    // `yield` stays what the code around makes it, so that a yield expression
    // in the head of an async arrow inside a generator is found (clause 15.9.1).
    p->flags = saved_flags | PF_AWAIT | PF_AWAIT_RESERVED;
  }
  // No `yield` or `await` expression among the parameters (clauses 15.3.1, 15.9.1).
  p->flags |= PF_PARAMS;
  p->no_in = false;
  size_t params_end = 0;
  GLECMA_BNode params = parse_function_params(p, GLECMA_BDECL_PARAM, 0,
      &params_end);
  p->flags = saved_flags;
  p->no_in = saved_no_in;
  PExpr arrow = finish_arrow(p, start, is_async, params, saved_flags,
      saved_no_in);
  parser_retro_leave(p, outer);
  return arrow;
}

PExpr parse_arrow_ident(Parser * p, size_t start, bool is_async,
    const GLECMA_Token * ident, const GLECMA_BText * name) {
  const Retro outer = parser_retro_enter(p);
  parser_note_binding(p, ident);
  p->params_simple = true;
  uint32_t saved_flags = p->flags;
  bool saved_no_in = p->no_in;
  parser_event(p, GLECMA_BEV_SCOPE_ENTER, GLECMA_BSCOPE_ARROW, NULL, start,
      start);
  parser_event(p, GLECMA_BEV_DECLARE, GLECMA_BDECL_PARAM, name,
      ident->start.offset, ident->end.offset);
  GLECMA_BNode id = parser_make_text(p, GLECMA_NODE_IDENTIFIER, 0,
      ident->start.offset, ident->end.offset, name, NULL);
  size_t mark = p->kids_len;
  parser_kid(p, id);
  GLECMA_BNode params = parser_make(p, GLECMA_NODE_PARAMETER_LIST, 0, 0,
      ident->start.offset, ident->end.offset, mark);
  PExpr arrow = finish_arrow(p, start, is_async, params, saved_flags,
      saved_no_in);
  parser_retro_leave(p, outer);
  return arrow;
}

/* ---- Parentheses ---------------------------------------------------------------- */

// `(` ... `)`: a parenthesized expression or arrow parameters. When it is
// arrow parameters, `*arrow` is set and the parse is back at the `(`: the
// caller parses the arrow function, so that this frame, which holds a
// snapshot, is not on the stack under every level of nested arrow bodies.
PARSER_NOINLINE static PExpr parse_paren(Parser * p, bool * arrow) {
  const size_t open = p->tok.start.offset;
  const bool can_arrow = p->assign_start == open;
  if (can_arrow && parser_memo_has(p, MEMO_ARROW(open))) {
    *arrow = true;
    return expr_of(NULL, open, open, SH_OTHER);
  }
  Snap snap;
  parser_snap(p, &snap);
  parser_next(p);
  bool saved_in = p->no_in;
  p->no_in = false;
  size_t mark = p->kids_len;
  GLECMA_Location rest_at = p->tok.start;
  bool saw_rest = false;
  bool saw_trailing = false;
  GLECMA_Location trailing_at = p->tok.start;
  size_t first_start = 0, last_end = 0;
  size_t count = 0;
  GLECMA_BNode only = NULL;
  PExpr only_expr = expr_of(NULL, 0, 0, SH_OTHER);
  if (p->tok.kind == GLECMA_TOK_RPAREN) {
    // `()` is arrow parameters or nothing.
    parser_next(p);
    p->no_in = saved_in;
    if (p->tok.kind == GLECMA_TOK_ARROW && can_arrow) {
      parser_restore(p, &snap);
      *arrow = true;
      return expr_of(NULL, open, open, SH_OTHER);
    }
    parser_fail(p, GLECMA_RULE_UNEXPECTED_TOKEN, &snap.tok.start);
  }
  for (;;) {
    if (p->tok.kind == GLECMA_TOK_ELLIPSIS) {
      saw_rest = true;
      rest_at = p->tok.start;
      parser_next(p);
      PExpr arg = parse_assignment(p, EF_COVER);
      (void)arg;
      if (p->tok.kind != GLECMA_TOK_RPAREN) {
        parser_unexpected(p);
      }
      break;
    }
    PExpr item = parse_assignment(p, EF_COVER);
    if (count == 0) {
      first_start = item.start;
      only = item.node;
      only_expr = item;
    }
    last_end = item.end;
    parser_kid(p, item.node);
    count++;
    if (p->tok.kind != GLECMA_TOK_COMMA) {
      break;
    }
    parser_next(p);
    if (p->tok.kind == GLECMA_TOK_RPAREN) {
      saw_trailing = true;
      trailing_at = p->tok.start;
      break;
    }
  }
  p->no_in = saved_in;
  if (p->tok.kind != GLECMA_TOK_RPAREN) {
    parser_unexpected(p);
  }
  parser_next(p);
  const size_t close_end = p->prev_end;
  if (p->tok.kind == GLECMA_TOK_ARROW && can_arrow) {
    parser_memo_add(p, MEMO_ARROW(open));
    parser_restore(p, &snap);
    *arrow = true;
    return expr_of(NULL, open, open, SH_OTHER);
  }
  if (saw_rest) {
    parser_fail(p, GLECMA_RULE_UNEXPECTED_TOKEN, &rest_at);
  }
  if (saw_trailing) {
    parser_fail(p, GLECMA_RULE_UNEXPECTED_TOKEN, &trailing_at);
  }
  if (count == 1) {
    p->kids_len = mark;
    PExpr e = only_expr;
    e.node = only;
    e.paren = 1;
    // The node keeps the inner range; whatever holds it takes the
    // parentheses with it.
    e.start = open;
    e.end = close_end;
    // What the parentheses leave visible: the names a target check reads and
    // the private member `delete` refuses.
    e.aux = (uint8_t)(e.aux & (PA_EVAL | PA_ARGUMENTS | PA_PRIVATE));
    if (e.shape == SH_ARROW) {
      // A parenthesized arrow function is an ordinary operand.
      e.shape = SH_FUNCTION;
    }
    return e;
  }
  GLECMA_BNode node = parser_make(p, GLECMA_NODE_SEQUENCE_EXPRESSION, 0, 0,
      first_start, last_end, mark);
  PExpr e = expr_of(node, open, close_end, SH_OTHER);
  e.paren = 1;
  return e;
}

// `async (` ... `)`: a call of a function named async, or async arrow
// parameters. The token is `async`.
PARSER_NOINLINE static PExpr parse_async_paren(Parser * p, bool * arrow) {
  const size_t start = p->tok.start.offset;
  if (parser_memo_has(p, MEMO_ARROW(start))) {
    parser_next(p);
    *arrow = true;
    return expr_of(NULL, start, start, SH_OTHER);
  }
  Snap snap;
  parser_snap(p, &snap);
  GLECMA_Token async_token = p->tok;
  GLECMA_BText name;
  PExpr callee = identifier_node(p, &name);
  emit_reference(p, &name, callee.start, callee.end, GLECMA_BREF_READ);
  parser_next(p);
  size_t mark = p->kids_len;
  parser_kid(p, callee.node);
  parser_next(p); // the `(`
  bool saved_in = p->no_in;
  p->no_in = false;
  while (p->tok.kind != GLECMA_TOK_RPAREN) {
    if (p->tok.kind == GLECMA_TOK_ELLIPSIS) {
      size_t spread_start = p->tok.start.offset;
      parser_next(p);
      PExpr arg = parse_assignment(p, EF_COVER);
      size_t inner = p->kids_len;
      parser_kid(p, arg.node);
      GLECMA_BNode spread = parser_make(p, GLECMA_NODE_SPREAD_ELEMENT, 0, 0,
          spread_start, arg.end, inner);
      parser_kid(p, spread);
    } else {
      PExpr arg = parse_assignment(p, EF_COVER);
      parser_kid(p, arg.node);
    }
    if (p->tok.kind != GLECMA_TOK_RPAREN) {
      parser_expect(p, GLECMA_TOK_COMMA);
    }
  }
  p->no_in = saved_in;
  size_t end = p->tok.end.offset;
  parser_next(p);
  if (p->tok.kind == GLECMA_TOK_ARROW) {
    if ((p->tok.flags & GLECMA_TOKF_NEWLINE_BEFORE) != 0) {
      parser_fail_token(p, GLECMA_RULE_ARROW_LINE_TERMINATOR);
    }
    parser_memo_add(p, MEMO_ARROW(start));
    parser_restore(p, &snap);
    parser_next(p);
    *arrow = true;
    return expr_of(NULL, start, start, SH_OTHER);
  }
  if (p->pending != NO_PENDING) {
    parser_fail(p, p->pending_rule, &p->pending_at);
  }
  (void)async_token;
  GLECMA_BNode node = parser_make(p, GLECMA_NODE_CALL_EXPRESSION, 0, 0, start,
      end, mark);
  return expr_of(node, start, end, SH_CALL);
}

/* ---- Primary expressions ---------------------------------------------------------- */

// `async` at the start of a primary expression: an async function, an async
// arrow function or a call of a function named async, or none of them. Kept
// out of parse_primary_name so that the frame of the common identifier does not
// carry the tokens this looks at.
PARSER_NOINLINE static bool parse_async_primary(Parser * p, PExpr * out) {
  const size_t start = p->tok.start.offset;
  GLECMA_Token after;
  if (parser_peek(p, &after) &&
      (after.flags & GLECMA_TOKF_NEWLINE_BEFORE) == 0) {
    if (token_is_kw(&after, GLECMA_KW_FUNCTION)) {
      parser_next(p);
      *out = parse_function_expression(p, start, true);
      return true;
    }
    if (p->assign_start == start && after.kind == GLECMA_TOK_LPAREN) {
      bool arrow = false;
      *out = parse_async_paren(p, &arrow);
      if (arrow) {
        *out = parse_arrow_paren(p, start, true);
      }
      return true;
    }
    GLECMA_Token first, second;
    if (p->assign_start == start && after.kind == GLECMA_TOK_NAME &&
        parser_peek2(p, &first, &second) && second.kind == GLECMA_TOK_ARROW &&
        glecma_token_identifier_check(&after, parser_idf(p) | GLECMA_IDF_AWAIT) ==
            GLECMA_RULE_NONE) {
      // `async x =>`.
      parser_next(p);
      GLECMA_Token ident = p->tok;
      GLECMA_BText name;
      uint32_t saved = p->flags;
      p->flags |= PF_AWAIT_RESERVED;
      uint32_t rule = glecma_token_identifier_check(&ident, parser_idf(p));
      p->flags = saved;
      if (rule != GLECMA_RULE_NONE) {
        parser_fail_token(p, rule);
      }
      parser_text(p, &ident, GLECMA_VALUE_COOKED, true, &name);
      parser_next(p);
      *out = parse_arrow_ident(p, start, true, &ident, &name);
      return true;
    }
  }
  return false;
}

static PExpr parse_primary_name(Parser * p) {
  const size_t start = p->tok.start.offset;
  if (parser_is_kw(p, GLECMA_KW_THIS)) {
    GLECMA_BNode node = parser_make(p, GLECMA_NODE_THIS_EXPRESSION, 0, 0, start,
        p->tok.end.offset, p->kids_len);
    parser_event(p, GLECMA_BEV_USE, GLECMA_BUSE_THIS, NULL, start,
        p->tok.end.offset);
    PExpr e = expr_of(node, start, p->tok.end.offset, SH_OTHER);
    parser_next(p);
    return e;
  }
  if (parser_is_kw(p, GLECMA_KW_NULL)) {
    GLECMA_BNode node = parser_make(p, GLECMA_NODE_NULL_LITERAL, 0, 0, start,
        p->tok.end.offset, p->kids_len);
    PExpr e = expr_of(node, start, p->tok.end.offset, SH_OTHER);
    parser_next(p);
    return e;
  }
  if (parser_is_kw(p, GLECMA_KW_TRUE) || parser_is_kw(p, GLECMA_KW_FALSE)) {
    GLECMA_BNode node = parser_make(p, GLECMA_NODE_BOOLEAN_LITERAL,
        p->tok.keyword == GLECMA_KW_TRUE ? GLECMA_NODEF_TRUE : 0u, 0, start,
        p->tok.end.offset, p->kids_len);
    PExpr e = expr_of(node, start, p->tok.end.offset, SH_OTHER);
    parser_next(p);
    return e;
  }
  if (parser_is_kw(p, GLECMA_KW_FUNCTION)) {
    return parse_function_expression(p, start, false);
  }
  if (parser_is_kw(p, GLECMA_KW_CLASS)) {
    return parse_class_expression(p);
  }
  if (parser_is_kw(p, GLECMA_KW_ASYNC)) {
    PExpr async_expr;
    if (parse_async_primary(p, &async_expr)) {
      return async_expr;
    }
  }
  // An identifier reference, or the parameter of `x => ...`.
  GLECMA_Token ident = p->tok;
  GLECMA_BText name;
  PExpr e = identifier_node(p, &name);
  parser_next(p);
  if (p->tok.kind == GLECMA_TOK_ARROW && p->assign_start == start) {
    return parse_arrow_ident(p, start, false, &ident, &name);
  }
  emit_reference(p, &name, e.start, e.end, GLECMA_BREF_READ);
  return e;
}

static PExpr parse_primary(Parser * p) {
  switch (p->tok.kind) {
    case GLECMA_TOK_NAME:
      return parse_primary_name(p);
    case GLECMA_TOK_NUMBER:
      return number_literal(p, GLECMA_NODE_NUMBER_LITERAL);
    case GLECMA_TOK_BIGINT:
      return number_literal(p, GLECMA_NODE_BIGINT_LITERAL);
    case GLECMA_TOK_STRING:
      return string_literal(p);
    case GLECMA_TOK_TEMPLATE:
      return parse_template(p, false);
    case GLECMA_TOK_SLASH:
    case GLECMA_TOK_SLASH_ASSIGN:
      return regexp_literal(p);
    case GLECMA_TOK_PERCENT:
      if ((p->flags & PF_BUILTIN) != 0) {
        parser_rescan(p, GLECMA_GOAL_REGEXP);
        if (p->tok.kind == GLECMA_TOK_INTRINSIC_NAME) {
          GLECMA_BText name;
          parser_text(p, &p->tok, GLECMA_VALUE_COOKED, true, &name);
          GLECMA_BNode node = parser_make_text(p, GLECMA_NODE_INTRINSIC_NAME, 0,
              p->tok.start.offset, p->tok.end.offset, &name, NULL);
          PExpr e = expr_of(node, p->tok.start.offset, p->tok.end.offset,
              SH_OTHER);
          parser_next(p);
          return e;
        }
      }
      parser_unexpected(p);
    case GLECMA_TOK_LPAREN: {
      bool arrow = false;
      const size_t open = p->tok.start.offset;
      PExpr group = parse_paren(p, &arrow);
      return arrow ? parse_arrow_paren(p, open, false) : group;
    }
    case GLECMA_TOK_LBRACKET:
      return parse_array_literal(p);
    case GLECMA_TOK_LBRACE:
      return parse_object_literal(p);
    default:
      parser_unexpected(p);
  }
}

/* ---- Left-hand-side expressions -------------------------------------------------- */

// `new.target`, after `new` and `.` have been read.
static PExpr parse_new_target(Parser * p, size_t start,
    const GLECMA_Location * new_at) {
  if (!parser_is_kw(p, GLECMA_KW_TARGET)) {
    parser_unexpected(p);
  }
  if ((p->flags & PF_NEW_TARGET) == 0) {
    parser_fail(p, GLECMA_RULE_NEW_TARGET_POSITION, new_at);
  }
  size_t end = p->tok.end.offset;
  parser_next(p);
  GLECMA_BNode node = parser_make(p, GLECMA_NODE_META_PROPERTY, 0,
      GLECMA_OP_NEW_TARGET, start, end, p->kids_len);
  parser_event(p, GLECMA_BEV_USE, GLECMA_BUSE_NEW_TARGET, NULL, start, end);
  return expr_of(node, start, end, SH_OTHER);
}

static PExpr member_dot(Parser * p, PExpr object, uint32_t extra_flags,
    bool allow_private) {
  // The token after the `.` or `?.`.
  GLECMA_BNode property;
  bool is_private = false;
  if (p->tok.kind == GLECMA_TOK_NAME) {
    property = property_name_node(p);
  } else if (p->tok.kind == GLECMA_TOK_PRIVATE_NAME && allow_private) {
    property = private_name_node(p);
    is_private = true;
  } else {
    parser_unexpected(p);
  }
  size_t end = p->tok.end.offset;
  size_t mark = p->kids_len;
  parser_kid(p, object.node);
  parser_kid(p, property);
  parser_next(p);
  GLECMA_BNode node = parser_make(p, GLECMA_NODE_MEMBER_EXPRESSION, extra_flags,
      0, object.start, end, mark);
  PExpr member = expr_of(node, object.start, end, SH_MEMBER);
  if (is_private) {
    member.aux |= PA_PRIVATE;
  }
  return member;
}

static PExpr member_bracket(Parser * p, PExpr object, uint32_t extra_flags) {
  // The token after the `[`.
  bool saved_in = p->no_in;
  p->no_in = false;
  PExpr index = parse_expression(p);
  p->no_in = saved_in;
  size_t end = p->tok.end.offset;
  size_t mark = p->kids_len;
  parser_kid(p, object.node);
  parser_kid(p, index.node);
  parser_expect(p, GLECMA_TOK_RBRACKET);
  GLECMA_BNode node = parser_make(p, GLECMA_NODE_MEMBER_EXPRESSION,
      extra_flags | GLECMA_NODEF_COMPUTED, 0, object.start, end, mark);
  return expr_of(node, object.start, end, SH_MEMBER);
}

static PExpr call_of(Parser * p, PExpr callee, uint32_t extra_flags) {
  size_t mark = p->kids_len;
  parser_kid(p, callee.node);
  size_t end = 0;
  parse_arguments(p, &end);
  GLECMA_BNode node = parser_make(p, GLECMA_NODE_CALL_EXPRESSION, extra_flags,
      0, callee.start, end, mark);
  return expr_of(node, callee.start, end, SH_CALL);
}

// Members and calls after a primary, an optional chain among them.
static PExpr parse_tails(Parser * p, PExpr e, bool allow_call) {
  bool chain = false;
  for (;;) {
    switch (p->tok.kind) {
      case GLECMA_TOK_DOT:
        parser_next(p);
        e = member_dot(p, e, 0, true);
        break;
      case GLECMA_TOK_LBRACKET:
        parser_next(p);
        e = member_bracket(p, e, 0);
        break;
      case GLECMA_TOK_QDOT:
        if (!allow_call) {
          parser_fail_token(p, GLECMA_RULE_OPTIONAL_CHAIN_NEW);
        }
        chain = true;
        parser_next(p);
        if (p->tok.kind == GLECMA_TOK_LPAREN) {
          e = call_of(p, e, GLECMA_NODEF_OPTIONAL);
        } else if (p->tok.kind == GLECMA_TOK_LBRACKET) {
          parser_next(p);
          e = member_bracket(p, e, GLECMA_NODEF_OPTIONAL);
        } else if (p->tok.kind == GLECMA_TOK_TEMPLATE) {
          parser_fail_token(p, GLECMA_RULE_OPTIONAL_CHAIN_TEMPLATE);
        } else {
          e = member_dot(p, e, GLECMA_NODEF_OPTIONAL, true);
        }
        e.shape = SH_OPTCHAIN;
        continue;
      case GLECMA_TOK_LPAREN: {
        if (!allow_call) {
          goto done;
        }
        const bool direct_eval = e.shape == SH_IDENT && (e.aux & PA_EVAL) != 0;
        size_t call_start = p->tok.start.offset;
        e = call_of(p, e, 0);
        if (direct_eval) {
          parser_event(p, GLECMA_BEV_USE, GLECMA_BUSE_DIRECT_EVAL, NULL,
              call_start, e.end);
        }
        break;
      }
      case GLECMA_TOK_TEMPLATE: {
        if (chain) {
          parser_fail_token(p, GLECMA_RULE_OPTIONAL_CHAIN_TEMPLATE);
        }
        PExpr quasi = parse_template(p, true);
        size_t mark = p->kids_len;
        parser_kid(p, e.node);
        parser_kid(p, quasi.node);
        GLECMA_BNode node = parser_make(p, GLECMA_NODE_TAGGED_TEMPLATE, 0, 0,
            e.start, quasi.end, mark);
        e = expr_of(node, e.start, quasi.end, SH_OTHER);
        break;
      }
      default:
        goto done;
    }
    if (chain) {
      e.shape = SH_OPTCHAIN;
    }
  }
done:
  if (chain) {
    size_t mark = p->kids_len;
    parser_kid(p, e.node);
    const uint8_t last = (uint8_t)(e.aux & PA_PRIVATE);
    GLECMA_BNode node = parser_make(p, GLECMA_NODE_OPTIONAL_CHAIN, 0, 0,
        e.start, e.end, mark);
    e = expr_of(node, e.start, e.end, SH_OPTCHAIN);
    e.aux = last;
  }
  return e;
}

static PExpr parse_super(Parser * p) {
  size_t start = p->tok.start.offset;
  size_t super_end = p->tok.end.offset;
  GLECMA_BNode super_node = parser_make(p, GLECMA_NODE_SUPER, 0, 0, start,
      super_end, p->kids_len);
  const GLECMA_Location super_at = p->tok.start;
  parser_next(p);
  PExpr s = expr_of(super_node, start, super_end, SH_OTHER);
  switch (p->tok.kind) {
    case GLECMA_TOK_LPAREN: {
      if ((p->flags & PF_SUPER_CALL) == 0) {
        parser_fail(p, GLECMA_RULE_SUPER_CALL_POSITION, &super_at);
      }
      PExpr call = call_of(p, s, 0);
      call.shape = SH_OTHER;
      parser_event(p, GLECMA_BEV_USE, GLECMA_BUSE_SUPER_CALL, NULL, start,
          call.end);
      return call;
    }
    case GLECMA_TOK_DOT:
      if ((p->flags & PF_SUPER_PROP) == 0) {
        parser_fail(p, GLECMA_RULE_SUPER_PROPERTY_POSITION, &super_at);
      }
      parser_next(p);
      parser_event(p, GLECMA_BEV_USE, GLECMA_BUSE_SUPER_PROPERTY, NULL, start,
          super_end);
      return member_dot(p, s, 0, false); // `super.#x` is not a SuperProperty
    case GLECMA_TOK_LBRACKET:
      if ((p->flags & PF_SUPER_PROP) == 0) {
        parser_fail(p, GLECMA_RULE_SUPER_PROPERTY_POSITION, &super_at);
      }
      parser_next(p);
      parser_event(p, GLECMA_BEV_USE, GLECMA_BUSE_SUPER_PROPERTY, NULL, start,
          super_end);
      return member_bracket(p, s, 0);
    default:
      parser_fail_token(p, GLECMA_RULE_INVALID_SUPER);
  }
}

static PExpr parse_import(Parser * p) {
  size_t start = p->tok.start.offset;
  parser_next(p);
  if (p->tok.kind == GLECMA_TOK_DOT) {
    // `import.meta` is a Module's: a Script has no such expression.
    parser_fail_token(p, GLECMA_RULE_IMPORT_META_OUTSIDE_MODULE);
  }
  if (p->tok.kind != GLECMA_TOK_LPAREN) {
    parser_unexpected(p);
  }
  parser_next(p);
  bool saved_in = p->no_in;
  p->no_in = false;
  size_t mark = p->kids_len;
  PExpr specifier = parse_assignment(p, 0);
  parser_kid(p, specifier.node);
  GLECMA_BNode attributes = NULL;
  if (p->tok.kind == GLECMA_TOK_COMMA) {
    parser_next(p);
    if (p->tok.kind != GLECMA_TOK_RPAREN) {
      attributes = parse_assignment(p, 0).node;
      if (p->tok.kind == GLECMA_TOK_COMMA) {
        parser_next(p);
      }
    }
  }
  parser_kid(p, attributes);
  p->no_in = saved_in;
  if (p->tok.kind != GLECMA_TOK_RPAREN) {
    parser_fail_token(p, GLECMA_RULE_IMPORT_CALL_ARGUMENTS);
  }
  size_t end = p->tok.end.offset;
  parser_next(p);
  GLECMA_BNode node = parser_make(p, GLECMA_NODE_IMPORT_CALL, 0, 0, start, end,
      mark);
  return expr_of(node, start, end, SH_OTHER);
}

static PExpr parse_new(Parser * p);

// A MemberExpression: the callee of `new`, with no call among its members.
static PExpr parse_member_no_call(Parser * p) {
  PExpr e;
  parser_enter(p);
  if (parser_is_kw(p, GLECMA_KW_NEW)) {
    e = parse_new(p);
  } else if (parser_is_kw(p, GLECMA_KW_SUPER)) {
    // `new super.x()` is a MemberExpression; `new super()` is not.
    GLECMA_Token after;
    if (!parser_peek(p, &after) || (after.kind != GLECMA_TOK_DOT &&
            after.kind != GLECMA_TOK_LBRACKET)) {
      parser_next(p);
      parser_fail_token(p, GLECMA_RULE_INVALID_SUPER);
    }
    e = parse_super(p);
  } else if (parser_is_kw(p, GLECMA_KW_IMPORT)) {
    // Only `import.meta` is a MemberExpression, and a Script has none.
    parser_next(p);
    if (p->tok.kind == GLECMA_TOK_DOT) {
      parser_fail_token(p, GLECMA_RULE_IMPORT_META_OUTSIDE_MODULE);
    }
    parser_unexpected(p);
  } else {
    e = parse_primary(p);
    if (e.shape == SH_ARROW) {
      parser_leave(p);
      return e;
    }
  }
  e = parse_tails(p, e, false);
  parser_leave(p);
  return e;
}

static PExpr parse_new(Parser * p) {
  size_t start = p->tok.start.offset;
  const GLECMA_Location new_at = p->tok.start;
  parser_next(p);
  if (p->tok.kind == GLECMA_TOK_DOT) {
    parser_next(p);
    return parse_new_target(p, start, &new_at);
  }
  PExpr callee = parse_member_no_call(p);
  if (callee.shape == SH_ARROW) {
    parser_unexpected(p);
  }
  size_t mark = p->kids_len;
  parser_kid(p, callee.node);
  size_t end = callee.end;
  if (p->tok.kind == GLECMA_TOK_LPAREN) {
    parse_arguments(p, &end);
  }
  GLECMA_BNode node = parser_make(p, GLECMA_NODE_NEW_EXPRESSION, 0, 0, start,
      end, mark);
  return expr_of(node, start, end, SH_OTHER);
}

PExpr parse_lhs(Parser * p) {
  PExpr e;
  if (parser_is_kw(p, GLECMA_KW_NEW)) {
    e = parse_new(p);
  } else if (parser_is_kw(p, GLECMA_KW_SUPER)) {
    e = parse_super(p);
  } else if (parser_is_kw(p, GLECMA_KW_IMPORT)) {
    e = parse_import(p);
  } else {
    e = parse_primary(p);
    if (e.shape == SH_ARROW) {
      return e;
    }
  }
  return parse_tails(p, e, true);
}

/* ---- Operators ---------------------------------------------------------------------- */

static PExpr parse_unary_operand(Parser * p);

static PExpr unary_inner(Parser * p) {
  const size_t start = p->tok.start.offset;
  uint32_t op = GLECMA_OP_NONE;
  switch (p->tok.kind) {
    case GLECMA_TOK_PLUS: op = GLECMA_OP_PLUS; break;
    case GLECMA_TOK_MINUS: op = GLECMA_OP_NEG; break;
    case GLECMA_TOK_BANG: op = GLECMA_OP_NOT; break;
    case GLECMA_TOK_TILDE: op = GLECMA_OP_BIT_NOT; break;
    case GLECMA_TOK_NAME:
      if (parser_is_kw(p, GLECMA_KW_TYPEOF)) {
        op = GLECMA_OP_TYPEOF;
      } else if (parser_is_kw(p, GLECMA_KW_VOID)) {
        op = GLECMA_OP_VOID;
      } else if (parser_is_kw(p, GLECMA_KW_DELETE)) {
        op = GLECMA_OP_DELETE;
      } else if ((p->flags & PF_AWAIT) != 0 && parser_is_kw(p, GLECMA_KW_AWAIT)) {
        if ((p->flags & PF_PARAMS) != 0) {
          parser_fail_token(p, GLECMA_RULE_AWAIT_IN_PARAMETERS);
        }
        parser_next(p);
        PExpr arg = parse_unary_operand(p);
        size_t mark = p->kids_len;
        parser_kid(p, arg.node);
        GLECMA_BNode node = parser_make(p, GLECMA_NODE_AWAIT_EXPRESSION, 0, 0,
            start, arg.end, mark);
        return expr_of(node, start, arg.end, SH_UNARY);
      }
      break;
    case GLECMA_TOK_INC:
    case GLECMA_TOK_DEC: {
      const uint32_t update = p->tok.kind == GLECMA_TOK_INC ? GLECMA_OP_INC
                                                            : GLECMA_OP_DEC;
      GLECMA_Location at = p->tok.start;
      parser_next(p);
      PExpr arg = parse_unary_operand(p);
      if (!expr_valid_target(p, &arg, true)) {
        parser_fail(p, GLECMA_RULE_INVALID_UPDATE_TARGET, &at);
      }
      expr_mark_write(p, &arg, true);
      size_t mark = p->kids_len;
      parser_kid(p, arg.node);
      GLECMA_BNode node = parser_make(p, GLECMA_NODE_UPDATE_EXPRESSION,
          GLECMA_NODEF_PREFIX, update, start, arg.end, mark);
      return expr_of(node, start, arg.end, SH_OTHER);
    }
    default:
      break;
  }
  if (op != GLECMA_OP_NONE) {
    parser_next(p);
    const GLECMA_Location arg_at = p->tok.start;
    PExpr arg = parse_unary_operand(p);
    if (op == GLECMA_OP_TYPEOF && arg.shape == SH_IDENT) {
      parser_amend_reference(p, arg.ref_start, GLECMA_BREF_TYPEOF, 0);
    } else if (op == GLECMA_OP_DELETE) {
      // Clause 13.5.1.1, through any parentheses: a name in strict code, and a
      // private member always.
      if (arg.shape == SH_IDENT) {
        if ((p->flags & PF_STRICT) != 0) {
          parser_fail(p, GLECMA_RULE_DELETE_IDENTIFIER_STRICT, &arg_at);
        }
        parser_amend_reference(p, arg.ref_start, GLECMA_BREF_DELETE, 0);
      } else if ((arg.aux & PA_PRIVATE) != 0) {
        parser_fail(p, GLECMA_RULE_DELETE_PRIVATE_REFERENCE, &arg_at);
      }
    }
    size_t mark = p->kids_len;
    parser_kid(p, arg.node);
    GLECMA_BNode node = parser_make(p, GLECMA_NODE_UNARY_EXPRESSION, 0, op,
        start, arg.end, mark);
    return expr_of(node, start, arg.end, SH_UNARY);
  }
  PExpr e = parse_lhs(p);
  if (e.shape != SH_ARROW && (p->tok.kind == GLECMA_TOK_INC ||
          p->tok.kind == GLECMA_TOK_DEC) &&
      (p->tok.flags & GLECMA_TOKF_NEWLINE_BEFORE) == 0) {
    if (!expr_valid_target(p, &e, true)) {
      parser_fail_token(p, GLECMA_RULE_INVALID_UPDATE_TARGET);
    }
    expr_mark_write(p, &e, true);
    const uint32_t update = p->tok.kind == GLECMA_TOK_INC ? GLECMA_OP_INC
                                                          : GLECMA_OP_DEC;
    size_t end = p->tok.end.offset;
    size_t mark = p->kids_len;
    parser_kid(p, e.node);
    GLECMA_BNode node = parser_make(p, GLECMA_NODE_UPDATE_EXPRESSION, 0, update,
        e.start, end, mark);
    parser_next(p);
    return expr_of(node, e.start, end, SH_OTHER);
  }
  return e;
}

PExpr parse_unary(Parser * p) {
  parser_check_stack(p);
  return unary_inner(p);
}

// The operand of a prefix operator is one level deeper: a chain of them
// (`!!!!x`) is a chain of nested expressions.
static PExpr parse_unary_operand(Parser * p) {
  parser_enter(p);
  PExpr e = parse_unary(p);
  parser_leave(p);
  return e;
}

// Binary operators by precedence, loosest first; 0 for a token that is none.
static int binary_precedence(const Parser * p, uint32_t * op, bool * logical) {
  *logical = false;
  switch (p->tok.kind) {
    case GLECMA_TOK_QQ: *op = GLECMA_OP_NULLISH; *logical = true; return 1;
    case GLECMA_TOK_OROR: *op = GLECMA_OP_OR; *logical = true; return 2;
    case GLECMA_TOK_ANDAND: *op = GLECMA_OP_AND; *logical = true; return 3;
    case GLECMA_TOK_PIPE: *op = GLECMA_OP_BIT_OR; return 4;
    case GLECMA_TOK_CARET: *op = GLECMA_OP_BIT_XOR; return 5;
    case GLECMA_TOK_AMP: *op = GLECMA_OP_BIT_AND; return 6;
    case GLECMA_TOK_EQ: *op = GLECMA_OP_EQ; return 7;
    case GLECMA_TOK_NE: *op = GLECMA_OP_NE; return 7;
    case GLECMA_TOK_SEQ: *op = GLECMA_OP_SEQ; return 7;
    case GLECMA_TOK_SNE: *op = GLECMA_OP_SNE; return 7;
    case GLECMA_TOK_LT: *op = GLECMA_OP_LT; return 8;
    case GLECMA_TOK_GT: *op = GLECMA_OP_GT; return 8;
    case GLECMA_TOK_LE: *op = GLECMA_OP_LE; return 8;
    case GLECMA_TOK_GE: *op = GLECMA_OP_GE; return 8;
    case GLECMA_TOK_SHL: *op = GLECMA_OP_SHL; return 9;
    case GLECMA_TOK_SHR: *op = GLECMA_OP_SHR; return 9;
    case GLECMA_TOK_USHR: *op = GLECMA_OP_USHR; return 9;
    case GLECMA_TOK_PLUS: *op = GLECMA_OP_ADD; return 10;
    case GLECMA_TOK_MINUS: *op = GLECMA_OP_SUB; return 10;
    case GLECMA_TOK_STAR: *op = GLECMA_OP_MUL; return 11;
    case GLECMA_TOK_SLASH: *op = GLECMA_OP_DIV; return 11;
    case GLECMA_TOK_PERCENT: *op = GLECMA_OP_MOD; return 11;
    case GLECMA_TOK_STARSTAR: *op = GLECMA_OP_EXP; return 12;
    case GLECMA_TOK_NAME:
      if (parser_is_kw(p, GLECMA_KW_INSTANCEOF)) {
        *op = GLECMA_OP_INSTANCEOF;
        return 8;
      }
      if (!p->no_in && parser_is_kw(p, GLECMA_KW_IN)) {
        *op = GLECMA_OP_IN;
        return 8;
      }
      return 0;
    default:
      return 0;
  }
}

#define PREC_RELATIONAL 8

static PExpr parse_binary(Parser * p, int min_precedence) {
  parser_check_stack(p);
  PExpr left;
  if (p->tok.kind == GLECMA_TOK_PRIVATE_NAME) {
    // `#x in o`: a private name is an operand of `in` and of nothing else.
    GLECMA_Token after;
    if (min_precedence > PREC_RELATIONAL || p->no_in || !parser_peek(p, &after) ||
        !token_is_kw(&after, GLECMA_KW_IN)) {
      parser_unexpected(p);
    }
    GLECMA_BNode node = private_name_node(p);
    left = expr_of(node, p->tok.start.offset, p->tok.end.offset, SH_PRIVATE);
    parser_next(p);
  } else {
    left = parse_unary(p);
  }
  for (;;) {
    uint32_t op = 0;
    bool logical = false;
    int precedence = binary_precedence(p, &op, &logical);
    if (precedence == 0 || precedence < min_precedence ||
        left.shape == SH_ARROW) {
      break;
    }
    if (left.shape == SH_PRIVATE && op != GLECMA_OP_IN) {
      parser_unexpected(p);
    }
    if (op == GLECMA_OP_EXP && left.shape == SH_UNARY && !left.paren) {
      parser_fail_token(p, GLECMA_RULE_EXPONENT_UNARY_OPERAND);
    }
    GLECMA_Location op_at = p->tok.start;
    parser_next(p);
    PExpr right;
    if (op == GLECMA_OP_EXP) {
      // Right-associative: a chain of `**` is a chain of nested expressions.
      parser_enter(p);
      right = parse_binary(p, precedence);
      parser_leave(p);
    } else {
      right = parse_binary(p, precedence + 1);
    }
    uint8_t aux = 0;
    if (logical) {
      // `??` cannot be mixed with `&&` or `||` without parentheses.
      if (op == GLECMA_OP_NULLISH) {
        if (((left.aux | right.aux) & PA_ANDOR) != 0) {
          parser_fail(p, GLECMA_RULE_NULLISH_MIXED, &op_at);
        }
        aux = PA_NULLISH;
      } else {
        if (((left.aux | right.aux) & PA_NULLISH) != 0) {
          parser_fail(p, GLECMA_RULE_NULLISH_MIXED, &op_at);
        }
        aux = PA_ANDOR;
      }
    }
    size_t mark = p->kids_len;
    parser_kid(p, left.node);
    parser_kid(p, right.node);
    GLECMA_BNode node = parser_make(p,
        logical ? GLECMA_NODE_LOGICAL_EXPRESSION : GLECMA_NODE_BINARY_EXPRESSION,
        0, op, left.start, right.end, mark);
    left = expr_of(node, left.start, right.end, SH_OTHER);
    left.aux = aux;
  }
  return left;
}

static PExpr parse_conditional(Parser * p) {
  PExpr test = parse_binary(p, 0);
  if (p->tok.kind != GLECMA_TOK_QUESTION || test.shape == SH_ARROW) {
    return test;
  }
  parser_next(p);
  bool saved_in = p->no_in;
  p->no_in = false;
  PExpr consequent = parse_assignment(p, 0);
  p->no_in = saved_in;
  parser_expect(p, GLECMA_TOK_COLON);
  PExpr alternate = parse_assignment(p, 0);
  size_t mark = p->kids_len;
  parser_kid(p, test.node);
  parser_kid(p, consequent.node);
  parser_kid(p, alternate.node);
  GLECMA_BNode node = parser_make(p, GLECMA_NODE_CONDITIONAL_EXPRESSION, 0, 0,
      test.start, alternate.end, mark);
  return expr_of(node, test.start, alternate.end, SH_OTHER);
}

static bool assignment_operator(GLECMA_TokenKind kind, uint32_t * op,
    bool * logical) {
  *logical = false;
  switch (kind) {
    case GLECMA_TOK_ASSIGN: *op = GLECMA_OP_NONE; return true;
    case GLECMA_TOK_PLUS_ASSIGN: *op = GLECMA_OP_ADD; return true;
    case GLECMA_TOK_MINUS_ASSIGN: *op = GLECMA_OP_SUB; return true;
    case GLECMA_TOK_STAR_ASSIGN: *op = GLECMA_OP_MUL; return true;
    case GLECMA_TOK_SLASH_ASSIGN: *op = GLECMA_OP_DIV; return true;
    case GLECMA_TOK_PERCENT_ASSIGN: *op = GLECMA_OP_MOD; return true;
    case GLECMA_TOK_STARSTAR_ASSIGN: *op = GLECMA_OP_EXP; return true;
    case GLECMA_TOK_SHL_ASSIGN: *op = GLECMA_OP_SHL; return true;
    case GLECMA_TOK_SHR_ASSIGN: *op = GLECMA_OP_SHR; return true;
    case GLECMA_TOK_USHR_ASSIGN: *op = GLECMA_OP_USHR; return true;
    case GLECMA_TOK_AMP_ASSIGN: *op = GLECMA_OP_BIT_AND; return true;
    case GLECMA_TOK_PIPE_ASSIGN: *op = GLECMA_OP_BIT_OR; return true;
    case GLECMA_TOK_CARET_ASSIGN: *op = GLECMA_OP_BIT_XOR; return true;
    case GLECMA_TOK_ANDAND_ASSIGN: *op = GLECMA_OP_AND; *logical = true; return true;
    case GLECMA_TOK_OROR_ASSIGN: *op = GLECMA_OP_OR; *logical = true; return true;
    case GLECMA_TOK_QQ_ASSIGN: *op = GLECMA_OP_NULLISH; *logical = true; return true;
    default: return false;
  }
}

static PExpr parse_yield(Parser * p) {
  if ((p->flags & PF_PARAMS) != 0) {
    parser_fail_token(p, GLECMA_RULE_YIELD_IN_PARAMETERS);
  }
  size_t start = p->tok.start.offset;
  size_t end = p->tok.end.offset;
  parser_next(p);
  uint32_t flags = 0;
  size_t mark = p->kids_len;
  bool has_argument = false;
  if ((p->tok.flags & GLECMA_TOKF_NEWLINE_BEFORE) == 0) {
    if (p->tok.kind == GLECMA_TOK_STAR) {
      flags |= GLECMA_NODEF_DELEGATE;
      parser_next(p);
      has_argument = true;
    } else {
      switch (p->tok.kind) {
        case GLECMA_TOK_RPAREN: case GLECMA_TOK_RBRACKET:
        case GLECMA_TOK_RBRACE: case GLECMA_TOK_COMMA:
        case GLECMA_TOK_SEMICOLON: case GLECMA_TOK_COLON: case GLECMA_TOK_EOF:
        case GLECMA_TOK_QUESTION:
          break;
        case GLECMA_TOK_NAME:
          // `in` and `of` cannot begin an expression; no other word is
          // excluded, `of` being an ordinary name.
          has_argument = !(parser_is_kw(p, GLECMA_KW_IN) ||
              parser_is_kw(p, GLECMA_KW_INSTANCEOF));
          break;
        default:
          has_argument = true;
          break;
      }
    }
  }
  if (has_argument) {
    PExpr arg = parse_assignment(p, 0);
    parser_kid(p, arg.node);
    end = arg.end;
  } else if ((flags & GLECMA_NODEF_DELEGATE) != 0) {
    parser_unexpected(p);
  } else {
    parser_kid(p, NULL);
  }
  GLECMA_BNode node = parser_make(p, GLECMA_NODE_YIELD_EXPRESSION, flags, 0,
      start, end, mark);
  return expr_of(node, start, end, SH_OTHER);
}

// What follows a parsed left-hand side: an assignment operator, or the end of
// the assignment expression. `snap` is where the left-hand side began when it
// began with a bracket, so that a literal followed by `=` can be read again as
// a pattern; `is_pattern` says the left-hand side was already read as one.
PARSER_NOINLINE static PExpr finish_assignment(Parser * p, PExpr lhs,
    uint32_t ef, const Snap * snap, bool is_pattern) {
  uint32_t op = 0;
  bool logical = false;
  if ((is_pattern || lhs.shape != SH_ARROW) &&
      assignment_operator(p->tok.kind, &op, &logical)) {
    const bool plain = p->tok.kind == GLECMA_TOK_ASSIGN;
    if (is_pattern) {
      // Already a pattern.
    } else if (plain && snap != NULL && !lhs.paren &&
        (lhs.shape == SH_ARRAY || lhs.shape == SH_OBJECT)) {
      // Remembered, so that a reading of an enclosing pattern's default does
      // not read this one as an expression again: a chain of patterns inside
      // each other's defaults costs quadratic work in the nesting and not
      // exponential.
      parser_memo_add(p, MEMO_PATTERN(snap->tok.start.offset));
      parser_restore(p, snap);
      lhs = parse_assignment_pattern(p);
      p->pending = NO_PENDING;
    } else {
      if (!expr_valid_target(p, &lhs, !logical)) {
        parser_fail_token(p, GLECMA_RULE_INVALID_ASSIGNMENT_TARGET);
      }
      if (p->pending != NO_PENDING) {
        parser_fail(p, p->pending_rule, &p->pending_at);
      }
      expr_mark_write(p, &lhs, !plain);
    }
    parser_next(p);
    PExpr rhs = parse_assignment(p, 0);
    size_t mark = p->kids_len;
    parser_kid(p, lhs.node);
    parser_kid(p, rhs.node);
    GLECMA_BNode node = parser_make(p, GLECMA_NODE_ASSIGNMENT_EXPRESSION, 0, op,
        lhs.start, rhs.end, mark);
    return expr_of(node, lhs.start, rhs.end, SH_OTHER);
  }
  if (is_pattern) {
    parser_unexpected(p);
  }
  if (p->pending != NO_PENDING) {
    const bool bare = !lhs.paren &&
        (lhs.shape == SH_ARRAY || lhs.shape == SH_OBJECT);
    if (!(bare && (ef & EF_COVER) != 0)) {
      parser_fail(p, p->pending_rule, &p->pending_at);
    }
  }
  return lhs;
}

// An assignment expression that begins with a bracket: its snapshot lives in
// this frame alone, so that the common assignment expression carries none.
PARSER_NOINLINE static PExpr assignment_bracketed(Parser * p, uint32_t ef) {
  if (parser_memo_has(p, MEMO_PATTERN(p->tok.start.offset))) {
    PExpr pattern = parse_assignment_pattern(p);
    p->pending = NO_PENDING;
    return finish_assignment(p, pattern, ef, NULL, true);
  }
  Snap snap;
  parser_snap(p, &snap);
  PExpr lhs = parse_conditional(p);
  return finish_assignment(p, lhs, ef, &snap, false);
}

static PExpr assignment_inner(Parser * p, uint32_t ef) {
  if ((p->flags & PF_YIELD) != 0 && parser_is_kw(p, GLECMA_KW_YIELD)) {
    return parse_yield(p);
  }
  const size_t saved_assign = p->assign_start;
  const size_t outer_pending = p->pending;
  const GLECMA_Location outer_at = p->pending_at;
  const uint32_t outer_rule = p->pending_rule;
  p->assign_start = p->tok.start.offset;
  p->pending = NO_PENDING;
  PExpr result;
  if (p->tok.kind == GLECMA_TOK_LBRACKET || p->tok.kind == GLECMA_TOK_LBRACE) {
    result = assignment_bracketed(p, ef);
  } else {
    result = finish_assignment(p, parse_conditional(p), ef, NULL, false);
  }
  p->assign_start = saved_assign;
  if (outer_pending != NO_PENDING) {
    p->pending = outer_pending;
    p->pending_at = outer_at;
    p->pending_rule = outer_rule;
  }
  return result;
}

PExpr parse_assignment(Parser * p, uint32_t ef) {
  parser_enter(p);
  PExpr e = assignment_inner(p, ef);
  parser_leave(p);
  return e;
}

PExpr parse_expression(Parser * p) {
  PExpr first = parse_assignment(p, 0);
  if (p->tok.kind != GLECMA_TOK_COMMA) {
    return first;
  }
  size_t mark = p->kids_len;
  parser_kid(p, first.node);
  size_t end = first.end;
  while (parser_eat(p, GLECMA_TOK_COMMA)) {
    PExpr next = parse_assignment(p, 0);
    parser_kid(p, next.node);
    end = next.end;
  }
  GLECMA_BNode node = parser_make(p, GLECMA_NODE_SEQUENCE_EXPRESSION, 0, 0,
      first.start, end, mark);
  return expr_of(node, first.start, end, SH_OTHER);
}

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
 * Destructuring: binding patterns, which declare names (clause 14.3.3 and the
 * parameter lists), and assignment patterns, whose targets are references
 * (clause 13.15.5). The two have one shape in the tree and differ in what a
 * leaf is: a binding identifier, which is declared, or a left-hand-side
 * expression, which is assigned.
 */

#include <ghoti.io/lang-ecmascript/macros.h>

#include "parse/parser.h"

static PExpr pattern_expr(GLECMA_BNode node, size_t start, size_t end) {
  PExpr e;
  e.node = node;
  e.start = start;
  e.end = end;
  e.ref_start = start;
  e.shape = SH_OTHER;
  e.paren = 0;
  e.aux = 0;
  return e;
}

PExpr parse_binding_identifier(Parser * p, uint32_t decl) {
  if (p->tok.kind != GLECMA_TOK_NAME) {
    parser_unexpected(p);
  }
  uint32_t rule = glecma_token_identifier_check(&p->tok, parser_idf(p));
  if (rule != GLECMA_RULE_NONE) {
    parser_fail_token(p, rule);
  }
  GLECMA_BText name;
  parser_text(p, &p->tok, GLECMA_VALUE_COOKED, true, &name);
  parser_note_binding(p, &p->tok);
  size_t start = p->tok.start.offset;
  size_t end = p->tok.end.offset;
  parser_event(p, GLECMA_BEV_DECLARE, decl, &name, start, end);
  GLECMA_BNode node = parser_make_text(p, GLECMA_NODE_IDENTIFIER, 0, start, end,
      &name, NULL);
  parser_next(p);
  PExpr e = pattern_expr(node, start, end);
  e.shape = SH_IDENT;
  return e;
}

static PExpr binding_pattern(Parser * p, uint32_t decl);

PExpr parse_binding_target(Parser * p, uint32_t decl) {
  switch (p->tok.kind) {
    case GLECMA_TOK_NAME:
      return parse_binding_identifier(p, decl);
    case GLECMA_TOK_LBRACKET:
    case GLECMA_TOK_LBRACE:
      return binding_pattern(p, decl);
    default:
      parser_unexpected(p);
  }
}

PExpr parse_binding_element(Parser * p, uint32_t decl) {
  PExpr target = parse_binding_target(p, decl);
  if (p->tok.kind != GLECMA_TOK_ASSIGN) {
    return target;
  }
  parser_next(p);
  bool saved_in = p->no_in;
  p->no_in = false;
  PExpr initial = parse_assignment(p, 0);
  p->no_in = saved_in;
  size_t mark = p->kids_len;
  parser_kid(p, target.node);
  parser_kid(p, initial.node);
  GLECMA_BNode node = parser_make(p, GLECMA_NODE_ASSIGNMENT_PATTERN, 0, 0,
      target.start, initial.end, mark);
  return pattern_expr(node, target.start, initial.end);
}

static PExpr rest_element(Parser * p, PExpr target, size_t start) {
  size_t mark = p->kids_len;
  parser_kid(p, target.node);
  GLECMA_BNode node = parser_make(p, GLECMA_NODE_REST_ELEMENT, 0, 0, start,
      target.end, mark);
  return pattern_expr(node, start, target.end);
}

static PExpr binding_array(Parser * p, uint32_t decl) {
  size_t start = p->tok.start.offset;
  size_t mark = p->kids_len;
  parser_next(p);
  while (p->tok.kind != GLECMA_TOK_RBRACKET) {
    if (p->tok.kind == GLECMA_TOK_COMMA) {
      parser_kid(p, NULL);
      parser_next(p);
      continue;
    }
    if (p->tok.kind == GLECMA_TOK_ELLIPSIS) {
      size_t rest_start = p->tok.start.offset;
      parser_next(p);
      PExpr target = parse_binding_target(p, decl);
      parser_kid(p, rest_element(p, target, rest_start).node);
      if (p->tok.kind != GLECMA_TOK_RBRACKET) {
        parser_fail_token(p, GLECMA_RULE_REST_ELEMENT_POSITION);
      }
      break;
    }
    parser_kid(p, parse_binding_element(p, decl).node);
    if (p->tok.kind != GLECMA_TOK_RBRACKET) {
      parser_expect(p, GLECMA_TOK_COMMA);
    }
  }
  size_t end = p->tok.end.offset;
  parser_next(p);
  GLECMA_BNode node = parser_make(p, GLECMA_NODE_ARRAY_PATTERN, 0, 0, start,
      end, mark);
  return pattern_expr(node, start, end);
}

static PExpr binding_object(Parser * p, uint32_t decl) {
  size_t start = p->tok.start.offset;
  size_t mark = p->kids_len;
  parser_next(p);
  while (p->tok.kind != GLECMA_TOK_RBRACE) {
    if (p->tok.kind == GLECMA_TOK_ELLIPSIS) {
      size_t rest_start = p->tok.start.offset;
      parser_next(p);
      PExpr target = parse_binding_identifier(p, decl);
      parser_kid(p, rest_element(p, target, rest_start).node);
      if (p->tok.kind != GLECMA_TOK_RBRACE) {
        parser_fail_token(p, GLECMA_RULE_REST_ELEMENT_POSITION);
      }
      break;
    }
    size_t property_start = p->tok.start.offset;
    PKey key;
    parse_property_key(p, &key, false);
    size_t inner = p->kids_len;
    uint32_t flags = key.computed ? GLECMA_NODEF_COMPUTED : 0u;
    size_t end;
    if (p->tok.kind == GLECMA_TOK_COLON) {
      parser_next(p);
      PExpr value = parse_binding_element(p, decl);
      parser_kid(p, key.node);
      parser_kid(p, value.node);
      end = value.end;
    } else {
      if (key.computed || !key.is_name) {
        parser_unexpected(p);
      }
      uint32_t rule = glecma_token_identifier_check(&key.tok, parser_idf(p));
      if (rule != GLECMA_RULE_NONE) {
        parser_fail(p, rule, &key.tok.start);
      }
      GLECMA_BText name;
      parser_text(p, &key.tok, GLECMA_VALUE_COOKED, true, &name);
      parser_note_binding(p, &key.tok);
      parser_event(p, GLECMA_BEV_DECLARE, decl, &name, key.start, key.end);
      GLECMA_BNode target = parser_make_text(p, GLECMA_NODE_IDENTIFIER, 0,
          key.start, key.end, &name, NULL);
      end = key.end;
      GLECMA_BNode value = target;
      if (p->tok.kind == GLECMA_TOK_ASSIGN) {
        parser_next(p);
        bool saved_in = p->no_in;
        p->no_in = false;
        PExpr initial = parse_assignment(p, 0);
        p->no_in = saved_in;
        size_t pair = p->kids_len;
        parser_kid(p, target);
        parser_kid(p, initial.node);
        value = parser_make(p, GLECMA_NODE_ASSIGNMENT_PATTERN, 0, 0, key.start,
            initial.end, pair);
        end = initial.end;
      }
      parser_kid(p, key.node);
      parser_kid(p, value);
      flags |= GLECMA_NODEF_SHORTHAND;
    }
    parser_kid(p, parser_make(p, GLECMA_NODE_PATTERN_PROPERTY, flags, 0,
                       property_start, end, inner));
    if (p->tok.kind != GLECMA_TOK_RBRACE) {
      parser_expect(p, GLECMA_TOK_COMMA);
    }
  }
  size_t end = p->tok.end.offset;
  parser_next(p);
  GLECMA_BNode node = parser_make(p, GLECMA_NODE_OBJECT_PATTERN, 0, 0, start,
      end, mark);
  return pattern_expr(node, start, end);
}

static PExpr binding_pattern(Parser * p, uint32_t decl) {
  parser_enter(p);
  PExpr e = p->tok.kind == GLECMA_TOK_LBRACKET ? binding_array(p, decl)
                                              : binding_object(p, decl);
  parser_leave(p);
  return e;
}

/* ---- Assignment patterns ---------------------------------------------------------- */

static PExpr assignment_element(Parser * p);

// A DestructuringAssignmentTarget: a nested pattern, or a simple target.
static PExpr destructuring_target(Parser * p, bool allow_pattern) {
  if (p->tok.kind == GLECMA_TOK_LBRACKET || p->tok.kind == GLECMA_TOK_LBRACE) {
    // A bracket may begin a nested pattern, or a member expression on a
    // literal (`{}[key]`). The target is read as a left-hand-side expression
    // first: a bare literal is a pattern and is read again as one.
    if (allow_pattern && parser_memo_has(p, MEMO_PATTERN(p->tok.start.offset))) {
      // Read before, and a pattern: not read as an expression again, which
      // would read the patterns in its defaults the same way at every level.
      return parse_assignment_pattern(p);
    }
    Snap snap;
    parser_snap(p, &snap);
    const size_t outer_pending = p->pending;
    const GLECMA_Location outer_at = p->pending_at;
    p->pending = NO_PENDING;
    GLECMA_Location at = p->tok.start;
    PExpr literal = parse_lhs(p);
    const size_t own_pending = p->pending;
    const GLECMA_Location own_at = p->pending_at;
    p->pending = outer_pending;
    p->pending_at = outer_at;
    if (!literal.paren && (literal.shape == SH_ARRAY || literal.shape == SH_OBJECT)) {
      if (!allow_pattern) {
        parser_fail(p, GLECMA_RULE_INVALID_ASSIGNMENT_TARGET, &at);
      }
      parser_memo_add(p, MEMO_PATTERN(snap.tok.start.offset));
      parser_restore(p, &snap);
      return parse_assignment_pattern(p);
    }
    if (own_pending != NO_PENDING) {
      parser_fail(p, GLECMA_RULE_COVER_INITIALIZED_NAME, &own_at);
    }
    if (!expr_valid_target(p, &literal, false)) {
      parser_fail(p, GLECMA_RULE_INVALID_ASSIGNMENT_TARGET, &at);
    }
    expr_mark_write(p, &literal, false);
    return literal;
  }
  GLECMA_Location at = p->tok.start;
  PExpr target = parse_lhs(p);
  if (!expr_valid_target(p, &target, false)) {
    parser_fail(p, GLECMA_RULE_INVALID_ASSIGNMENT_TARGET, &at);
  }
  expr_mark_write(p, &target, false);
  return target;
}

static PExpr assignment_element(Parser * p) {
  PExpr target = destructuring_target(p, true);
  if (p->tok.kind != GLECMA_TOK_ASSIGN) {
    return target;
  }
  parser_next(p);
  PExpr initial = parse_assignment(p, 0);
  size_t mark = p->kids_len;
  parser_kid(p, target.node);
  parser_kid(p, initial.node);
  GLECMA_BNode node = parser_make(p, GLECMA_NODE_ASSIGNMENT_PATTERN, 0, 0,
      target.start, initial.end, mark);
  return pattern_expr(node, target.start, initial.end);
}

static PExpr assignment_array(Parser * p) {
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
      size_t rest_start = p->tok.start.offset;
      parser_next(p);
      PExpr target = destructuring_target(p, true);
      parser_kid(p, rest_element(p, target, rest_start).node);
      if (p->tok.kind != GLECMA_TOK_RBRACKET) {
        parser_fail_token(p, GLECMA_RULE_REST_ELEMENT_POSITION);
      }
      break;
    }
    parser_kid(p, assignment_element(p).node);
    if (p->tok.kind != GLECMA_TOK_RBRACKET) {
      parser_expect(p, GLECMA_TOK_COMMA);
    }
  }
  p->no_in = saved_in;
  size_t end = p->tok.end.offset;
  parser_next(p);
  GLECMA_BNode node = parser_make(p, GLECMA_NODE_ARRAY_PATTERN, 0, 0, start,
      end, mark);
  return pattern_expr(node, start, end);
}

static PExpr assignment_object(Parser * p) {
  size_t start = p->tok.start.offset;
  size_t mark = p->kids_len;
  parser_next(p);
  bool saved_in = p->no_in;
  p->no_in = false;
  while (p->tok.kind != GLECMA_TOK_RBRACE) {
    if (p->tok.kind == GLECMA_TOK_ELLIPSIS) {
      size_t rest_start = p->tok.start.offset;
      parser_next(p);
      PExpr target = destructuring_target(p, false);
      parser_kid(p, rest_element(p, target, rest_start).node);
      if (p->tok.kind != GLECMA_TOK_RBRACE) {
        parser_fail_token(p, GLECMA_RULE_REST_ELEMENT_POSITION);
      }
      break;
    }
    size_t property_start = p->tok.start.offset;
    PKey key;
    parse_property_key(p, &key, false);
    size_t inner = p->kids_len;
    uint32_t flags = key.computed ? GLECMA_NODEF_COMPUTED : 0u;
    size_t end;
    if (p->tok.kind == GLECMA_TOK_COLON) {
      parser_next(p);
      PExpr value = assignment_element(p);
      parser_kid(p, key.node);
      parser_kid(p, value.node);
      end = value.end;
    } else {
      if (key.computed || !key.is_name) {
        parser_unexpected(p);
      }
      uint32_t rule = glecma_token_identifier_check(&key.tok, parser_idf(p));
      if (rule != GLECMA_RULE_NONE) {
        parser_fail(p, rule, &key.tok.start);
      }
      GLECMA_BText name;
      parser_text(p, &key.tok, GLECMA_VALUE_COOKED, true, &name);
      parser_event(p, GLECMA_BEV_REFERENCE, GLECMA_BREF_WRITE, &name, key.start,
          key.end);
      GLECMA_BNode target = parser_make_text(p, GLECMA_NODE_IDENTIFIER, 0,
          key.start, key.end, &name, NULL);
      end = key.end;
      GLECMA_BNode value = target;
      if (p->tok.kind == GLECMA_TOK_ASSIGN) {
        parser_next(p);
        PExpr initial = parse_assignment(p, 0);
        size_t pair = p->kids_len;
        parser_kid(p, target);
        parser_kid(p, initial.node);
        value = parser_make(p, GLECMA_NODE_ASSIGNMENT_PATTERN, 0, 0, key.start,
            initial.end, pair);
        end = initial.end;
      }
      parser_kid(p, key.node);
      parser_kid(p, value);
      flags |= GLECMA_NODEF_SHORTHAND;
    }
    parser_kid(p, parser_make(p, GLECMA_NODE_PATTERN_PROPERTY, flags, 0,
                       property_start, end, inner));
    if (p->tok.kind != GLECMA_TOK_RBRACE) {
      parser_expect(p, GLECMA_TOK_COMMA);
    }
  }
  p->no_in = saved_in;
  size_t end = p->tok.end.offset;
  parser_next(p);
  GLECMA_BNode node = parser_make(p, GLECMA_NODE_OBJECT_PATTERN, 0, 0, start,
      end, mark);
  return pattern_expr(node, start, end);
}

PExpr parse_assignment_pattern(Parser * p) {
  parser_enter(p);
  PExpr e = p->tok.kind == GLECMA_TOK_LBRACKET ? assignment_array(p)
                                              : assignment_object(p);
  parser_leave(p);
  return e;
}

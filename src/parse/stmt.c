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
 * Statements and declarations (clause 14) and the directive prologue
 * (clause 11.2.1).
 *
 * Automatic semicolon insertion (clause 12.10) is `parser_semicolon` and the
 * restricted productions (`return`, `break`, `continue`, `throw`, postfix
 * operators, `yield`, arrows, `async`), each of which checks the line
 * terminator where its production says. Annex B's syntax is here: a function
 * declaration as the body of an `if` or of a label, and a `for-in` head with
 * an initializer. A function declaration the Annex puts in an `if` is placed
 * in a block of its own, as the Annex says it is to be treated.
 */

#include <ghoti.io/lang-ecmascript/macros.h>

#include "parse/parser.h"

#include <string.h>

/* ---- Helpers ---------------------------------------------------------------------- */

static GLECMA_BNode statement_node(Parser * p, uint32_t kind, uint32_t flags,
    size_t start, size_t mark) {
  return parser_make(p, kind, flags, 0, start, p->prev_end, mark);
}

static void block_scope(Parser * p, uint32_t type, size_t start, size_t end) {
  parser_event(p, type, GLECMA_BSCOPE_BLOCK, NULL, start, end);
}

// Whether `let` begins a declaration: what follows is a binding.
static bool let_declares(Parser * p) {
  if ((p->flags & PF_STRICT) != 0) {
    return true; // reserved: it can only begin a declaration
  }
  GLECMA_Token after;
  if (!parser_peek(p, &after)) {
    return false;
  }
  if (after.kind == GLECMA_TOK_LBRACKET || after.kind == GLECMA_TOK_LBRACE) {
    return true;
  }
  return after.kind == GLECMA_TOK_NAME &&
      glecma_token_identifier_check(&after, 0) == GLECMA_RULE_NONE;
}

/* ---- Variable declarations --------------------------------------------------------- */

GLECMA_BNode parse_variable_declaration(Parser * p, uint32_t kind, bool in_for,
    size_t start, uint32_t * out_info) {
  // The keyword has been read.
  size_t mark = p->kids_len;
  uint32_t info = 0;
  size_t count = 0;
  for (;;) {
    size_t declarator_start = p->tok.start.offset;
    const GLECMA_Location declarator_at = p->tok.start;
    PExpr target = parse_binding_target(p, kind);
    const bool pattern = target.shape != SH_IDENT;
    GLECMA_BNode initial = NULL;
    size_t end = target.end;
    if (p->tok.kind == GLECMA_TOK_ASSIGN) {
      parser_next(p);
      PExpr value = parse_assignment(p, 0);
      initial = value.node;
      end = value.end;
      if (count == 0) {
        info |= VDI_HAS_INIT;
      }
    } else {
      if (pattern) {
        info |= VDI_PATTERN_NO_INIT;
      }
      if (kind == GLECMA_BDECL_CONST) {
        info |= VDI_CONST_NO_INIT;
        if (!in_for) {
          parser_fail(p, GLECMA_RULE_CONST_WITHOUT_INITIALIZER, &declarator_at);
        }
      }
    }
    if (count == 0 && pattern) {
      info |= VDI_PATTERN;
    }
    if (pattern && initial == NULL && !in_for) {
      // A pattern needs a value to take apart.
      parser_unexpected(p);
    }
    size_t inner = p->kids_len;
    parser_kid(p, target.node);
    parser_kid(p, initial);
    parser_kid(p, parser_make(p, GLECMA_NODE_VARIABLE_DECLARATOR, 0, 0,
                       declarator_start, end, inner));
    count++;
    if (!parser_eat(p, GLECMA_TOK_COMMA)) {
      break;
    }
  }
  if (count > 1) {
    info |= VDI_MULTIPLE;
  }
  if (!in_for) {
    parser_semicolon(p);
  }
  if (out_info != NULL) {
    *out_info = info;
  }
  uint32_t flags = kind == GLECMA_BDECL_VAR ? GLECMA_NODEF_VAR
                 : kind == GLECMA_BDECL_LET ? GLECMA_NODEF_LET
                                            : GLECMA_NODEF_CONST;
  return parser_make(p, GLECMA_NODE_VARIABLE_DECLARATION, flags, 0, start,
      p->prev_end, mark);
}

/* ---- Statements ---------------------------------------------------------------------- */

GLECMA_BNode parse_block(Parser * p, bool scoped) {
  size_t start = p->tok.start.offset;
  size_t mark = p->kids_len;
  parser_expect(p, GLECMA_TOK_LBRACE);
  if (scoped) {
    block_scope(p, GLECMA_BEV_SCOPE_ENTER, start, start);
  }
  while (p->tok.kind != GLECMA_TOK_RBRACE) {
    if (p->tok.kind == GLECMA_TOK_EOF) {
      parser_unexpected(p);
    }
    parser_kid(p, parse_statement_list_item(p));
  }
  size_t end = p->tok.end.offset;
  parser_next(p);
  if (scoped) {
    block_scope(p, GLECMA_BEV_SCOPE_LEAVE, start, end);
  }
  return parser_make(p, GLECMA_NODE_BLOCK, 0, 0, start, end, mark);
}

static GLECMA_BNode paren_expression(Parser * p) {
  parser_expect(p, GLECMA_TOK_LPAREN);
  bool saved_in = p->no_in;
  p->no_in = false;
  PExpr e = parse_expression(p);
  p->no_in = saved_in;
  parser_expect(p, GLECMA_TOK_RPAREN);
  return e.node;
}

// The body of an `if`, a loop or `with`: a statement, and not a labelled
// function declaration (clauses 14.6.1, 14.7.2.1, 14.7.3.1, 14.7.4.1, 14.7.5.1,
// 14.11.1).
static GLECMA_BNode statement_body(Parser * p) {
  const GLECMA_Location at = p->tok.start;
  GLECMA_BNode body = parse_statement(p);
  if (p->last_labelled_fn) {
    parser_fail(p, GLECMA_RULE_LABELLED_FUNCTION_BODY, &at);
  }
  return body;
}

// The body of a loop: the targets of `break` and `continue` are inside it.
static GLECMA_BNode loop_body(Parser * p) {
  p->loops++;
  p->breakables++;
  GLECMA_BNode body = statement_body(p);
  p->loops--;
  p->breakables--;
  return body;
}

// An `if` body or label body that is a function declaration (Annex B.3.1,
// B.3.3): a block of its own, with the declaration in it.
static GLECMA_BNode function_in_block(Parser * p, bool in_if) {
  size_t start = p->tok.start.offset;
  if ((p->flags & PF_STRICT) != 0) {
    parser_fail_token(p, GLECMA_RULE_DECLARATION_POSITION);
  }
  if (!in_if) {
    // A labelled function is the declaration itself.
    return parse_function_declaration(p, start, false, false).node;
  }
  block_scope(p, GLECMA_BEV_SCOPE_ENTER, start, start);
  size_t mark = p->kids_len;
  PExpr function = parse_function_declaration(p, start, false, false);
  parser_kid(p, function.node);
  block_scope(p, GLECMA_BEV_SCOPE_LEAVE, start, function.end);
  return parser_make(p, GLECMA_NODE_BLOCK, 0, 0, start, function.end, mark);
}

static GLECMA_BNode parse_if(Parser * p) {
  size_t start = p->tok.start.offset;
  size_t mark = p->kids_len;
  parser_next(p);
  GLECMA_BNode test = paren_expression(p);
  parser_kid(p, test);
  if (parser_is_kw(p, GLECMA_KW_FUNCTION)) {
    parser_kid(p, function_in_block(p, true));
  } else {
    parser_kid(p, statement_body(p));
  }
  if (parser_is_kw(p, GLECMA_KW_ELSE)) {
    parser_next(p);
    if (parser_is_kw(p, GLECMA_KW_FUNCTION)) {
      parser_kid(p, function_in_block(p, true));
    } else {
      parser_kid(p, statement_body(p));
    }
  } else {
    parser_kid(p, NULL);
  }
  return statement_node(p, GLECMA_NODE_IF_STATEMENT, 0, start, mark);
}

static GLECMA_BNode parse_for(Parser * p) {
  const size_t start = p->tok.start.offset;
  parser_next(p); // `for`
  bool is_await = false;
  if (parser_is_kw(p, GLECMA_KW_AWAIT)) {
    if ((p->flags & PF_AWAIT) == 0) {
      parser_fail_token(p, GLECMA_RULE_FOR_AWAIT_POSITION);
    }
    is_await = true;
    parser_next(p);
  }
  parser_expect(p, GLECMA_TOK_LPAREN);
  parser_event(p, GLECMA_BEV_SCOPE_ENTER, GLECMA_BSCOPE_FOR, NULL, start, start);
  const bool saved_in = p->no_in;
  GLECMA_BNode init = NULL;
  GLECMA_BNode left = NULL;
  bool is_loop_in = false;
  bool is_loop_of = false;
  uint32_t kind = 0;
  if (p->tok.kind == GLECMA_TOK_SEMICOLON) {
    // no initialization
  } else if (parser_is_kw(p, GLECMA_KW_VAR) || parser_is_kw(p, GLECMA_KW_CONST) ||
      (parser_is_kw(p, GLECMA_KW_LET) && let_declares(p))) {
    kind = parser_is_kw(p, GLECMA_KW_VAR) ? GLECMA_BDECL_VAR
         : parser_is_kw(p, GLECMA_KW_CONST) ? GLECMA_BDECL_CONST
                                            : GLECMA_BDECL_LET;
    size_t decl_start = p->tok.start.offset;
    parser_next(p);
    p->no_in = true;
    uint32_t info = 0;
    GLECMA_BNode declaration = parse_variable_declaration(p, kind, true,
        decl_start, &info);
    p->no_in = saved_in;
    is_loop_of = parser_is_kw(p, GLECMA_KW_OF);
    is_loop_in = parser_is_kw(p, GLECMA_KW_IN);
    if (is_loop_of || is_loop_in) {
      if ((info & VDI_MULTIPLE) != 0) {
        parser_fail_token(p, GLECMA_RULE_FOR_HEAD_RESTRICTED);
      }
      if ((info & VDI_HAS_INIT) != 0) {
        // Annex B.3.5: `for (var x = 1 in o)` in sloppy code, and only that.
        if (is_loop_of || kind != GLECMA_BDECL_VAR ||
            (info & VDI_PATTERN) != 0 || (p->flags & PF_STRICT) != 0) {
          parser_fail_token(p, GLECMA_RULE_FOR_HEAD_RESTRICTED);
        }
      }
      left = declaration;
    } else {
      if ((info & VDI_PATTERN_NO_INIT) != 0) {
        parser_unexpected(p);
      }
      if ((info & VDI_CONST_NO_INIT) != 0) {
        parser_fail(p, GLECMA_RULE_CONST_WITHOUT_INITIALIZER, &p->tok.start);
      }
      init = declaration;
    }
  } else {
    // An expression, or the target of a for-in or for-of.
    const bool starts_with_let = parser_is_kw(p, GLECMA_KW_LET);
    Snap snap;
    parser_snap(p, &snap);
    p->no_in = true;
    p->pending = NO_PENDING;
    PExpr head = parse_assignment(p, EF_COVER);
    p->no_in = saved_in;
    is_loop_of = parser_is_kw(p, GLECMA_KW_OF);
    is_loop_in = parser_is_kw(p, GLECMA_KW_IN);
    if (is_loop_of || is_loop_in) {
      if (!head.paren && (head.shape == SH_ARRAY || head.shape == SH_OBJECT)) {
        parser_restore(p, &snap);
        p->pending = NO_PENDING;
        p->no_in = true;
        head = parse_assignment_pattern(p);
        p->no_in = saved_in;
      } else {
        if (p->pending != NO_PENDING) {
          parser_fail(p, p->pending_rule, &p->pending_at);
        }
        if (!expr_valid_target(p, &head, true)) {
          parser_fail(p, GLECMA_RULE_INVALID_FOR_TARGET, &snap.tok.start);
        }
        expr_mark_write(p, &head, false);
      }
      if (is_loop_of && (starts_with_let ||
              (!is_await && head.shape == SH_IDENT && !head.paren &&
                  (head.aux & PA_ASYNC) != 0))) {
        // `for (let of ...)` and `for (async of ...)` are refused by the
        // grammar's lookahead restrictions.
        parser_fail(p, GLECMA_RULE_FOR_HEAD_RESTRICTED, &snap.tok.start);
      }
      left = head.node;
    } else {
      if (p->pending != NO_PENDING) {
        parser_fail(p, p->pending_rule, &p->pending_at);
      }
      GLECMA_BNode node = head.node;
      if (p->tok.kind == GLECMA_TOK_COMMA) {
        // The rest of a comma expression.
        size_t mark = p->kids_len;
        parser_kid(p, head.node);
        size_t end = head.end;
        p->no_in = true;
        while (parser_eat(p, GLECMA_TOK_COMMA)) {
          PExpr next = parse_assignment(p, 0);
          parser_kid(p, next.node);
          end = next.end;
        }
        p->no_in = saved_in;
        node = parser_make(p, GLECMA_NODE_SEQUENCE_EXPRESSION, 0, 0, head.start,
            end, mark);
      }
      init = node;
    }
  }
  size_t mark = p->kids_len;
  uint32_t node_kind;
  uint32_t flags = 0;
  if (is_loop_of || is_loop_in) {
    parser_next(p); // `of` or `in`
    if (is_await && !is_loop_of) {
      parser_unexpected(p);
    }
    PExpr right = is_loop_of ? parse_assignment(p, 0) : parse_expression(p);
    parser_expect(p, GLECMA_TOK_RPAREN);
    GLECMA_BNode body = loop_body(p);
    parser_kid(p, left);
    parser_kid(p, right.node);
    parser_kid(p, body);
    node_kind = is_loop_of ? GLECMA_NODE_FOR_OF_STATEMENT
                           : GLECMA_NODE_FOR_IN_STATEMENT;
    if (is_await) {
      flags |= GLECMA_NODEF_AWAIT;
    }
  } else {
    if (is_await) {
      parser_unexpected(p);
    }
    parser_expect(p, GLECMA_TOK_SEMICOLON);
    GLECMA_BNode test = NULL;
    GLECMA_BNode update = NULL;
    if (p->tok.kind != GLECMA_TOK_SEMICOLON) {
      test = parse_expression(p).node;
    }
    parser_expect(p, GLECMA_TOK_SEMICOLON);
    if (p->tok.kind != GLECMA_TOK_RPAREN) {
      update = parse_expression(p).node;
    }
    parser_expect(p, GLECMA_TOK_RPAREN);
    GLECMA_BNode body = loop_body(p);
    parser_kid(p, init);
    parser_kid(p, test);
    parser_kid(p, update);
    parser_kid(p, body);
    node_kind = GLECMA_NODE_FOR_STATEMENT;
  }
  parser_event(p, GLECMA_BEV_SCOPE_LEAVE, GLECMA_BSCOPE_FOR, NULL, start,
      p->prev_end);
  return parser_make(p, node_kind, flags, 0, start, p->prev_end, mark);
}

static GLECMA_BNode parse_switch(Parser * p) {
  size_t start = p->tok.start.offset;
  size_t mark = p->kids_len;
  parser_next(p);
  parser_kid(p, paren_expression(p));
  parser_expect(p, GLECMA_TOK_LBRACE);
  block_scope(p, GLECMA_BEV_SCOPE_ENTER, start, start);
  p->breakables++;
  bool seen_default = false;
  while (p->tok.kind != GLECMA_TOK_RBRACE) {
    size_t case_start = p->tok.start.offset;
    size_t inner = p->kids_len;
    uint32_t flags = 0;
    if (parser_is_kw(p, GLECMA_KW_CASE)) {
      parser_next(p);
      parser_kid(p, parse_expression(p).node);
    } else if (parser_is_kw(p, GLECMA_KW_DEFAULT)) {
      // CaseBlock has one DefaultClause at most (clause 14.12).
      if (seen_default) {
        parser_unexpected(p);
      }
      seen_default = true;
      parser_next(p);
      parser_kid(p, NULL);
      flags |= GLECMA_NODEF_DEFAULT;
    } else {
      parser_unexpected(p);
    }
    parser_expect(p, GLECMA_TOK_COLON);
    while (p->tok.kind != GLECMA_TOK_RBRACE && !parser_is_kw(p, GLECMA_KW_CASE) &&
        !parser_is_kw(p, GLECMA_KW_DEFAULT)) {
      if (p->tok.kind == GLECMA_TOK_EOF) {
        parser_unexpected(p);
      }
      parser_kid(p, parse_statement_list_item(p));
    }
    GLECMA_BNode node = parser_make(p, GLECMA_NODE_SWITCH_CASE, flags, 0,
        case_start, p->prev_end, inner);
    parser_kid(p, node);
  }
  p->breakables--;
  size_t end = p->tok.end.offset;
  parser_next(p);
  block_scope(p, GLECMA_BEV_SCOPE_LEAVE, start, end);
  return parser_make(p, GLECMA_NODE_SWITCH_STATEMENT, 0, 0, start, end, mark);
}

static GLECMA_BNode parse_try(Parser * p) {
  size_t start = p->tok.start.offset;
  size_t mark = p->kids_len;
  parser_next(p);
  parser_kid(p, parse_block(p, true));
  GLECMA_BNode handler = NULL;
  GLECMA_BNode finalizer = NULL;
  if (parser_is_kw(p, GLECMA_KW_CATCH)) {
    size_t catch_start = p->tok.start.offset;
    size_t inner = p->kids_len;
    parser_next(p);
    parser_event(p, GLECMA_BEV_SCOPE_ENTER, GLECMA_BSCOPE_CATCH, NULL,
        catch_start, catch_start);
    if (parser_eat(p, GLECMA_TOK_LPAREN)) {
      parser_kid(p, parse_binding_target(p, GLECMA_BDECL_CATCH_PARAM).node);
      parser_expect(p, GLECMA_TOK_RPAREN);
    } else {
      parser_kid(p, NULL);
    }
    parser_kid(p, parse_block(p, true));
    parser_event(p, GLECMA_BEV_SCOPE_LEAVE, GLECMA_BSCOPE_CATCH, NULL,
        catch_start, p->prev_end);
    handler = parser_make(p, GLECMA_NODE_CATCH_CLAUSE, 0, 0, catch_start,
        p->prev_end, inner);
  }
  if (parser_is_kw(p, GLECMA_KW_FINALLY)) {
    parser_next(p);
    finalizer = parse_block(p, true);
  }
  if (handler == NULL && finalizer == NULL) {
    parser_unexpected(p);
  }
  parser_kid(p, handler);
  parser_kid(p, finalizer);
  return statement_node(p, GLECMA_NODE_TRY_STATEMENT, 0, start, mark);
}

static GLECMA_BNode parse_jump(Parser * p, uint32_t kind) {
  size_t start = p->tok.start.offset;
  const GLECMA_Location at = p->tok.start;
  size_t mark = p->kids_len;
  parser_next(p);
  const bool is_break = kind == GLECMA_NODE_BREAK_STATEMENT;
  if (p->tok.kind == GLECMA_TOK_NAME &&
      (p->tok.flags & GLECMA_TOKF_NEWLINE_BEFORE) == 0) {
    uint32_t rule = glecma_token_identifier_check(&p->tok, parser_idf(p));
    if (rule != GLECMA_RULE_NONE) {
      parser_fail_token(p, rule);
    }
    GLECMA_BText name;
    parser_text(p, &p->tok, GLECMA_VALUE_COOKED, true, &name);
    if (is_break) {
      early_break(p, &name, &at);
    } else {
      early_continue(p, &name, &at);
    }
    parser_kid(p, parser_make_text(p, GLECMA_NODE_IDENTIFIER, 0,
                       p->tok.start.offset, p->tok.end.offset, &name, NULL));
    parser_next(p);
  } else {
    if (is_break) {
      early_break(p, NULL, &at);
    } else {
      early_continue(p, NULL, &at);
    }
    parser_kid(p, NULL);
  }
  parser_semicolon(p);
  return statement_node(p, kind, 0, start, mark);
}

// A labelled statement. `chain` is the number of labels directly before it,
// which label the same statement: `continue` may name any of them when that
// statement is a loop (clause 14.8.1). `*labelled_fn` is set when the
// statement the labels end in is a function declaration.
static GLECMA_BNode parse_labelled(Parser * p, const GLECMA_BText * name,
    size_t start, size_t label_end, const GLECMA_Location * at, uint32_t chain,
    bool * labelled_fn) {
  size_t mark = p->kids_len;
  early_label_push(p, name, at);
  parser_kid(p, parser_make_text(p, GLECMA_NODE_IDENTIFIER, 0, start, label_end,
                     name, NULL));
  parser_next(p); // `:`
  if (parser_is_kw(p, GLECMA_KW_FUNCTION)) {
    parser_kid(p, function_in_block(p, false));
    *labelled_fn = true;
  } else {
    p->label_chain = chain + 1;
    parser_kid(p, parse_statement(p));
    *labelled_fn = p->last_labelled_fn;
  }
  early_label_pop(p);
  return statement_node(p, GLECMA_NODE_LABELLED_STATEMENT, 0, start, mark);
}

static GLECMA_BNode expression_statement(Parser * p) {
  size_t start = p->tok.start.offset;
  const bool prologue = p->prologue_item;
  p->prologue_item = false;
  PExpr e = parse_expression(p);
  parser_semicolon(p);
  uint32_t flags = 0;
  if (prologue && e.shape == SH_STRING && !e.paren) {
    flags |= GLECMA_NODEF_DIRECTIVE;
    p->directive_hit = true;
  }
  size_t mark = p->kids_len;
  parser_kid(p, e.node);
  return statement_node(p, GLECMA_NODE_EXPRESSION_STATEMENT, flags, start, mark);
}

static GLECMA_BNode parse_statement_inner(Parser * p, bool * labelled_fn) {
  const size_t start = p->tok.start.offset;
  const uint32_t chain = p->label_chain; // labels directly before this statement
  p->label_chain = 0;
  switch (p->tok.kind) {
    case GLECMA_TOK_LBRACE:
      p->prologue_item = false;
      return parse_block(p, true);
    case GLECMA_TOK_SEMICOLON:
      p->prologue_item = false;
      parser_next(p);
      return statement_node(p, GLECMA_NODE_EMPTY_STATEMENT, 0, start,
          p->kids_len);
    case GLECMA_TOK_NAME:
      break;
    default:
      return expression_statement(p);
  }
  p->prologue_item = false;
  if (p->tok.keyword != GLECMA_KW_NONE && (p->tok.flags & GLECMA_TOKF_ESCAPED) == 0) {
    switch (p->tok.keyword) {
      case GLECMA_KW_VAR: {
        parser_next(p);
        return parse_variable_declaration(p, GLECMA_BDECL_VAR, false, start,
            NULL);
      }
      case GLECMA_KW_IF:
        return parse_if(p);
      case GLECMA_KW_FOR:
        early_label_loops(p, chain);
        return parse_for(p);
      case GLECMA_KW_WHILE: {
        size_t mark = p->kids_len;
        early_label_loops(p, chain);
        parser_next(p);
        parser_kid(p, paren_expression(p));
        parser_kid(p, loop_body(p));
        return statement_node(p, GLECMA_NODE_WHILE_STATEMENT, 0, start, mark);
      }
      case GLECMA_KW_DO: {
        size_t mark = p->kids_len;
        early_label_loops(p, chain);
        parser_next(p);
        parser_kid(p, loop_body(p));
        if (!parser_is_kw(p, GLECMA_KW_WHILE)) {
          parser_unexpected(p);
        }
        parser_next(p);
        parser_kid(p, paren_expression(p));
        // A semicolon after `do-while` may always be omitted (clause 12.10.1).
        parser_eat(p, GLECMA_TOK_SEMICOLON);
        return statement_node(p, GLECMA_NODE_DO_WHILE_STATEMENT, 0, start, mark);
      }
      case GLECMA_KW_CONTINUE:
        return parse_jump(p, GLECMA_NODE_CONTINUE_STATEMENT);
      case GLECMA_KW_BREAK:
        return parse_jump(p, GLECMA_NODE_BREAK_STATEMENT);
      case GLECMA_KW_RETURN: {
        size_t mark = p->kids_len;
        if ((p->flags & PF_RETURN) == 0) {
          parser_fail_token(p, GLECMA_RULE_RETURN_POSITION);
        }
        parser_next(p);
        if (p->tok.kind == GLECMA_TOK_SEMICOLON ||
            p->tok.kind == GLECMA_TOK_RBRACE || p->tok.kind == GLECMA_TOK_EOF ||
            (p->tok.flags & GLECMA_TOKF_NEWLINE_BEFORE) != 0) {
          parser_kid(p, NULL);
        } else {
          parser_kid(p, parse_expression(p).node);
        }
        parser_semicolon(p);
        return statement_node(p, GLECMA_NODE_RETURN_STATEMENT, 0, start, mark);
      }
      case GLECMA_KW_WITH: {
        size_t mark = p->kids_len;
        if ((p->flags & PF_STRICT) != 0) {
          parser_fail_token(p, GLECMA_RULE_WITH_STRICT);
        }
        parser_next(p);
        parser_kid(p, paren_expression(p));
        parser_event(p, GLECMA_BEV_SCOPE_ENTER, GLECMA_BSCOPE_WITH, NULL, start,
            start);
        parser_kid(p, statement_body(p));
        parser_event(p, GLECMA_BEV_SCOPE_LEAVE, GLECMA_BSCOPE_WITH, NULL, start,
            p->prev_end);
        return statement_node(p, GLECMA_NODE_WITH_STATEMENT, 0, start, mark);
      }
      case GLECMA_KW_SWITCH:
        return parse_switch(p);
      case GLECMA_KW_THROW: {
        size_t mark = p->kids_len;
        parser_next(p);
        if ((p->tok.flags & GLECMA_TOKF_NEWLINE_BEFORE) != 0) {
          parser_fail_token(p, GLECMA_RULE_THROW_LINE_TERMINATOR);
        }
        parser_kid(p, parse_expression(p).node);
        parser_semicolon(p);
        return statement_node(p, GLECMA_NODE_THROW_STATEMENT, 0, start, mark);
      }
      case GLECMA_KW_TRY:
        return parse_try(p);
      case GLECMA_KW_DEBUGGER:
        parser_next(p);
        parser_semicolon(p);
        return statement_node(p, GLECMA_NODE_DEBUGGER_STATEMENT, 0, start,
            p->kids_len);
      case GLECMA_KW_FUNCTION:
      case GLECMA_KW_CLASS:
      case GLECMA_KW_CONST:
        parser_fail_token(p, GLECMA_RULE_DECLARATION_POSITION);
      case GLECMA_KW_LET: {
        GLECMA_Token after;
        if (parser_peek(p, &after) && after.kind == GLECMA_TOK_LBRACKET) {
          // ExpressionStatement: [lookahead ≠ let [ ]
          parser_fail_token(p, GLECMA_RULE_DECLARATION_POSITION);
        }
        break;
      }
      case GLECMA_KW_ASYNC: {
        GLECMA_Token after;
        if (parser_peek(p, &after) && token_is_kw(&after, GLECMA_KW_FUNCTION) &&
            (after.flags & GLECMA_TOKF_NEWLINE_BEFORE) == 0) {
          parser_fail_token(p, GLECMA_RULE_DECLARATION_POSITION);
        }
        break;
      }
      case GLECMA_KW_IMPORT: {
        // `import(` and `import.` begin an expression; a Script has no
        // import declaration.
        GLECMA_Token after;
        if (!parser_peek(p, &after) ||
            (after.kind != GLECMA_TOK_LPAREN && after.kind != GLECMA_TOK_DOT)) {
          parser_unexpected(p);
        }
        break;
      }
      case GLECMA_KW_EXPORT:
        parser_unexpected(p);
      default:
        break;
    }
  }
  // A labelled statement, or an expression statement.
  if (p->tok.keyword != GLECMA_KW_FUNCTION) {
    GLECMA_Token after;
    if (glecma_token_identifier_check(&p->tok, parser_idf(p)) == GLECMA_RULE_NONE &&
        parser_peek(p, &after) && after.kind == GLECMA_TOK_COLON) {
      GLECMA_BText name;
      parser_text(p, &p->tok, GLECMA_VALUE_COOKED, true, &name);
      size_t label_end = p->tok.end.offset;
      const GLECMA_Location label_at = p->tok.start;
      parser_next(p);
      return parse_labelled(p, &name, start, label_end, &label_at, chain,
          labelled_fn);
    }
  }
  return expression_statement(p);
}

GLECMA_BNode parse_statement(Parser * p) {
  parser_enter(p);
  bool labelled_fn = false;
  GLECMA_BNode node = parse_statement_inner(p, &labelled_fn);
  p->last_labelled_fn = labelled_fn;
  parser_leave(p);
  return node;
}

GLECMA_BNode parse_statement_list_item(Parser * p) {
  const size_t start = p->tok.start.offset;
  if (p->tok.kind == GLECMA_TOK_NAME && p->tok.keyword != GLECMA_KW_NONE &&
      (p->tok.flags & GLECMA_TOKF_ESCAPED) == 0) {
    switch (p->tok.keyword) {
      case GLECMA_KW_FUNCTION: {
        p->prologue_item = false;
        parser_enter(p);
        GLECMA_BNode node = parse_function_declaration(p, start, false, true).node;
        parser_leave(p);
        return node;
      }
      case GLECMA_KW_CLASS: {
        p->prologue_item = false;
        parser_enter(p);
        GLECMA_BNode node = parse_class_declaration(p).node;
        parser_leave(p);
        return node;
      }
      case GLECMA_KW_CONST: {
        p->prologue_item = false;
        parser_enter(p);
        parser_next(p);
        GLECMA_BNode node = parse_variable_declaration(p, GLECMA_BDECL_CONST,
            false, start, NULL);
        parser_leave(p);
        return node;
      }
      case GLECMA_KW_LET:
        if (let_declares(p)) {
          p->prologue_item = false;
          parser_enter(p);
          parser_next(p);
          GLECMA_BNode node = parse_variable_declaration(p, GLECMA_BDECL_LET,
              false, start, NULL);
          parser_leave(p);
          return node;
        }
        break;
      case GLECMA_KW_ASYNC: {
        GLECMA_Token after;
        if (parser_peek(p, &after) && token_is_kw(&after, GLECMA_KW_FUNCTION) &&
            (after.flags & GLECMA_TOKF_NEWLINE_BEFORE) == 0) {
          p->prologue_item = false;
          parser_enter(p);
          parser_next(p);
          GLECMA_BNode node = parse_function_declaration(p, start, true, true).node;
          parser_leave(p);
          return node;
        }
        break;
      }
      default:
        break;
    }
  }
  return parse_statement(p);
}

/* ---- Lists and the directive prologue ------------------------------------------------- */

static bool is_use_strict(const Parser * p, const GLECMA_Token * t) {
  return t->end.offset - t->start.offset == 12 &&
      token_is_ascii(p, t, "use strict");
}

// Strictness has just been turned on: what was read before the directive in
// the same sloppy lookahead is checked now.
static void check_retroactive(Parser * p, const GLECMA_Location * earlier,
    uint32_t earlier_flags) {
  if (p->retro.rule != GLECMA_RULE_NONE) {
    // The function's name or a parameter is a word that strict code reserves.
    parser_fail(p, p->retro.rule, &p->retro.at);
  }
  if (earlier_flags != 0) {
    parser_fail(p, (earlier_flags & GLECMA_TOKF_LEGACY_OCTAL) != 0
            ? GLECMA_RULE_ESCAPE_LEGACY_OCTAL_STRICT
            : GLECMA_RULE_ESCAPE_NON_OCTAL_DECIMAL_STRICT,
        earlier);
  }
  const uint32_t octal = GLECMA_TOKF_LEGACY_OCTAL | GLECMA_TOKF_NON_OCTAL_DECIMAL;
  if ((p->tok.kind == GLECMA_TOK_NUMBER || p->tok.kind == GLECMA_TOK_STRING) &&
      (p->tok.flags & octal) != 0) {
    uint32_t rule;
    if (p->tok.kind == GLECMA_TOK_NUMBER) {
      rule = (p->tok.flags & GLECMA_TOKF_LEGACY_OCTAL) != 0
          ? GLECMA_RULE_NUMERIC_LEGACY_OCTAL_STRICT
          : GLECMA_RULE_NUMERIC_NON_OCTAL_DECIMAL_STRICT;
    } else {
      rule = (p->tok.flags & GLECMA_TOKF_LEGACY_OCTAL) != 0
          ? GLECMA_RULE_ESCAPE_LEGACY_OCTAL_STRICT
          : GLECMA_RULE_ESCAPE_NON_OCTAL_DECIMAL_STRICT;
    }
    parser_fail_token(p, rule);
  }
}

void parse_statement_list(Parser * p, GLECMA_TokenKind end, bool prologue,
    size_t * out_end, bool * out_strict) {
  bool in_prologue = prologue;
  GLECMA_Location octal_at = {0, 0, 0};
  uint32_t octal_flags = 0;
  while (p->tok.kind != end) {
    if (p->tok.kind == GLECMA_TOK_EOF) {
      parser_unexpected(p);
    }
    const GLECMA_Token first = p->tok;
    p->prologue_item = in_prologue && first.kind == GLECMA_TOK_STRING;
    p->directive_hit = false;
    parser_kid(p, parse_statement_list_item(p));
    p->prologue_item = false;
    if (in_prologue) {
      if (p->directive_hit) {
        if (is_use_strict(p, &first)) {
          if ((p->flags & PF_STRICT) == 0) {
            check_retroactive(p, &octal_at, octal_flags);
            parser_set_strict(p, true);
          }
          if (!p->body_simple) {
            // A function with parameters that are not a simple list cannot
            // be given strictness by its own body (clause 15.2.1). What
            // strict code would have refused before the directive is
            // reported first, being earlier in the text.
            parser_fail(p, GLECMA_RULE_USE_STRICT_NON_SIMPLE_PARAMETERS,
                &first.start);
          }
        } else if (octal_flags == 0 &&
            (first.flags & (GLECMA_TOKF_LEGACY_OCTAL |
                 GLECMA_TOKF_NON_OCTAL_DECIMAL)) != 0) {
          octal_flags = first.flags &
              (GLECMA_TOKF_LEGACY_OCTAL | GLECMA_TOKF_NON_OCTAL_DECIMAL);
          octal_at = first.start;
        }
      } else {
        in_prologue = false;
      }
    }
  }
  *out_end = p->tok.end.offset;
  *out_strict = (p->flags & PF_STRICT) != 0;
}

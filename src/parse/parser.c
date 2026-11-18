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
 * The parser's infrastructure and its entry points: the token stream with its
 * rescans and lookahead, failure, the caps, the children stack, node and event
 * creation, and the functions that open a parse, run it and clean up.
 */

#include <ghoti.io/lang-ecmascript/macros.h>

#include "parse/parser.h"
#include "parse/parse_internal.h"

#include <string.h>

/**
 * Stack kept in reserve when the depth is checked: what the frames between
 * two checks, the tokenizer and the builder can take. design.md ("Recursive
 * descent and the stack") says how it was measured.
 */
#define PARSER_STACK_RESERVE ((uint64_t)24 * 1024)

#if !defined(__GNUC__) && !defined(__clang__) && !defined(_MSC_VER)
// The one compiler-independent way to see where the native stack is: a local's
// address, in a function that cannot be inlined into its caller.
PARSER_NOINLINE uintptr_t parser_stack_here(void) {
  volatile char marker = 0;
  return (uintptr_t)&marker;
}
#endif

/* ---- Failure ------------------------------------------------------------- */

static GLECMA_NORETURN void parser_jump(Parser * p) {
  longjmp(p->jump, 1);
}

void parser_fail(Parser * p, uint32_t rule, const GLECMA_Location * where) {
  if (p->result == GLECMA_OK) {
    p->result = GLECMA_ERR_SYNTAX;
    p->rule = rule;
    p->message = glecma_rule_message(rule);
    p->where = *where;
  }
  parser_jump(p);
}

void parser_fail_token(Parser * p, uint32_t rule) {
  parser_fail(p, rule, &p->tok.start);
}

void parser_unexpected(Parser * p) {
  parser_fail_token(p, p->tok.kind == GLECMA_TOK_EOF
          ? GLECMA_RULE_UNEXPECTED_END
          : GLECMA_RULE_UNEXPECTED_TOKEN);
}

void parser_fail_limit(Parser * p, const char * limit) {
  if (p->result == GLECMA_OK) {
    p->result = GLECMA_ERR_LIMIT;
    p->limit = limit;
    p->message = "a parse limit was exceeded";
    p->where = p->tok.start;
  }
  parser_jump(p);
}

void parser_fail_result(Parser * p, GLECMA_Result result) {
  if (p->result == GLECMA_OK) {
    p->result = result;
    p->message = result == GLECMA_ERR_OOM ? "allocation failed"
                                          : "the parser lost its place";
    p->where = p->tok.start;
  }
  parser_jump(p);
}

// The lexer failed: its error is the parse's.
static GLECMA_NORETURN void parser_lex_failed(Parser * p) {
  if (p->result == GLECMA_OK) {
    p->result = p->lex.error.result;
    p->rule = p->lex.error.rule;
    p->message = p->lex.error.message;
    p->where = p->lex.error.location;
    p->limit = p->lex.error.limit;
    if (p->result == GLECMA_OK) {
      p->result = GLECMA_ERR_INTERNAL;
    }
  }
  parser_jump(p);
}

/* ---- Tokens -------------------------------------------------------------- */

void parser_next(Parser * p) {
  p->prev_end = p->tok.end.offset;
  if (glecma_lexer_next(&p->lex, GLECMA_GOAL_DIV, &p->tok) != GLECMA_OK) {
    parser_lex_failed(p);
  }
}

bool parser_peek(Parser * p, GLECMA_Token * out) {
  GLECMA_Lexer copy = p->lex;
  return glecma_lexer_next(&copy, GLECMA_GOAL_DIV, out) == GLECMA_OK;
}

bool parser_peek2(Parser * p, GLECMA_Token * first, GLECMA_Token * second) {
  GLECMA_Lexer copy = p->lex;
  return glecma_lexer_next(&copy, GLECMA_GOAL_DIV, first) == GLECMA_OK &&
      glecma_lexer_next(&copy, GLECMA_GOAL_DIV, second) == GLECMA_OK;
}

bool token_starts_property_name(const GLECMA_Token * t) {
  switch (t->kind) {
    case GLECMA_TOK_NAME: case GLECMA_TOK_STRING: case GLECMA_TOK_NUMBER:
    case GLECMA_TOK_BIGINT: case GLECMA_TOK_LBRACKET:
    case GLECMA_TOK_PRIVATE_NAME:
      return true;
    default:
      return false;
  }
}

void parser_rescan(Parser * p, uint32_t goal) {
  uint32_t newline = p->tok.flags & GLECMA_TOKF_NEWLINE_BEFORE;
  GLECMA_Location from = p->tok.start;
  glecma_lexer_reset(&p->lex, &from);
  p->lex.tokens--; // the token being read again is counted once
  if (glecma_lexer_next(&p->lex, goal, &p->tok) != GLECMA_OK) {
    parser_lex_failed(p);
  }
  p->tok.flags |= newline;
}

void parser_expect(Parser * p, GLECMA_TokenKind kind) {
  if (p->tok.kind != kind) {
    parser_unexpected(p);
  }
  parser_next(p);
}

bool parser_eat(Parser * p, GLECMA_TokenKind kind) {
  if (p->tok.kind != kind) {
    return false;
  }
  parser_next(p);
  return true;
}

void parser_semicolon(Parser * p) {
  if (p->tok.kind == GLECMA_TOK_SEMICOLON) {
    parser_next(p);
    return;
  }
  if (p->tok.kind == GLECMA_TOK_RBRACE || p->tok.kind == GLECMA_TOK_EOF ||
      (p->tok.flags & GLECMA_TOKF_NEWLINE_BEFORE) != 0) {
    return;
  }
  parser_fail_token(p, GLECMA_RULE_MISSING_SEMICOLON);
}

void parser_set_strict(Parser * p, bool strict) {
  if (strict) {
    p->flags |= PF_STRICT;
  } else {
    p->flags &= ~PF_STRICT;
  }
  glecma_lexer_set_strict(&p->lex, strict);
}

/* ---- Snapshots ------------------------------------------------------------ */

void parser_snap(const Parser * p, Snap * snap) {
  snap->lex = p->lex;
  snap->tok = p->tok;
  snap->prev_end = p->prev_end;
  p->builder->ops->mark(p->builder, &snap->mark);
  snap->nodes = p->nodes;
  snap->kids = p->kids_len;
  snap->pending = p->pending;
  snap->pending_at = p->pending_at;
  snap->pending_rule = p->pending_rule;
  snap->labels = p->label_len;
  snap->priv_refs = p->priv_ref_len;
  snap->priv_decls = p->priv_decl_len;
  snap->classes = p->class_len;
  snap->recs = p->rec_len;
  snap->frames = p->frame_len;
}

void parser_restore(Parser * p, const Snap * snap) {
  p->lex = snap->lex;
  p->tok = snap->tok;
  p->prev_end = snap->prev_end;
  p->builder->ops->rewind(p->builder, &snap->mark);
  p->nodes = snap->nodes;
  p->kids_len = snap->kids;
  p->pending = snap->pending;
  p->pending_at = snap->pending_at;
  p->pending_rule = snap->pending_rule;
  p->label_len = snap->labels;
  p->priv_ref_len = snap->priv_refs;
  p->priv_decl_len = snap->priv_decls;
  p->class_len = snap->classes;
  p->rec_len = snap->recs;
  p->frame_len = snap->frames;
}

/* ---- Depth and the native stack ------------------------------------------- */

// A level of nesting: counted against the nesting cap, and the native stack
// is checked. The recursive functions that are not a level of syntactic
// nesting of their own (the layers of one expression) check the stack only.
void parser_enter(Parser * p) {
  if (++p->depth > p->max_nesting) {
    parser_fail_limit(p, "nesting");
  }
  parser_check_stack(p);
}

void parser_check_stack(Parser * p) {
  parser_check_stack_for(p, 0);
}

// The same, for a callee that needs `extra` bytes more than the reserve.
void parser_check_stack_for(Parser * p, uint64_t extra) {
  uintptr_t here = PARSER_STACK_POSITION();
  uintptr_t used = p->stack_base > here ? p->stack_base - here
                                        : here - p->stack_base;
  if ((uint64_t)used + PARSER_STACK_RESERVE + extra > p->native_stack_bytes) {
    parser_fail_limit(p, "native_stack_bytes");
  }
}

void parser_leave(Parser * p) {
  p->depth--;
}

// A binding's name, checked in the strictness the code has now: `eval` and
// `arguments` cannot be bound in strict code (clause 13.1.1). In sloppy code
// a binding that strict code would refuse is remembered (the first), in case
// a directive later in the same function turns the code it is part of into
// strict code.
void parser_note_binding(Parser * p, const GLECMA_Token * name) {
  const bool restricted = name->keyword == GLECMA_KW_EVAL ||
      name->keyword == GLECMA_KW_ARGUMENTS;
  if ((p->flags & PF_STRICT) != 0) {
    if (restricted) {
      parser_fail(p, GLECMA_RULE_BINDING_EVAL_ARGUMENTS, &name->start);
    }
    return;
  }
  if (p->retro.rule != GLECMA_RULE_NONE) {
    return;
  }
  uint32_t rule = glecma_token_identifier_check(name, parser_idf(p) | GLECMA_IDF_STRICT);
  if (rule == GLECMA_RULE_NONE && restricted) {
    rule = GLECMA_RULE_BINDING_EVAL_ARGUMENTS;
  }
  if (rule != GLECMA_RULE_NONE) {
    p->retro.rule = rule;
    p->retro.at = name->start;
  }
}

// A function has its own bindings to answer for: the enclosing function's
// are set aside while it is read and put back after.
Retro parser_retro_enter(Parser * p) {
  Retro saved = p->retro;
  p->retro.rule = GLECMA_RULE_NONE;
  return saved;
}

void parser_retro_leave(Parser * p, Retro saved) {
  p->retro = saved;
}

/* ---- Arrow memo ----------------------------------------------------------- */

static size_t memo_slot(size_t key, size_t capacity) {
  return (key * (size_t)0x9E3779B97F4A7C15ull >> 7) & (capacity - 1);
}

void parser_memo_add(Parser * p, size_t key) {
  if ((p->memo_count + 1) * 2 > p->memo_cap) {
    size_t capacity = p->memo_cap == 0 ? 64 : p->memo_cap * 2;
    size_t * table = (size_t *)p->allocator->calloc_fn(p->allocator->ctx,
        capacity, sizeof(size_t));
    if (table == NULL) {
      parser_fail_result(p, GLECMA_ERR_OOM);
    }
    for (size_t i = 0; i < p->memo_cap; i++) {
      if (p->memo[i] != 0) {
        size_t slot = memo_slot(p->memo[i], capacity);
        while (table[slot] != 0) {
          slot = (slot + 1) & (capacity - 1);
        }
        table[slot] = p->memo[i];
      }
    }
    if (p->memo != NULL) {
      p->allocator->free_fn(p->allocator->ctx, p->memo);
    }
    p->memo = table;
    p->memo_cap = capacity;
  }
  size_t stored = key + 1; // zero marks an empty slot
  size_t slot = memo_slot(stored, p->memo_cap);
  while (p->memo[slot] != 0) {
    if (p->memo[slot] == stored) {
      return;
    }
    slot = (slot + 1) & (p->memo_cap - 1);
  }
  p->memo[slot] = stored;
  p->memo_count++;
}

bool parser_memo_has(const Parser * p, size_t key) {
  if (p->memo_cap == 0) {
    return false;
  }
  size_t stored = key + 1;
  size_t slot = memo_slot(stored, p->memo_cap);
  while (p->memo[slot] != 0) {
    if (p->memo[slot] == stored) {
      return true;
    }
    slot = (slot + 1) & (p->memo_cap - 1);
  }
  return false;
}

/* ---- Sets of interned names ------------------------------------------------ */

static size_t ptr_slot(const uint16_t * key, size_t mask) {
  // 64-bit arithmetic on every target: a 32-bit uintptr_t would truncate the
  // multiplier and leave 12 bits of index, whatever the table's size.
  uint64_t h = (uint64_t)(uintptr_t)key >> 4;
  h *= UINT64_C(0x9E3779B97F4A7C15);
  return (size_t)(h >> 32) & mask;
}

// Adds a key; false when it was already there.
bool parser_ptrset_add(Parser * p, PtrSet * set, const uint16_t * key) {
  if ((set->count + 1) * 2 > set->capacity) {
    size_t capacity = set->capacity == 0 ? 16 : set->capacity * 2;
    if (capacity > SIZE_MAX / sizeof(*set->keys)) {
      parser_fail_result(p, GLECMA_ERR_OOM);
    }
    const uint16_t ** keys = (const uint16_t **)p->allocator->calloc_fn(
        p->allocator->ctx, capacity, sizeof(*keys));
    if (keys == NULL) {
      parser_fail_result(p, GLECMA_ERR_OOM);
    }
    for (size_t i = 0; i < set->capacity; i++) {
      if (set->keys[i] != NULL) {
        size_t slot = ptr_slot(set->keys[i], capacity - 1);
        while (keys[slot] != NULL) {
          slot = (slot + 1) & (capacity - 1);
        }
        keys[slot] = set->keys[i];
      }
    }
    if (set->keys != NULL) {
      p->allocator->free_fn(p->allocator->ctx, (void *)set->keys);
    }
    set->keys = keys;
    set->capacity = capacity;
  }
  size_t mask = set->capacity - 1;
  size_t slot = ptr_slot(key, mask);
  while (set->keys[slot] != NULL) {
    if (set->keys[slot] == key) {
      return false;
    }
    slot = (slot + 1) & mask;
  }
  set->keys[slot] = key;
  set->count++;
  return true;
}

void parser_ptrset_release(Parser * p, PtrSet * set) {
  if (set->keys != NULL) {
    p->allocator->free_fn(p->allocator->ctx, (void *)set->keys);
  }
  memset(set, 0, sizeof(*set));
}

// Empties a set for the next use, without a cost out of proportion to what it
// held: a big one is released, a small one is wiped.
void parser_ptrset_clear(Parser * p, PtrSet * set) {
  if (set->capacity > 64) {
    parser_ptrset_release(p, set);
    return;
  }
  if (set->keys != NULL) {
    memset((void *)set->keys, 0, set->capacity * sizeof(*set->keys));
  }
  set->count = 0;
}

// Whether a key is in the set.
bool parser_ptrset_has(const PtrSet * set, const uint16_t * key) {
  if (set->keys == NULL) {
    return false;
  }
  size_t mask = set->capacity - 1;
  size_t slot = ptr_slot(key, mask);
  while (set->keys[slot] != NULL) {
    if (set->keys[slot] == key) {
      return true;
    }
    slot = (slot + 1) & mask;
  }
  return false;
}

/* ---- Nodes, text and events ----------------------------------------------- */

void parser_kid(Parser * p, GLECMA_BNode node) {
  if (p->kids_len == p->kids_cap) {
    size_t capacity = p->kids_cap == 0 ? 64 : p->kids_cap * 2;
    if (capacity > SIZE_MAX / sizeof(GLECMA_BNode)) {
      parser_fail_result(p, GLECMA_ERR_OOM);
    }
    GLECMA_BNode * grown = (GLECMA_BNode *)p->allocator->realloc_fn(
        p->allocator->ctx, p->kids, capacity * sizeof(GLECMA_BNode));
    if (grown == NULL) {
      parser_fail_result(p, GLECMA_ERR_OOM);
    }
    p->kids = grown;
    p->kids_cap = capacity;
  }
  p->kids[p->kids_len++] = node;
}

static void count_node(Parser * p) {
  if (++p->nodes > p->max_nodes) {
    parser_fail_limit(p, "nodes");
  }
}

GLECMA_BNode parser_make(Parser * p, uint32_t kind, uint32_t flags,
    uint32_t op, size_t start, size_t end, size_t kids_mark) {
  count_node(p);
  GLECMA_BSpec spec;
  memset(&spec, 0, sizeof(spec));
  spec.kind = kind;
  spec.flags = flags;
  spec.op = op;
  spec.start = start;
  spec.end = end;
  // No pointer arithmetic on the children array before it exists: a node with no
  // child may be the first thing the parse makes.
  spec.children = p->kids != NULL ? p->kids + kids_mark : NULL;
  spec.child_count = p->kids_len - kids_mark;
  GLECMA_BNode node = NULL;
  GLECMA_Result r = p->builder->ops->node(p->builder, &spec, &node);
  p->kids_len = kids_mark;
  if (r != GLECMA_OK) {
    parser_fail_result(p, r);
  }
  return node;
}

// A function body's node, through the builder's body protocol (kind 0 is an
// expression body: the one child is the body).
GLECMA_BNode parser_make_body(Parser * p, uint32_t kind, uint32_t flags,
    size_t start, size_t end, size_t kids_mark) {
  count_node(p);
  GLECMA_BSpec spec;
  memset(&spec, 0, sizeof(spec));
  spec.kind = kind;
  spec.flags = flags;
  spec.start = start;
  spec.end = end;
  // No pointer arithmetic on the children array before it exists: a node with no
  // child may be the first thing the parse makes.
  spec.children = p->kids != NULL ? p->kids + kids_mark : NULL;
  spec.child_count = p->kids_len - kids_mark;
  GLECMA_BNode node = NULL;
  GLECMA_Result r = p->builder->ops->body_end(p->builder, &spec, &node);
  p->kids_len = kids_mark;
  if (r != GLECMA_OK) {
    parser_fail_result(p, r);
  }
  return node;
}

GLECMA_BNode parser_make_text(Parser * p, uint32_t kind, uint32_t flags,
    size_t start, size_t end, const GLECMA_BText * text0,
    const GLECMA_BText * text1) {
  count_node(p);
  GLECMA_BSpec spec;
  memset(&spec, 0, sizeof(spec));
  spec.kind = kind;
  spec.flags = flags;
  spec.start = start;
  spec.end = end;
  if (text0 != NULL) {
    spec.text[spec.text_count++] = *text0;
  }
  if (text1 != NULL) {
    spec.text[spec.text_count++] = *text1;
  }
  GLECMA_BNode node = NULL;
  GLECMA_Result r = p->builder->ops->node(p->builder, &spec, &node);
  if (r != GLECMA_OK) {
    parser_fail_result(p, r);
  }
  return node;
}

bool parser_materializing(const Parser * p) {
  return p->builder->ops->materializing(p->builder);
}

#define SMALL_UNITS 256u
_Static_assert(SMALL_UNITS == sizeof(((Parser *)0)->scratch) / sizeof(uint16_t),
    "the scratch buffer is SMALL_UNITS long");

void parser_text(Parser * p, const GLECMA_Token * token, GLECMA_ValueKind kind,
    bool intern, GLECMA_BText * out) {
  uint16_t * const small = p->scratch;
  uint16_t * buffer = small;
  size_t span = token->end.offset - token->start.offset;
  if (span > SMALL_UNITS) {
    buffer = (uint16_t *)p->allocator->malloc_fn(p->allocator->ctx,
        span * sizeof(uint16_t));
    if (buffer == NULL) {
      parser_fail_result(p, GLECMA_ERR_OOM);
    }
  }
  size_t length = 0;
  GLECMA_Result r = glecma_token_value_into(p->source, token, kind, buffer,
      span, &length);
  GLECMA_Result made = GLECMA_ERR_INTERNAL;
  if (r == GLECMA_OK) {
    made = p->builder->ops->text(p->builder, buffer, length, intern, out);
  }
  if (buffer != small) {
    p->allocator->free_fn(p->allocator->ctx, buffer);
  }
  if (r != GLECMA_OK) {
    parser_fail_result(p, GLECMA_ERR_INTERNAL);
  }
  if (made != GLECMA_OK) {
    parser_fail_result(p, made);
  }
}

// The source's text between two offsets as UTF-16, for the pattern and flags
// of a regular expression literal.
void parser_slice(Parser * p, size_t from, size_t to, GLECMA_BText * out) {
  size_t span = to - from;
  uint16_t * const small = p->scratch;
  uint16_t * buffer = small;
  if (span > SMALL_UNITS) {
    buffer = (uint16_t *)p->allocator->malloc_fn(p->allocator->ctx,
        span * sizeof(uint16_t));
    if (buffer == NULL) {
      parser_fail_result(p, GLECMA_ERR_OOM);
    }
  }
  size_t length = 0;
  size_t offset = from;
  while (offset < to) {
    size_t unit_length = 0;
    uint32_t cp = glecma_lex_cp(&p->text, offset, &unit_length);
    if (cp == GLECMA_LEX_NONE || cp == GLECMA_LEX_BAD) {
      break;
    }
    if (cp > 0xFFFFu) {
      buffer[length++] = (uint16_t)(0xD800u + ((cp - 0x10000u) >> 10));
      buffer[length++] = (uint16_t)(0xDC00u + ((cp - 0x10000u) & 0x3FFu));
    } else {
      buffer[length++] = (uint16_t)cp;
    }
    offset += unit_length;
  }
  GLECMA_Result made = p->builder->ops->text(p->builder, buffer, length, false,
      out);
  if (buffer != small) {
    p->allocator->free_fn(p->allocator->ctx, buffer);
  }
  if (offset < to) {
    parser_fail_result(p, GLECMA_ERR_INTERNAL);
  }
  if (made != GLECMA_OK) {
    parser_fail_result(p, made);
  }
}

void parser_event(Parser * p, uint32_t type, uint32_t what,
    const GLECMA_BText * name, size_t start, size_t end) {
  GLECMA_BEvent event;
  memset(&event, 0, sizeof(event));
  event.type = type;
  event.what = what;
  if (name != NULL) {
    event.name = *name;
  }
  event.start = start;
  event.end = end;
  // The early errors see the scopes and declarations as the builder does.
  if (type == GLECMA_BEV_SCOPE_ENTER) {
    early_scope_enter(p, what);
  } else if (type == GLECMA_BEV_SCOPE_LEAVE) {
    early_scope_leave(p, what);
  } else if (type == GLECMA_BEV_DECLARE) {
    early_declare(p, what, name, start);
  }
  GLECMA_Result r = p->builder->ops->event(p->builder, &event);
  if (r != GLECMA_OK) {
    parser_fail_result(p, r);
  }
}

void parser_amend_reference(Parser * p, size_t start, uint32_t add,
    uint32_t clear) {
  p->builder->ops->amend(p->builder, start, add, clear);
}

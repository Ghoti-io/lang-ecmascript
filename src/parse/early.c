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
 * The early errors that need what lies around the code being read, which the
 * grammar does not decide: labels, `break` and `continue` targets, and the
 * private names of a class and the references to them.
 *
 * Like every check in the parser, these read the parser's own records and
 * never a node (builder.h): a body the checking builder did not build is
 * checked exactly as a built one is. Every stack here is append-only within a
 * construct and is cut back to a mark when the parser rewinds (a ::Snap), so
 * a cover grammar that reads a construct twice leaves one set of records.
 */

#include <ghoti.io/lang-ecmascript/macros.h>

#include "parse/parser.h"

#include <stdlib.h>
#include <string.h>

/* ---- Growing the stacks ----------------------------------------------------- */

// Room for one more element of a stack; OOM is the parse's failure.
static void * stack_room(Parser * p, void * array, size_t length, size_t * capacity,
    size_t element) {
  if (length < *capacity) {
    return array;
  }
  size_t grown = *capacity == 0 ? 16 : *capacity * 2;
  if (grown > SIZE_MAX / element) {
    parser_fail_result(p, GLECMA_ERR_OOM);
  }
  void * result = p->allocator->realloc_fn(p->allocator->ctx, array,
      grown * element);
  if (result == NULL) {
    parser_fail_result(p, GLECMA_ERR_OOM);
  }
  *capacity = grown;
  return result;
}

void early_free(Parser * p) {
  parser_ptrset_release(p, &p->outer_names);
  void * stacks[] = {p->labels, p->priv_decls, p->priv_refs, p->classes,
      p->recs, p->frames};
  for (size_t i = 0; i < sizeof stacks / sizeof stacks[0]; i++) {
    if (stacks[i] != NULL) {
      p->allocator->free_fn(p->allocator->ctx, stacks[i]);
    }
  }
}

/* ---- Token values ------------------------------------------------------------- */

bool token_value_is(Parser * p, const GLECMA_Token * token, const char * ascii) {
  size_t length = strlen(ascii);
  size_t span = token->end.offset - token->start.offset;
  if (span < length) {
    return false; // a value is never longer than the text it came from
  }
  uint16_t * const small = p->scratch;
  uint16_t * buffer = small;
  if (span > sizeof p->scratch / sizeof p->scratch[0]) {
    buffer = (uint16_t *)p->allocator->malloc_fn(p->allocator->ctx,
        span * sizeof(uint16_t));
    if (buffer == NULL) {
      parser_fail_result(p, GLECMA_ERR_OOM);
    }
  }
  size_t got = 0;
  GLECMA_Result r = glecma_token_value_into(p->source, token,
      GLECMA_VALUE_COOKED, buffer, span, &got);
  bool same = r == GLECMA_OK && got == length;
  for (size_t i = 0; same && i < length; i++) {
    same = buffer[i] == (uint16_t)(unsigned char)ascii[i];
  }
  if (buffer != small) {
    p->allocator->free_fn(p->allocator->ctx, buffer);
  }
  return same;
}

/* ---- Names ---------------------------------------------------------------------- */

// `arguments` is not a name inside a class field initializer or static block,
// arrow functions in them included (clause 15.7.1).
void early_check_arguments(Parser * p, const GLECMA_Token * name) {
  if ((p->flags & PF_NO_ARGUMENTS) != 0) {
    parser_fail(p, GLECMA_RULE_ARGUMENTS_IN_INITIALIZER, &name->start);
  }
}

/* ---- Jump targets (clauses 14.8.1, 14.9.1, 14.13.1) ---------------------------- */

JumpCtx early_jump_enter(Parser * p) {
  JumpCtx saved = {p->loops, p->breakables, p->label_chain, p->label_base};
  p->loops = 0;
  p->breakables = 0;
  p->label_chain = 0;
  p->label_base = p->label_len;
  return saved;
}

void early_jump_leave(Parser * p, JumpCtx saved) {
  p->label_len = p->label_base;
  p->loops = saved.loops;
  p->breakables = saved.breakables;
  p->label_chain = saved.chain;
  p->label_base = saved.label_base;
}

void early_label_push(Parser * p, const GLECMA_BText * name,
    const GLECMA_Location * at) {
  // A label may not be redeclared inside the statement it labels, within the
  // function: the same name is fine in a function that is nested in it.
  for (size_t i = p->label_len; i > p->label_base; i--) {
    if (p->labels[i - 1].name == name->units) {
      parser_fail(p, GLECMA_RULE_LABEL_REDECLARED, at);
    }
  }
  p->labels = (Label *)stack_room(p, p->labels, p->label_len, &p->label_cap,
      sizeof(Label));
  p->labels[p->label_len].name = name->units;
  p->labels[p->label_len].loop = false;
  p->label_len++;
}

void early_label_pop(Parser * p) {
  p->label_len--;
}

// The `count` labels nearest the top label an iteration statement.
void early_label_loops(Parser * p, uint32_t count) {
  for (size_t i = p->label_len; count > 0 && i > p->label_base; i--, count--) {
    p->labels[i - 1].loop = true;
  }
}

void early_break(Parser * p, const GLECMA_BText * label, const GLECMA_Location * at) {
  if (label == NULL) {
    if (p->breakables == 0) {
      parser_fail(p, GLECMA_RULE_BREAK_TARGET, at);
    }
    return;
  }
  for (size_t i = p->label_len; i > p->label_base; i--) {
    if (p->labels[i - 1].name == label->units) {
      return;
    }
  }
  parser_fail(p, GLECMA_RULE_BREAK_TARGET, at);
}

void early_continue(Parser * p, const GLECMA_BText * label,
    const GLECMA_Location * at) {
  if (label == NULL) {
    if (p->loops == 0) {
      parser_fail(p, GLECMA_RULE_CONTINUE_TARGET, at);
    }
    return;
  }
  for (size_t i = p->label_len; i > p->label_base; i--) {
    if (p->labels[i - 1].name == label->units) {
      if (p->labels[i - 1].loop) {
        return;
      }
      break;
    }
  }
  parser_fail(p, GLECMA_RULE_CONTINUE_TARGET, at);
}

/* ---- Private names (clause 15.7.1) ----------------------------------------------- */

void early_class_enter(Parser * p) {
  p->classes = (ClassFrame *)stack_room(p, p->classes, p->class_len,
      &p->class_cap, sizeof(ClassFrame));
  p->classes[p->class_len].decl_first = p->priv_decl_len;
  p->classes[p->class_len].ref_first = p->priv_ref_len;
  p->class_len++;
}

void early_private_declare(Parser * p, const GLECMA_BText * name,
    const GLECMA_Location * at, uint32_t kind, bool is_static) {
  p->priv_decls = (PrivDecl *)stack_room(p, p->priv_decls, p->priv_decl_len,
      &p->priv_decl_cap, sizeof(PrivDecl));
  PrivDecl * d = &p->priv_decls[p->priv_decl_len++];
  d->name = name->units;
  d->offset = at->offset;
  d->kind = (uint8_t)kind;
  d->is_static = is_static;
}

void early_private_reference(Parser * p, const GLECMA_BText * name,
    const GLECMA_Location * at) {
  p->priv_refs = (PrivRef *)stack_room(p, p->priv_refs, p->priv_ref_len,
      &p->priv_ref_cap, sizeof(PrivRef));
  p->priv_refs[p->priv_ref_len].name = name->units;
  p->priv_refs[p->priv_ref_len].at = *at;
  p->priv_ref_len++;
}

// What one class declared of a name: where the first declaration is, and how
// many there were. A table sized to the class's declarations, so the check
// costs what the declarations cost.
typedef struct NameTable {
  const uint16_t ** keys;
  uint32_t * first; ///< The first declaration of the name, plus one.
  uint32_t * count;
  size_t mask;
} NameTable;

static size_t name_slot(const uint16_t * name, size_t mask) {
  return (size_t)(((uintptr_t)name >> 4) * (uintptr_t)0x9E3779B97F4A7C15ull >> 20) & mask;
}

static void table_free(Parser * p, NameTable * table) {
  if (table->keys != NULL) {
    p->allocator->free_fn(p->allocator->ctx, (void *)table->keys);
  }
  if (table->first != NULL) {
    p->allocator->free_fn(p->allocator->ctx, table->first);
  }
  if (table->count != NULL) {
    p->allocator->free_fn(p->allocator->ctx, table->count);
  }
}

static void table_open(Parser * p, NameTable * table, size_t count) {
  size_t capacity = 8;
  while (capacity < count * 2) {
    capacity *= 2;
  }
  table->keys = (const uint16_t **)p->allocator->calloc_fn(p->allocator->ctx,
      capacity, sizeof(const uint16_t *));
  table->first = (uint32_t *)p->allocator->calloc_fn(p->allocator->ctx,
      capacity, sizeof(uint32_t));
  table->count = (uint32_t *)p->allocator->calloc_fn(p->allocator->ctx,
      capacity, sizeof(uint32_t));
  table->mask = capacity - 1;
  if (table->keys == NULL || table->first == NULL || table->count == NULL) {
    table_free(p, table);
    parser_fail_result(p, GLECMA_ERR_OOM);
  }
}

// The references from `first` on are what no class of the code itself declares:
// each must be a private name of the code around, or it is an error.
static void early_outer_answer(Parser * p, size_t first) {
  for (size_t i = first; i < p->priv_ref_len; i++) {
    if (!parser_ptrset_has(&p->outer_names, p->priv_refs[i].name)) {
      parser_fail(p, GLECMA_RULE_PRIVATE_NAME_UNDECLARED, &p->priv_refs[i].at);
    }
  }
  p->priv_ref_len = first;
}

// The class's declarations are checked for duplicates, and its references are
// matched to them; what no declaration answers belongs to the class around,
// and to no class at all when this is the outermost.
void early_class_leave(Parser * p) {
  const ClassFrame frame = p->classes[--p->class_len];
  const size_t count = p->priv_decl_len - frame.decl_first;
  const PrivDecl * decls = p->priv_decls + frame.decl_first;
  NameTable table = {NULL, NULL, NULL, 0};
  if (count > 0) {
    table_open(p, &table, count);
  }
  size_t duplicate = SIZE_MAX; // the first declaration that repeats a name
  for (size_t i = 0; i < count && duplicate == SIZE_MAX; i++) {
    size_t slot = name_slot(decls[i].name, table.mask);
    while (table.keys[slot] != NULL && table.keys[slot] != decls[i].name) {
      slot = (slot + 1) & table.mask;
    }
    if (table.keys[slot] == NULL) {
      table.keys[slot] = decls[i].name;
      table.first[slot] = (uint32_t)(i + 1);
      table.count[slot] = 1;
      continue;
    }
    // A getter and a setter, both static or both not, may share a name.
    const PrivDecl * first = &decls[table.first[slot] - 1];
    const bool pair = table.count[slot] == 1 &&
        ((first->kind == PRIV_GETTER && decls[i].kind == PRIV_SETTER) ||
            (first->kind == PRIV_SETTER && decls[i].kind == PRIV_GETTER)) &&
        first->is_static == decls[i].is_static;
    if (!pair) {
      duplicate = i;
    }
    table.count[slot]++;
  }
  GLECMA_Location duplicate_at = {0, 0, 0};
  if (duplicate != SIZE_MAX) {
    (void)glecma_source_location(p->source, decls[duplicate].offset,
        &duplicate_at);
  }
  // The references made inside this class: those a declaration answers are
  // settled, the rest are kept for the class around.
  size_t kept = frame.ref_first;
  for (size_t i = frame.ref_first; i < p->priv_ref_len; i++) {
    bool found = false;
    if (table.keys != NULL) {
      size_t slot = name_slot(p->priv_refs[i].name, table.mask);
      while (table.keys[slot] != NULL) {
        if (table.keys[slot] == p->priv_refs[i].name) {
          found = true;
          break;
        }
        slot = (slot + 1) & table.mask;
      }
    }
    if (!found) {
      p->priv_refs[kept++] = p->priv_refs[i];
    }
  }
  p->priv_ref_len = kept;
  p->priv_decl_len = frame.decl_first;
  table_free(p, &table);
  if (duplicate != SIZE_MAX) {
    parser_fail(p, GLECMA_RULE_PRIVATE_NAME_DUPLICATE, &duplicate_at);
  }
  if (p->class_len == 0 && kept > frame.ref_first) {
    // The outermost class of the code: what none of its classes declares is
    // answered by the names of the code around it, if it has any (direct eval).
    early_outer_answer(p, frame.ref_first);
  }
}

/* ---- Scopes and declarations (clauses 14.2.1, 14.3.1.1, 14.7.5.1, 14.12.1, 14.15.1, 15.1.1, 15.2.1, 15.7.1, 16.1.1) -- */

// Every name a scope declares is a record. A scope's records lie together at
// the end of the stack while it is open, and are checked once, when it ends:
// what clauses 14.2.1 and the others say about LexicallyDeclaredNames and
// VarDeclaredNames is a statement about the whole scope, which is only known
// then (a directive late in a function body decides whether its parameters may
// repeat). A var declaration is a record of the scope it is written in and
// stays in the stack when that scope ends, so that the scope around sees it
// too, until a function boundary stops it.

#define B_LEX 0x01u
#define B_FUNC 0x02u
#define B_FPLAIN 0x04u  // a function declaration that is no generator and not async
#define B_FOTHER 0x08u
#define B_VAR 0x10u
#define B_PARAM 0x40u
#define B_CATCH_SIMPLE 0x80u
#define B_CATCH_PATTERN 0x100u

static uint32_t record_bits(uint8_t kind) {
  switch (kind) {
    case SR_LEX: return B_LEX;
    case SR_FUNC: return B_FUNC | B_FPLAIN;
    case SR_FUNC_OTHER: return B_FUNC | B_FOTHER;
    case SR_VAR: return B_VAR;
    case SR_PARAM: return B_PARAM;
    case SR_CATCH_SIMPLE: return B_CATCH_SIMPLE;
    default: return B_CATCH_PATTERN;
  }
}

typedef enum { OK_NO_CONFLICT, CONFLICT_REDECLARED, CONFLICT_PARAMETER } Conflict;

// Whether a declaration of kind `kind` clashes with what the scope already
// holds under the same name (`have`), by the rules of the scope's kind.
static Conflict conflict_in(const Frame * f, uint32_t have, uint8_t kind) {
  const uint32_t catches = B_CATCH_SIMPLE | B_CATCH_PATTERN;
  const bool plain = kind == SR_FUNC;
  switch (f->kind) {
    case GLECMA_BSCOPE_BLOCK:
    case SCOPE_SWITCH:
      // Annex B.3.2.4, B.3.2.5: in sloppy code a block may declare one plain
      // function twice.
      if (kind == SR_LEX) {
        return (have & (B_LEX | B_FUNC | B_VAR)) != 0 ? CONFLICT_REDECLARED : OK_NO_CONFLICT;
      }
      if (kind == SR_FUNC || kind == SR_FUNC_OTHER) {
        if ((have & (B_LEX | B_VAR)) != 0) {
          return CONFLICT_REDECLARED;
        }
        if ((have & B_FUNC) != 0 &&
            (f->strict || !plain || (have & B_FOTHER) != 0)) {
          return CONFLICT_REDECLARED;
        }
        return OK_NO_CONFLICT;
      }
      return (have & (B_LEX | B_FUNC)) != 0 ? CONFLICT_REDECLARED : OK_NO_CONFLICT;
    case GLECMA_BSCOPE_CATCH:
      // Annex B.3.4: a simple catch parameter may be redeclared by a var.
      if (kind == SR_CATCH_SIMPLE || kind == SR_CATCH_PATTERN) {
        return (have & catches) != 0 ? CONFLICT_REDECLARED : OK_NO_CONFLICT;
      }
      if (kind == SR_LEX) {
        return (have & (B_LEX | B_FUNC | B_VAR | catches)) != 0
            ? CONFLICT_REDECLARED : OK_NO_CONFLICT;
      }
      if (kind == SR_FUNC || kind == SR_FUNC_OTHER) {
        if ((have & (B_LEX | B_VAR | catches)) != 0) {
          return CONFLICT_REDECLARED;
        }
        if ((have & B_FUNC) != 0 &&
            (f->strict || !plain || (have & B_FOTHER) != 0)) {
          return CONFLICT_REDECLARED;
        }
        return OK_NO_CONFLICT;
      }
      return (have & (B_LEX | B_FUNC | B_CATCH_PATTERN)) != 0
          ? CONFLICT_REDECLARED : OK_NO_CONFLICT;
    case GLECMA_BSCOPE_FOR:
      if (kind == SR_LEX) {
        return (have & (B_LEX | B_VAR)) != 0 ? CONFLICT_REDECLARED : OK_NO_CONFLICT;
      }
      return (have & B_LEX) != 0 ? CONFLICT_REDECLARED : OK_NO_CONFLICT;
    default: {
      // A script, a function or a static block: its top-level functions are
      // var-like, unless the top level is a module's.
      const bool functions_lexical = f->kind == GLECMA_BSCOPE_MODULE;
      switch (kind) {
        case SR_LEX:
          return (have & (B_LEX | B_FUNC | B_VAR | B_PARAM)) != 0
              ? CONFLICT_REDECLARED : OK_NO_CONFLICT;
        case SR_FUNC:
        case SR_FUNC_OTHER:
          return (have & (B_LEX | (functions_lexical ? (B_FUNC | B_VAR) : 0u))) != 0
              ? CONFLICT_REDECLARED : OK_NO_CONFLICT;
        case SR_PARAM:
          return (have & B_PARAM) != 0 && f->params_unique ? CONFLICT_PARAMETER
                                                           : OK_NO_CONFLICT;
        default:
          return (have & (B_LEX | (functions_lexical ? B_FUNC : 0u))) != 0
              ? CONFLICT_REDECLARED : OK_NO_CONFLICT;
      }
    }
  }
}

static uint32_t redeclaration_rule(const Frame * f) {
  switch (f->kind) {
    case GLECMA_BSCOPE_BLOCK: return GLECMA_RULE_BLOCK_REDECLARATION;
    case SCOPE_SWITCH: return GLECMA_RULE_SWITCH_REDECLARATION;
    case GLECMA_BSCOPE_CATCH: return GLECMA_RULE_CATCH_REDECLARATION;
    case GLECMA_BSCOPE_FOR: return GLECMA_RULE_FOR_REDECLARATION;
    case GLECMA_BSCOPE_STATIC_BLOCK: return GLECMA_RULE_STATIC_BLOCK_REDECLARATION;
    case GLECMA_BSCOPE_SCRIPT: return GLECMA_RULE_SCRIPT_REDECLARATION;
    case GLECMA_BSCOPE_MODULE: return GLECMA_RULE_MODULE_REDECLARATION;
    default: return GLECMA_RULE_FUNCTION_REDECLARATION;
  }
}

static bool frame_keeps_vars(uint32_t kind) {
  switch (kind) {
    case GLECMA_BSCOPE_BLOCK:
    case SCOPE_SWITCH:
    case GLECMA_BSCOPE_CATCH:
    case GLECMA_BSCOPE_FOR:
    case GLECMA_BSCOPE_WITH:
    case GLECMA_BSCOPE_CLASS:
      return true;
    default:
      return false;
  }
}

static GLECMA_Location location_of(const Parser * p, size_t offset) {
  GLECMA_Location at = {0, 0, 0};
  (void)glecma_source_location(p->source, offset, &at);
  return at;
}

// The records of a scope that is ending, looked at together: the first that
// clashes with an earlier one under the same name is the error.
static void scope_check(Parser * p, const Frame * f) {
  const ScopeRec * recs = p->recs + f->first;
  const size_t count = p->rec_len - f->first;
  if (count < 2) {
    return;
  }
  Conflict found = OK_NO_CONFLICT;
  size_t at = 0;
  if (count <= 16) {
    for (size_t i = 1; i < count && found == OK_NO_CONFLICT; i++) {
      uint32_t have = 0;
      for (size_t j = 0; j < i; j++) {
        if (recs[j].name == recs[i].name) {
          have |= record_bits(recs[j].kind);
        }
      }
      if (have != 0) {
        found = conflict_in(f, have, recs[i].kind);
        at = i;
      }
    }
  } else {
    size_t capacity = 32;
    while (capacity < count * 2) {
      capacity *= 2;
    }
    const uint16_t ** names = (const uint16_t **)p->allocator->calloc_fn(
        p->allocator->ctx, capacity, sizeof(const uint16_t *));
    uint32_t * seen = (uint32_t *)p->allocator->calloc_fn(p->allocator->ctx,
        capacity, sizeof(uint32_t));
    if (names == NULL || seen == NULL) {
      if (names != NULL) {
        p->allocator->free_fn(p->allocator->ctx, (void *)names);
      }
      if (seen != NULL) {
        p->allocator->free_fn(p->allocator->ctx, seen);
      }
      parser_fail_result(p, GLECMA_ERR_OOM);
    }
    const size_t mask = capacity - 1;
    for (size_t i = 0; i < count && found == OK_NO_CONFLICT; i++) {
      size_t slot = name_slot(recs[i].name, mask);
      while (names[slot] != NULL && names[slot] != recs[i].name) {
        slot = (slot + 1) & mask;
      }
      if (names[slot] == NULL) {
        names[slot] = recs[i].name;
      } else {
        found = conflict_in(f, seen[slot], recs[i].kind);
        at = i;
      }
      seen[slot] |= record_bits(recs[i].kind);
    }
    p->allocator->free_fn(p->allocator->ctx, (void *)names);
    p->allocator->free_fn(p->allocator->ctx, seen);
  }
  if (found != OK_NO_CONFLICT) {
    GLECMA_Location where = location_of(p, recs[at].offset);
    parser_fail(p, found == CONFLICT_PARAMETER ? GLECMA_RULE_DUPLICATE_PARAMETER
                                               : redeclaration_rule(f),
        &where);
  }
}

void early_scope_enter(Parser * p, uint32_t kind) {
  if (kind == GLECMA_BSCOPE_BLOCK && p->frame_len > 0) {
    Frame * top = &p->frames[p->frame_len - 1];
    if (top->kind == GLECMA_BSCOPE_CATCH && top->body_pending) {
      // The block of a catch clause shares the clause's scope (clause 14.15.1).
      top->body_pending = false;
      top->body_open = true;
      return;
    }
  }
  p->frames = (Frame *)stack_room(p, p->frames, p->frame_len, &p->frame_cap,
      sizeof(Frame));
  Frame * f = &p->frames[p->frame_len++];
  memset(f, 0, sizeof(*f));
  f->kind = (uint8_t)kind;
  f->strict = (p->flags & PF_STRICT) != 0;
  f->first = p->rec_len;
}

void early_scope_leave(Parser * p, uint32_t kind) {
  Frame * top = &p->frames[p->frame_len - 1];
  if (kind == GLECMA_BSCOPE_BLOCK && top->kind == GLECMA_BSCOPE_CATCH &&
      top->body_open) {
    top->body_open = false;
    return;
  }
  const Frame f = *top;
  p->frame_len--;
  if (f.declared == 0) {
    // Nothing here can clash: a scope with no lexical name, function or
    // parameter of its own passes its var declarations on untouched, which is
    // what makes a deep run of plain blocks cost nothing.
    if (!frame_keeps_vars(f.kind)) {
      p->rec_len = f.first;
    }
    return;
  }
  scope_check(p, &f);
  // What stays for the scope around: the var declarations, until a function
  // boundary; everything else ends here.
  size_t kept = f.first;
  if (frame_keeps_vars(f.kind)) {
    for (size_t i = f.first; i < p->rec_len; i++) {
      if (p->recs[i].kind == SR_VAR) {
        p->recs[kept++] = p->recs[i];
      }
    }
  }
  p->rec_len = kept;
}

// The top scope is the one a switch's case block makes.
void early_scope_switch(Parser * p) {
  p->frames[p->frame_len - 1].kind = SCOPE_SWITCH;
}

// The next block belongs to the catch clause whose scope is open.
void early_scope_catch_body(Parser * p) {
  p->frames[p->frame_len - 1].body_pending = true;
}

// What the function that is ending says about its parameters, known only
// after its body: whether they may repeat is a question of its strictness.
void early_scope_function(Parser * p, bool unique, bool simple, bool strict) {
  Frame * f = &p->frames[p->frame_len - 1];
  f->params_unique = unique || !simple || strict;
}

void early_declare(Parser * p, uint32_t what, const GLECMA_BText * name,
    size_t offset) {
  uint8_t kind;
  switch (what) {
    case GLECMA_BDECL_VAR: kind = SR_VAR; break;
    case GLECMA_BDECL_LET:
    case GLECMA_BDECL_CONST:
    case GLECMA_BDECL_IMPORT:
    case GLECMA_BDECL_CLASS: kind = SR_LEX; break;
    case GLECMA_BDECL_FUNCTION:
      kind = (p->decl_flags & DF_PLAIN_FUNCTION) != 0 ? SR_FUNC : SR_FUNC_OTHER;
      break;
    case GLECMA_BDECL_PARAM: kind = SR_PARAM; break;
    case GLECMA_BDECL_CATCH_PARAM:
      kind = (p->decl_flags & DF_SIMPLE_CATCH) != 0 ? SR_CATCH_SIMPLE
                                                    : SR_CATCH_PATTERN;
      break;
    default:
      return; // a function expression's or a class's own name is in a scope of its own
  }
  p->recs = (ScopeRec *)stack_room(p, p->recs, p->rec_len, &p->rec_cap,
      sizeof(ScopeRec));
  p->recs[p->rec_len].name = name->units;
  p->recs[p->rec_len].offset = offset;
  p->recs[p->rec_len].kind = kind;
  p->rec_len++;
  if (kind != SR_VAR) {
    p->frames[p->frame_len - 1].declared++;
  }
}

// The end of the code: a private name used where no class around it declares
// it is an error (clause 15.7.1). A name used inside a class that does not
// declare it was already refused when that class ended; what is left here was
// used outside every class, or in a class heritage, which sees only the names
// of the classes around the class.
void early_finish(Parser * p) {
  early_outer_answer(p, 0);
}

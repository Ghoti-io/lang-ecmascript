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
  void * stacks[] = {p->labels, p->priv_decls, p->priv_refs, p->classes};
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
    parser_fail(p, GLECMA_RULE_PRIVATE_NAME_UNDECLARED,
        &p->priv_refs[frame.ref_first].at);
  }
}

// The end of the code: a private name used where no class around it declares
// it is an error (clause 15.7.1). A name used inside a class that does not
// declare it was already refused when that class ended; what is left here was
// used outside every class, or in a class heritage, which sees only the names
// of the classes around the class.
void early_finish(Parser * p) {
  if (p->priv_ref_len > 0) {
    parser_fail(p, GLECMA_RULE_PRIVATE_NAME_UNDECLARED, &p->priv_refs[0].at);
  }
}

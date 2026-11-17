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
 * The tree object, its two builders and the public walker.
 *
 * Nodes live in a chunked arena that the tree owns and frees in one pass, so
 * destroying a tree is a walk of its chunks and never recursive. A builder
 * mark is a position in that arena, and a rewind frees the chunks past it.
 * Names are interned into a second arena that a rewind does not touch, so an
 * event recorded before a mark keeps a valid name after one.
 */

#include <ghoti.io/lang-ecmascript/macros.h>

#include <ghoti.io/lang-ecmascript/tree.h>

#include "core/core_internal.h"
#include "parse/builder.h"

#include <stdint.h>
#include <string.h>

/* ---- Records ------------------------------------------------------------- */

struct GLECMA_Node {
  uint32_t kind;
  uint32_t flags;
  uint16_t op;
  uint16_t text_count;
  uint32_t child_count;
  size_t start;
  size_t end;
  // Followed in the arena by `child_count` child pointers and then
  // `text_count` GLECMA_BText records.
};

// The records after a node's header, found by byte offset: the arena is raw
// memory, and a node is followed by its child pointers and then its texts.
static inline const GLECMA_Node * const * node_children(const GLECMA_Node * node) {
  return (const GLECMA_Node * const *)(const void *)((const uint8_t *)node +
      sizeof(GLECMA_Node));
}

static inline const GLECMA_BText * node_texts(const GLECMA_Node * node) {
  return (const GLECMA_BText *)(const void *)((const uint8_t *)node +
      sizeof(GLECMA_Node) + (size_t)node->child_count * sizeof(void *));
}

#define NODE_CHILDREN(node) node_children(node)
#define NODE_TEXTS(node) node_texts(node)

typedef struct Chunk {
  struct Chunk * next;
  size_t used;
  size_t capacity;
  size_t reserved; // keeps the data 32-byte aligned on 64-bit hosts
} Chunk;

typedef struct Arena {
  Chunk * head;
  size_t next_capacity;
} Arena;

#define CHUNK_FIRST ((size_t)16 * 1024)
#define CHUNK_MAX ((size_t)1024 * 1024)
#define ALIGN8(n) (((n) + 7u) & ~(size_t)7u)

struct GLECMA_Tree {
  GLECMA_TreeBuilder builder;
  GLECMA_Allocator allocator;
  GLECMA_Source * source;
  uint32_t mode;
  uint32_t goal;
  bool strict;
  const GLECMA_Node * root;
  Arena nodes;
  Arena texts;
  GLECMA_BEvent * events;
  size_t event_count;
  size_t event_capacity;
  GLECMA_BText * table; // open-addressing table of interned names
  size_t table_capacity;
  size_t table_count;
  uint32_t body_depth;
};

static const uint16_t empty_units[1] = {0};

static inline GLECMA_Tree * tree_of(GLECMA_TreeBuilder * builder) {
  return (GLECMA_Tree *)builder->owner;
}

static inline const GLECMA_Tree * const_tree_of(
    const GLECMA_TreeBuilder * builder) {
  return (const GLECMA_Tree *)builder->owner;
}

/* ---- Arena --------------------------------------------------------------- */

static void * arena_alloc(const GLECMA_Allocator * allocator, Arena * arena,
    size_t size) {
  size = ALIGN8(size);
  if (arena->head == NULL || arena->head->capacity - arena->head->used < size) {
    size_t capacity = arena->next_capacity == 0 ? CHUNK_FIRST
                                                : arena->next_capacity;
    if (capacity < size) {
      capacity = size;
    }
    Chunk * chunk = (Chunk *)allocator->malloc_fn(allocator->ctx,
        sizeof(Chunk) + capacity);
    if (chunk == NULL) {
      return NULL;
    }
    chunk->next = arena->head;
    chunk->used = 0;
    chunk->capacity = capacity;
    chunk->reserved = 0;
    arena->head = chunk;
    arena->next_capacity = capacity < CHUNK_MAX ? capacity * 2 : CHUNK_MAX;
  }
  void * p = (uint8_t *)(arena->head + 1) + arena->head->used;
  arena->head->used += size;
  return p;
}

static void arena_free(const GLECMA_Allocator * allocator, Arena * arena) {
  Chunk * chunk = arena->head;
  while (chunk != NULL) {
    Chunk * next = chunk->next;
    allocator->free_fn(allocator->ctx, chunk);
    chunk = next;
  }
  arena->head = NULL;
}

static void arena_rewind(const GLECMA_Allocator * allocator, Arena * arena,
    Chunk * chunk, size_t used) {
  while (arena->head != chunk) {
    Chunk * next = arena->head->next;
    allocator->free_fn(allocator->ctx, arena->head);
    arena->head = next;
  }
  if (arena->head != NULL) {
    arena->head->used = used;
  }
}

/* ---- Names --------------------------------------------------------------- */

static size_t hash_units(const uint16_t * units, size_t length) {
  uint64_t h = UINT64_C(14695981039346656037);
  for (size_t i = 0; i < length; i++) {
    h ^= units[i];
    h *= UINT64_C(1099511628211);
  }
  return (size_t)(h ^ (h >> 29));
}

static GLECMA_Result table_grow(GLECMA_Tree * tree) {
  size_t capacity = tree->table_capacity == 0 ? 256 : tree->table_capacity * 2;
  GLECMA_BText * table = (GLECMA_BText *)tree->allocator.calloc_fn(
      tree->allocator.ctx, capacity, sizeof(GLECMA_BText));
  if (table == NULL) {
    return GLECMA_ERR_OOM;
  }
  for (size_t i = 0; i < tree->table_capacity; i++) {
    GLECMA_BText entry = tree->table[i];
    if (entry.units == NULL) {
      continue;
    }
    size_t slot = hash_units(entry.units, entry.length) & (capacity - 1);
    while (table[slot].units != NULL) {
      slot = (slot + 1) & (capacity - 1);
    }
    table[slot] = entry;
  }
  if (tree->table != NULL) {
    tree->allocator.free_fn(tree->allocator.ctx, tree->table);
  }
  tree->table = table;
  tree->table_capacity = capacity;
  return GLECMA_OK;
}

static GLECMA_Result copy_units(GLECMA_Tree * tree, const uint16_t * units,
    size_t length, GLECMA_BText * out) {
  if (length == 0) {
    out->units = empty_units;
    out->length = 0;
    return GLECMA_OK;
  }
  uint16_t * copy = (uint16_t *)arena_alloc(&tree->allocator, &tree->texts,
      length * sizeof(uint16_t));
  if (copy == NULL) {
    return GLECMA_ERR_OOM;
  }
  memcpy(copy, units, length * sizeof(uint16_t));
  out->units = copy;
  out->length = length;
  return GLECMA_OK;
}

static GLECMA_Result tree_text(GLECMA_TreeBuilder * builder,
    const uint16_t * units, size_t length, bool intern, GLECMA_BText * out) {
  GLECMA_Tree * tree = tree_of(builder);
  if (!intern || length == 0) {
    return copy_units(tree, units, length, out);
  }
  if ((tree->table_count + 1) * 2 > tree->table_capacity) {
    GLECMA_Result grown = table_grow(tree);
    if (grown != GLECMA_OK) {
      return grown;
    }
  }
  size_t mask = tree->table_capacity - 1;
  size_t slot = hash_units(units, length) & mask;
  while (tree->table[slot].units != NULL) {
    if (tree->table[slot].length == length &&
        memcmp(tree->table[slot].units, units, length * sizeof(uint16_t)) == 0) {
      *out = tree->table[slot];
      return GLECMA_OK;
    }
    slot = (slot + 1) & mask;
  }
  GLECMA_Result copied = copy_units(tree, units, length, out);
  if (copied != GLECMA_OK) {
    return copied;
  }
  tree->table[slot] = *out;
  tree->table_count++;
  return GLECMA_OK;
}

/* ---- Events -------------------------------------------------------------- */

static GLECMA_Result tree_event(GLECMA_TreeBuilder * builder,
    const GLECMA_BEvent * event) {
  GLECMA_Tree * tree = tree_of(builder);
  if (tree->event_count == tree->event_capacity) {
    size_t capacity = tree->event_capacity == 0 ? 256 : tree->event_capacity * 2;
    if (capacity > SIZE_MAX / sizeof(GLECMA_BEvent)) {
      return GLECMA_ERR_OOM;
    }
    GLECMA_BEvent * grown = (GLECMA_BEvent *)tree->allocator.realloc_fn(
        tree->allocator.ctx, tree->events, capacity * sizeof(GLECMA_BEvent));
    if (grown == NULL) {
      return GLECMA_ERR_OOM;
    }
    tree->events = grown;
    tree->event_capacity = capacity;
  }
  tree->events[tree->event_count++] = *event;
  return GLECMA_OK;
}

/* ---- Nodes --------------------------------------------------------------- */

static GLECMA_Result make_node(GLECMA_Tree * tree, const GLECMA_BSpec * spec,
    GLECMA_BNode * out) {
  if (spec->child_count > UINT32_MAX || spec->text_count > 2 ||
      spec->op > UINT16_MAX) {
    return GLECMA_ERR_INTERNAL;
  }
  if (spec->child_count != 0 && spec->children == NULL) {
    return GLECMA_ERR_INTERNAL;
  }
  size_t size = sizeof(GLECMA_Node) +
      spec->child_count * sizeof(const GLECMA_Node *) +
      (size_t)spec->text_count * sizeof(GLECMA_BText);
  if (spec->child_count > (SIZE_MAX - size) / sizeof(const GLECMA_Node *)) {
    return GLECMA_ERR_OOM;
  }
  GLECMA_Node * node = (GLECMA_Node *)arena_alloc(&tree->allocator,
      &tree->nodes, size);
  if (node == NULL) {
    return GLECMA_ERR_OOM;
  }
  node->kind = spec->kind;
  node->flags = spec->flags;
  node->op = (uint16_t)spec->op;
  node->text_count = (uint16_t)spec->text_count;
  node->child_count = (uint32_t)spec->child_count;
  node->start = spec->start;
  node->end = spec->end;
  uint8_t * trailer = (uint8_t *)node + sizeof(GLECMA_Node);
  for (size_t i = 0; i < spec->child_count; i++) {
    // A node that was built never has an elided child: nothing inside a
    // body the checking builder skipped is built, and the body is replaced.
    if (spec->children[i] == GLECMA_BNODE_ELIDED) {
      return GLECMA_ERR_INTERNAL;
    }
    const GLECMA_Node * child = (const GLECMA_Node *)spec->children[i];
    memcpy(trailer + i * sizeof(child), &child, sizeof(child));
  }
  trailer += spec->child_count * sizeof(const GLECMA_Node *);
  for (uint32_t i = 0; i < spec->text_count; i++) {
    memcpy(trailer + (size_t)i * sizeof(GLECMA_BText), &spec->text[i],
        sizeof(GLECMA_BText));
  }
  *out = node;
  return GLECMA_OK;
}

static GLECMA_Result tree_node(GLECMA_TreeBuilder * builder,
    const GLECMA_BSpec * spec, GLECMA_BNode * out) {
  return make_node(tree_of(builder), spec, out);
}

static GLECMA_Result tree_body_begin(GLECMA_TreeBuilder * builder) {
  (void)builder;
  return GLECMA_OK;
}

static GLECMA_Result tree_body_end(GLECMA_TreeBuilder * builder,
    const GLECMA_BSpec * spec, GLECMA_BNode * out) {
  if (spec->kind == 0) {
    // An expression body: the body is the expression itself.
    *out = spec->child_count == 1 ? spec->children[0] : NULL;
    return spec->child_count == 1 ? GLECMA_OK : GLECMA_ERR_INTERNAL;
  }
  return make_node(tree_of(builder), spec, out);
}

static bool tree_materializing(const GLECMA_TreeBuilder * builder) {
  (void)builder;
  return true;
}

// The reference being amended is the latest one at that position, and is
// found within a few events of the end: the name was the last thing parsed.
static void tree_amend(GLECMA_TreeBuilder * builder, size_t start,
    uint32_t add, uint32_t clear) {
  GLECMA_Tree * tree = tree_of(builder);
  size_t looked = 0;
  for (size_t i = tree->event_count; i > 0 && looked < 4; i--, looked++) {
    GLECMA_BEvent * event = &tree->events[i - 1];
    if (event->type == GLECMA_BEV_REFERENCE && event->start == start) {
      event->what = (event->what | add) & ~clear;
      return;
    }
  }
}

static void tree_mark(GLECMA_TreeBuilder * builder, GLECMA_BMark * mark) {
  GLECMA_Tree * tree = tree_of(builder);
  mark->chunk = tree->nodes.head;
  mark->used = tree->nodes.head != NULL ? tree->nodes.head->used : 0;
  mark->events = tree->event_count;
  mark->body_depth = tree->body_depth;
}

static void tree_rewind(GLECMA_TreeBuilder * builder, const GLECMA_BMark * mark) {
  GLECMA_Tree * tree = tree_of(builder);
  arena_rewind(&tree->allocator, &tree->nodes, (Chunk *)mark->chunk,
      mark->used);
  tree->event_count = mark->events;
  tree->body_depth = mark->body_depth;
}

static const GLECMA_TreeBuilderOps tree_ops = {
  tree_node, tree_text, tree_event, tree_body_begin, tree_body_end,
  tree_materializing, tree_amend, tree_mark, tree_rewind,
};

// The checking builder: the same, except that inside a body no node is built.

static GLECMA_Result checking_node(GLECMA_TreeBuilder * builder,
    const GLECMA_BSpec * spec, GLECMA_BNode * out) {
  GLECMA_Tree * tree = tree_of(builder);
  if (tree->body_depth > 0) {
    *out = GLECMA_BNODE_ELIDED;
    return GLECMA_OK;
  }
  return make_node(tree, spec, out);
}

static GLECMA_Result checking_body_begin(GLECMA_TreeBuilder * builder) {
  GLECMA_Tree * tree = tree_of(builder);
  if (tree->body_depth == UINT32_MAX) {
    return GLECMA_ERR_INTERNAL;
  }
  tree->body_depth++;
  return GLECMA_OK;
}

static GLECMA_Result checking_body_end(GLECMA_TreeBuilder * builder,
    const GLECMA_BSpec * spec, GLECMA_BNode * out) {
  GLECMA_Tree * tree = tree_of(builder);
  if (tree->body_depth == 0) {
    return GLECMA_ERR_INTERNAL;
  }
  tree->body_depth--;
  if (tree->body_depth > 0) {
    // Still inside an outer body: nothing is built.
    *out = GLECMA_BNODE_ELIDED;
    return GLECMA_OK;
  }
  GLECMA_BSpec body = *spec;
  body.kind = GLECMA_NODE_UNMATERIALIZED_BODY;
  body.children = NULL;
  body.child_count = 0;
  body.text_count = 0;
  return make_node(tree, &body, out);
}

static bool checking_materializing(const GLECMA_TreeBuilder * builder) {
  return const_tree_of(builder)->body_depth == 0;
}

static const GLECMA_TreeBuilderOps checking_ops = {
  checking_node, tree_text, tree_event, checking_body_begin, checking_body_end,
  checking_materializing, tree_amend, tree_mark, tree_rewind,
};

/* ---- The tree ------------------------------------------------------------ */

GLECMA_Result glecma_tree_open(uint32_t mode, GLECMA_Source * source,
    const GLECMA_Allocator * allocator, GLECMA_Tree ** out_tree) {
  if (source == NULL || out_tree == NULL ||
      (mode != GLECMA_BUILDER_TREE && mode != GLECMA_BUILDER_CHECKING)) {
    return GLECMA_ERR_INVALID;
  }
  if (allocator == NULL) {
    allocator = glecma_allocator_default();
  }
  if (allocator->malloc_fn == NULL || allocator->calloc_fn == NULL ||
      allocator->realloc_fn == NULL || allocator->free_fn == NULL) {
    return GLECMA_ERR_INVALID;
  }
  GLECMA_Tree * tree =
      (GLECMA_Tree *)allocator->calloc_fn(allocator->ctx, 1, sizeof(*tree));
  if (tree == NULL) {
    return GLECMA_ERR_OOM;
  }
  tree->allocator = *allocator;
  tree->mode = mode;
  tree->builder.ops = mode == GLECMA_BUILDER_TREE ? &tree_ops : &checking_ops;
  tree->builder.owner = tree;
  tree->source = glecma_source_retain(source);
  *out_tree = tree;
  return GLECMA_OK;
}

GLECMA_TreeBuilder * glecma_tree_builder(GLECMA_Tree * tree) {
  return &tree->builder;
}

void glecma_tree_finish(GLECMA_Tree * tree, GLECMA_BNode root, uint32_t goal,
    bool strict) {
  tree->root = (const GLECMA_Node *)root;
  tree->goal = goal;
  tree->strict = strict;
}

size_t glecma_tree_event_count(const GLECMA_Tree * tree) {
  return tree->event_count;
}

const GLECMA_BEvent * glecma_tree_event(const GLECMA_Tree * tree, size_t index) {
  return index < tree->event_count ? &tree->events[index] : NULL;
}

bool glecma_tree_is_checking(const GLECMA_Tree * tree) {
  return tree->mode == GLECMA_BUILDER_CHECKING;
}

void glecma_tree_destroy(GLECMA_Tree * tree) {
  if (tree == NULL) {
    return;
  }
  const GLECMA_Allocator allocator = tree->allocator;
  arena_free(&allocator, &tree->nodes);
  arena_free(&allocator, &tree->texts);
  if (tree->events != NULL) {
    allocator.free_fn(allocator.ctx, tree->events);
  }
  if (tree->table != NULL) {
    allocator.free_fn(allocator.ctx, tree->table);
  }
  glecma_source_release(tree->source);
  allocator.free_fn(allocator.ctx, tree);
}

const GLECMA_Node * glecma_tree_root(const GLECMA_Tree * tree) {
  return tree == NULL ? NULL : tree->root;
}

const GLECMA_Source * glecma_tree_source(const GLECMA_Tree * tree) {
  return tree == NULL ? NULL : tree->source;
}

uint32_t glecma_tree_goal(const GLECMA_Tree * tree) {
  return tree == NULL ? 0 : tree->goal;
}

bool glecma_tree_strict(const GLECMA_Tree * tree) {
  return tree != NULL && tree->strict;
}

/* ---- The walker ---------------------------------------------------------- */

uint32_t glecma_node_kind(const GLECMA_Node * node) {
  return node == NULL ? GLECMA_NODE_NONE : node->kind;
}

uint32_t glecma_node_flags(const GLECMA_Node * node) {
  return node == NULL ? 0 : node->flags;
}

uint32_t glecma_node_operator(const GLECMA_Node * node) {
  return node == NULL ? GLECMA_OP_NONE : node->op;
}

size_t glecma_node_start(const GLECMA_Node * node) {
  return node == NULL ? 0 : node->start;
}

size_t glecma_node_end(const GLECMA_Node * node) {
  return node == NULL ? 0 : node->end;
}

GLECMA_Result glecma_node_location(const GLECMA_Tree * tree,
    const GLECMA_Node * node, GLECMA_Location * out_start,
    GLECMA_Location * out_end) {
  if (tree == NULL || node == NULL) {
    return GLECMA_ERR_INVALID;
  }
  if (out_start != NULL) {
    GLECMA_Result r = glecma_source_location(tree->source, node->start, out_start);
    if (r != GLECMA_OK) {
      return r;
    }
  }
  if (out_end != NULL) {
    return glecma_source_location(tree->source, node->end, out_end);
  }
  return GLECMA_OK;
}

size_t glecma_node_child_count(const GLECMA_Node * node) {
  return node == NULL ? 0 : node->child_count;
}

const GLECMA_Node * glecma_node_child(const GLECMA_Node * node, size_t index) {
  if (node == NULL || index >= node->child_count) {
    return NULL;
  }
  return NODE_CHILDREN(node)[index];
}

bool glecma_node_text(const GLECMA_Node * node, size_t index,
    const uint16_t ** out_units, size_t * out_length) {
  if (node == NULL || index >= node->text_count) {
    return false;
  }
  const GLECMA_BText * text = &NODE_TEXTS(node)[index];
  if (out_units != NULL) {
    *out_units = text->units;
  }
  if (out_length != NULL) {
    *out_length = text->length;
  }
  return true;
}

static const char * const kind_names[GLECMA_NODE_KIND_COUNT] = {
  "None", "Script", "Block", "EmptyStatement", "ExpressionStatement",
  "IfStatement", "ForStatement", "ForInStatement", "ForOfStatement",
  "WhileStatement", "DoWhileStatement", "ContinueStatement", "BreakStatement",
  "ReturnStatement", "WithStatement", "SwitchStatement", "SwitchCase",
  "LabelledStatement", "ThrowStatement", "TryStatement", "CatchClause",
  "DebuggerStatement", "VariableDeclaration", "VariableDeclarator",
  "FunctionDeclaration", "FunctionExpression", "ArrowFunction",
  "ParameterList", "FunctionBody", "UnmaterializedBody", "ClassDeclaration",
  "ClassExpression", "ClassBody", "MethodDefinition", "FieldDefinition",
  "StaticBlock", "PrivateName", "ObjectPattern", "ArrayPattern",
  "PatternProperty", "AssignmentPattern", "RestElement", "Identifier",
  "ThisExpression", "Super", "NullLiteral", "BooleanLiteral", "NumberLiteral",
  "BigIntLiteral", "StringLiteral", "RegExpLiteral", "TemplateLiteral",
  "TemplateElement", "TaggedTemplate", "ArrayLiteral", "ObjectLiteral",
  "Property", "SpreadElement", "UnaryExpression", "UpdateExpression",
  "BinaryExpression", "LogicalExpression", "AssignmentExpression",
  "ConditionalExpression", "CallExpression", "NewExpression",
  "MemberExpression", "OptionalChain", "SequenceExpression",
  "YieldExpression", "AwaitExpression", "MetaProperty", "ImportCall",
  "IntrinsicName",
};

const char * glecma_node_kind_name(uint32_t kind) {
  return kind < GLECMA_NODE_KIND_COUNT ? kind_names[kind] : "Unknown";
}

static const char * const operator_texts[GLECMA_OP_COUNT] = {
  "", "+", "-", "*", "/", "%", "**", "<<", ">>", ">>>", "&", "|", "^", "==",
  "!=", "===", "!==", "<", ">", "<=", ">=", "in", "instanceof", "&&", "||",
  "??", "-", "+", "!", "~", "typeof", "void", "delete", "++", "--",
  "new.target", "import.meta",
};

const char * glecma_operator_text(uint32_t op) {
  return op < GLECMA_OP_COUNT ? operator_texts[op] : "";
}

/* ---- The dump ------------------------------------------------------------ */

static const struct {
  uint32_t flag;
  const char * name;
} flag_names[] = {
  {GLECMA_NODEF_COMPUTED, "computed"}, {GLECMA_NODEF_SHORTHAND, "shorthand"},
  {GLECMA_NODEF_STATIC, "static"}, {GLECMA_NODEF_ASYNC, "async"},
  {GLECMA_NODEF_GENERATOR, "generator"}, {GLECMA_NODEF_PREFIX, "prefix"},
  {GLECMA_NODEF_OPTIONAL, "optional"}, {GLECMA_NODEF_DELEGATE, "delegate"},
  {GLECMA_NODEF_AWAIT, "await"}, {GLECMA_NODEF_DIRECTIVE, "directive"},
  {GLECMA_NODEF_EXPRESSION_BODY, "expression-body"},
  {GLECMA_NODEF_STRICT, "strict"}, {GLECMA_NODEF_GETTER, "getter"},
  {GLECMA_NODEF_SETTER, "setter"}, {GLECMA_NODEF_CONSTRUCTOR, "constructor"},
  {GLECMA_NODEF_DEFAULT, "default"}, {GLECMA_NODEF_VAR, "var"},
  {GLECMA_NODEF_LET, "let"}, {GLECMA_NODEF_CONST, "const"},
  {GLECMA_NODEF_TRUE, "true"}, {GLECMA_NODEF_LEGACY_OCTAL, "legacy-octal"},
  {GLECMA_NODEF_NON_OCTAL_DECIMAL, "non-octal-decimal"},
  {GLECMA_NODEF_COOKED_UNDEFINED, "cooked-undefined"},
  {GLECMA_NODEF_METHOD, "method"}, {GLECMA_NODEF_DERIVED, "derived"},
};

static bool dump_text(FILE * out, const GLECMA_BText * text) {
  if (fputc('"', out) == EOF) {
    return false;
  }
  for (size_t i = 0; i < text->length; i++) {
    uint16_t u = text->units[i];
    int rc;
    if (u == '"' || u == '\\') {
      rc = fprintf(out, "\\%c", (char)u);
    } else if (u >= 0x20 && u < 0x7F) {
      rc = fputc((int)u, out);
    } else {
      rc = fprintf(out, "\\u%04X", (unsigned)u);
    }
    if (rc < 0) {
      return false;
    }
  }
  return fputc('"', out) != EOF;
}

static bool dump_line(FILE * out, const GLECMA_Node * node, size_t depth,
    size_t slot, bool in_slot) {
  for (size_t i = 0; i < depth; i++) {
    if (fputs("  ", out) == EOF) {
      return false;
    }
  }
  (void)slot;
  (void)in_slot;
  if (node == NULL) {
    return fputs("<empty>\n", out) != EOF;
  }
  if (node->kind < GLECMA_NODE_KIND_COUNT) {
    if (fputs(kind_names[node->kind], out) == EOF) {
      return false;
    }
  } else if (fprintf(out, "Unknown(%u)", (unsigned)node->kind) < 0) {
    return false;
  }
  switch (node->kind) {
    case GLECMA_NODE_UNARY_EXPRESSION: case GLECMA_NODE_UPDATE_EXPRESSION:
    case GLECMA_NODE_BINARY_EXPRESSION: case GLECMA_NODE_LOGICAL_EXPRESSION:
    case GLECMA_NODE_META_PROPERTY:
      if (fprintf(out, " %s", glecma_operator_text(node->op)) < 0) {
        return false;
      }
      break;
    case GLECMA_NODE_ASSIGNMENT_EXPRESSION:
      if (fprintf(out, " %s=", glecma_operator_text(node->op)) < 0) {
        return false;
      }
      break;
    default:
      break;
  }
  for (size_t i = 0; i < sizeof(flag_names) / sizeof(flag_names[0]); i++) {
    if ((node->flags & flag_names[i].flag) != 0 &&
        fprintf(out, " +%s", flag_names[i].name) < 0) {
      return false;
    }
  }
  for (uint16_t i = 0; i < node->text_count; i++) {
    if (fputc(' ', out) == EOF || !dump_text(out, &NODE_TEXTS(node)[i])) {
      return false;
    }
  }
  return fprintf(out, " [%zu,%zu)\n", node->start, node->end) >= 0;
}

typedef struct DumpFrame {
  const GLECMA_Node * node;
  size_t next;
} DumpFrame;

GLECMA_Result glecma_tree_dump(const GLECMA_Tree * tree, FILE * out) {
  if (tree == NULL || out == NULL) {
    return GLECMA_ERR_INVALID;
  }
  if (tree->root == NULL) {
    return GLECMA_ERR_INVALID;
  }
  // An explicit stack: a left-leaning chain of operators is as deep as it is
  // long, and the dump must not depend on the native stack.
  size_t capacity = 64;
  size_t depth = 0;
  const GLECMA_Allocator * a = &tree->allocator;
  DumpFrame * stack = (DumpFrame *)a->malloc_fn(a->ctx, capacity * sizeof(*stack));
  if (stack == NULL) {
    return GLECMA_ERR_OOM;
  }
  GLECMA_Result result = GLECMA_OK;
  if (!dump_line(out, tree->root, 0, 0, false)) {
    result = GLECMA_ERR_IO;
  } else {
    stack[depth].node = tree->root;
    stack[depth].next = 0;
    depth++;
  }
  while (result == GLECMA_OK && depth > 0) {
    DumpFrame * top = &stack[depth - 1];
    if (top->next >= top->node->child_count) {
      depth--;
      continue;
    }
    const GLECMA_Node * child = NODE_CHILDREN(top->node)[top->next++];
    if (!dump_line(out, child, depth, top->next - 1, true)) {
      result = GLECMA_ERR_IO;
      break;
    }
    if (child != NULL && child->child_count > 0) {
      if (depth == capacity) {
        if (capacity > SIZE_MAX / 2 / sizeof(*stack)) {
          result = GLECMA_ERR_OOM;
          break;
        }
        DumpFrame * grown = (DumpFrame *)a->realloc_fn(a->ctx, stack,
            capacity * 2 * sizeof(*stack));
        if (grown == NULL) {
          result = GLECMA_ERR_OOM;
          break;
        }
        stack = grown;
        capacity *= 2;
      }
      stack[depth].node = child;
      stack[depth].next = 0;
      depth++;
    }
  }
  a->free_fn(a->ctx, stack);
  return result;
}

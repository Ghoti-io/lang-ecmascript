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
 * The builder interface: how the parser makes the tree. Never installed.
 *
 * **The parser creates nodes only through ::GLECMA_TreeBuilder.** It never
 * allocates, inspects or rewrites a node: a node is an opaque ::GLECMA_BNode
 * the builder returned, and everything the parser needs to know about an
 * expression it has already parsed (whether it can be assigned to, whether
 * it began with a bracket, its range) it keeps in its own small record. That
 * is the rule that lets a second builder materialize no function body: no
 * early-error check, cover-grammar reinterpretation or node count reads a
 * node, so a node that was never built cannot change the answer.
 *
 * Two builders implement the interface, over one tree object:
 *
 *  - the **tree builder** builds every node, and every public parse uses it;
 *  - the **checking builder** is the same, except that between
 *    ::GLECMA_TreeBuilderOps::body_begin and ::GLECMA_TreeBuilderOps::body_end
 *    it builds no node, and ::GLECMA_TreeBuilderOps::body_end then makes the
 *    one node ::GLECMA_NODE_UNMATERIALIZED_BODY in place of the body. It is
 *    that kind's only producer. In this version it is reached by an internal
 *    entry; no public parse uses it.
 *
 * **Events.** The builder also receives declaration and reference events:
 * scopes entered and left, names declared and referenced, and the uses
 * (`this`, `super`, direct `eval`, ...) that change how a function must be
 * compiled. A consumer computes a function's scope summary from the events,
 * never by walking a subtree, so a body that was checked and not built still
 * yields one. Both builders record every event, bodies included.
 *
 * **Cover grammars are resolved from builder events, not by rewriting nodes.**
 * Where the grammar cannot tell what a construct is until later (a
 * parenthesized list that turns out to be arrow parameters, an array literal
 * that turns out to be a pattern), the parser takes a ::GLECMA_BMark, parses
 * the construct one way, and if it was the other, rewinds the builder to the
 * mark and parses it again as that. A rewind discards the nodes and the
 * events since the mark and keeps nothing of them.
 */

#ifndef GHOTI_IO_GLECMA_SRC_PARSE_BUILDER_H
#define GHOTI_IO_GLECMA_SRC_PARSE_BUILDER_H

#include <ghoti.io/lang-ecmascript/macros.h>

#include <ghoti.io/lang-ecmascript/core.h>
#include <ghoti.io/lang-ecmascript/tree.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief A node as the parser sees it: opaque. NULL is no node. */
typedef void * GLECMA_BNode;

/** @brief What the checking builder returns for a node it did not build. */
#define GLECMA_BNODE_ELIDED ((GLECMA_BNode)(uintptr_t)1)

/** @brief A run of UTF-16 code units owned by the builder. */
typedef struct GLECMA_BText {
  const uint16_t * units;
  size_t length;
} GLECMA_BText;

/** @brief Everything the builder needs to make one node. */
typedef struct GLECMA_BSpec {
  uint32_t kind;  ///< A GLECMA_NODE_* kind.
  uint32_t flags; ///< GLECMA_NODEF_* flags.
  uint32_t op;    ///< A GLECMA_OP_*.
  size_t start;   ///< First unit of the node's range.
  size_t end;     ///< One past the last.
  const GLECMA_BNode * children; ///< The child slots, NULL for an empty one.
  size_t child_count;
  GLECMA_BText text[2]; ///< Texts, from ::GLECMA_TreeBuilderOps::text.
  uint32_t text_count;
} GLECMA_BSpec;

/** @name Event types */
///@{
#define GLECMA_BEV_SCOPE_ENTER 1u
#define GLECMA_BEV_SCOPE_LEAVE 2u
#define GLECMA_BEV_DECLARE 3u
#define GLECMA_BEV_REFERENCE 4u
#define GLECMA_BEV_USE 5u
///@}

/** @name Scope kinds (`what` of a scope event) */
///@{
#define GLECMA_BSCOPE_SCRIPT 1u       ///< The top level.
#define GLECMA_BSCOPE_FUNCTION 2u     ///< A function, method, getter, setter or constructor.
#define GLECMA_BSCOPE_ARROW 3u        ///< An arrow function.
#define GLECMA_BSCOPE_BLOCK 4u        ///< A block, or a switch's case block.
#define GLECMA_BSCOPE_CATCH 5u        ///< A catch clause's parameter scope.
#define GLECMA_BSCOPE_FOR 6u          ///< A for statement's head scope.
#define GLECMA_BSCOPE_CLASS 7u        ///< A class's own scope (its name, its private names).
#define GLECMA_BSCOPE_STATIC_BLOCK 8u ///< A class static block.
#define GLECMA_BSCOPE_FIELD_INIT 9u   ///< A class field's initializer.
#define GLECMA_BSCOPE_WITH 10u        ///< A `with` statement's object environment.
#define GLECMA_BSCOPE_MODULE 11u      ///< The top level of a Module.
///@}

/** @name Declaration kinds (`what` of a declare event) */
///@{
#define GLECMA_BDECL_VAR 1u
#define GLECMA_BDECL_LET 2u
#define GLECMA_BDECL_CONST 3u
#define GLECMA_BDECL_FUNCTION 4u      ///< A function declaration's name.
#define GLECMA_BDECL_CLASS 5u         ///< A class declaration's name.
#define GLECMA_BDECL_PARAM 6u
#define GLECMA_BDECL_CATCH_PARAM 7u
#define GLECMA_BDECL_FUNCTION_NAME 8u ///< A function expression's own name, inside its scope.
#define GLECMA_BDECL_CLASS_NAME 9u    ///< A class's inner name binding, inside its scope.
#define GLECMA_BDECL_IMPORT 10u       ///< The local name an import declaration binds.
///@}

/** @name Reference flags (`what` of a reference event) */
///@{
#define GLECMA_BREF_READ 1u
#define GLECMA_BREF_WRITE 2u  ///< An assignment target, update operand or loop target.
#define GLECMA_BREF_TYPEOF 4u ///< The operand of `typeof`: an unresolvable name is not an error.
#define GLECMA_BREF_DELETE 8u ///< The operand of `delete`.
///@}

/** @name Use kinds (`what` of a use event) */
///@{
#define GLECMA_BUSE_THIS 1u
#define GLECMA_BUSE_SUPER_PROPERTY 2u
#define GLECMA_BUSE_SUPER_CALL 3u
#define GLECMA_BUSE_NEW_TARGET 4u
#define GLECMA_BUSE_DIRECT_EVAL 5u ///< A call whose callee is the plain name `eval`.
#define GLECMA_BUSE_IMPORT_META 6u
///@}

/**
 * @brief One declaration or reference event.
 *
 * `name` is set for a declare or reference event and is interned: equal
 * names have equal `units` pointers. `start` and `end` are the range of the
 * name (or of the construct, for a scope or a use).
 */
typedef struct GLECMA_BEvent {
  uint32_t type; ///< A GLECMA_BEV_*.
  uint32_t what; ///< The kind of scope, declaration, reference or use.
  GLECMA_BText name;
  size_t start;
  size_t end;
} GLECMA_BEvent;

/** @brief A saved position of a builder, to return to with `rewind`. */
typedef struct GLECMA_BMark {
  void * chunk;
  size_t used;
  size_t events;
  uint32_t body_depth;
} GLECMA_BMark;

typedef struct GLECMA_TreeBuilder GLECMA_TreeBuilder;

/** @brief The operations a builder provides. Every function may fail with ::GLECMA_ERR_OOM. */
typedef struct GLECMA_TreeBuilderOps {
  /** Makes a node; `*out` is GLECMA_BNODE_ELIDED when the builder built none. */
  GLECMA_Result (*node)(GLECMA_TreeBuilder * builder, const GLECMA_BSpec * spec,
      GLECMA_BNode * out);
  /** Copies text into the builder, interned when @p intern; the copy outlives a rewind. */
  GLECMA_Result (*text)(GLECMA_TreeBuilder * builder, const uint16_t * units,
      size_t length, bool intern, GLECMA_BText * out);
  /** Records a declaration or reference event. */
  GLECMA_Result (*event)(GLECMA_TreeBuilder * builder,
      const GLECMA_BEvent * event);
  /** A function body begins: the next nodes are body nodes. */
  GLECMA_Result (*body_begin)(GLECMA_TreeBuilder * builder);
  /** The body ends: makes its node (the tree builder uses @p spec as given; the checking builder's is an unmaterialized body). */
  GLECMA_Result (*body_end)(GLECMA_TreeBuilder * builder,
      const GLECMA_BSpec * spec, GLECMA_BNode * out);
  /** Whether nodes are being built (false inside a body of the checking builder). */
  bool (*materializing)(const GLECMA_TreeBuilder * builder);
  /**
   * Changes the flags of the reference event for the name at @p start, the
   * latest one: used when an identifier read as a value turns out, from what
   * follows it, to be an assignment target or the operand of `typeof`.
   */
  void (*amend)(GLECMA_TreeBuilder * builder, size_t start, uint32_t add,
      uint32_t clear);
  /** Saves a position. */
  void (*mark)(GLECMA_TreeBuilder * builder, GLECMA_BMark * mark);
  /** Returns to a saved position, discarding every node and event since. */
  void (*rewind)(GLECMA_TreeBuilder * builder, const GLECMA_BMark * mark);
} GLECMA_TreeBuilderOps;

struct GLECMA_TreeBuilder {
  const GLECMA_TreeBuilderOps * ops;
  void * owner; ///< The builder's own state: the tree.
};

/** @name Builder modes */
///@{
#define GLECMA_BUILDER_TREE 0u     ///< Builds every node.
#define GLECMA_BUILDER_CHECKING 1u ///< Builds no function body.
///@}

/**
 * @brief Makes a tree and its builder.
 *
 * @param mode ::GLECMA_BUILDER_TREE or ::GLECMA_BUILDER_CHECKING.
 * @param source The source; the tree takes its own reference.
 * @param allocator The allocator, or NULL for the default.
 * @param out_tree Receives the tree, which is not complete until
 *   ::glecma_tree_finish; destroy it with ::glecma_tree_destroy.
 * @return ::GLECMA_OK, ::GLECMA_ERR_INVALID or ::GLECMA_ERR_OOM.
 */
GLECMA_INTERNAL_API GLECMA_Result glecma_tree_open(uint32_t mode,
    GLECMA_Source * source, const GLECMA_Allocator * allocator,
    GLECMA_Tree ** out_tree);

/** @brief The tree's builder. */
GLECMA_INTERNAL_API GLECMA_TreeBuilder * glecma_tree_builder(GLECMA_Tree * tree);

/** @brief Completes a tree: sets its root, goal and strictness. */
GLECMA_INTERNAL_API void glecma_tree_finish(GLECMA_Tree * tree,
    GLECMA_BNode root, uint32_t goal, bool strict);

/**
 * @brief Gives the tree its own copy of the context its code was parsed in.
 *
 * @return ::GLECMA_OK or ::GLECMA_ERR_OOM.
 */
GLECMA_INTERNAL_API GLECMA_Result glecma_tree_set_eval_context(
    GLECMA_Tree * tree, const GLECMA_EvalContext * context);

/** @brief The number of events the builder recorded. */
GLECMA_INTERNAL_API size_t glecma_tree_event_count(const GLECMA_Tree * tree);

/** @brief The event at @p index, valid until the tree is destroyed. */
GLECMA_INTERNAL_API const GLECMA_BEvent * glecma_tree_event(
    const GLECMA_Tree * tree, size_t index);

/** @brief Whether the tree was made by the checking builder. */
GLECMA_INTERNAL_API bool glecma_tree_is_checking(const GLECMA_Tree * tree);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GLECMA_SRC_PARSE_BUILDER_H

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
 * @file tree.h
 * @stability free
 *
 * The public tree a parse returns, and the walker that reads it.
 *
 * A tree is read-only and owned by the caller, who destroys it. It keeps no
 * pointer into the caller's buffer: every name and string a node carries is
 * the tree's own copy, and the tree holds one counted reference to its
 * ::GLECMA_Source, which it releases when it is destroyed.
 *
 * **The tree is not needed after its top level is compiled.** A function that
 * is compiled later is parsed again from the source text, so this API
 * promises nothing about use of a tree at run time, and nothing keeps one
 * alive.
 *
 * **Shape.** Every node has a kind, flags, an operator (for the kinds that
 * have one), a source range, an ordered list of children and up to two texts.
 * A child slot that the grammar makes optional, or that a hole in an array
 * leaves empty, is NULL, so a kind's children are positional: the table in
 * this header's kind list gives each kind's slots. A walker that reads only
 * kinds, flags, operators, texts and children, and has a default case for a
 * kind it does not know, keeps working when a kind is added at the end of the
 * enumeration; ::GLECMA_NODE_UNMATERIALIZED_BODY is reserved for a function
 * body that was checked and not built, and no tree a public parse returns
 * holds one.
 *
 * **Ranges.** A node's range is `[start, end)` in the source's own units. The
 * range of a function or class node is exactly the text
 * `Function.prototype.toString` returns for it, so a method's range starts at
 * its first modifier or name and not at `static`. The range of a parenthesized
 * expression is the inner expression's: parentheses are not nodes.
 *
 * **Values.** A numeric literal's value is not computed here (the compiler
 * computes it, with exact arithmetic): the node's range is the literal's
 * text, and its flags say whether it was a legacy octal or a leading-zero
 * decimal literal. A string's value and a template part's cooked and raw
 * values are UTF-16 code units, because a string may hold a lone surrogate.
 */

#ifndef GHOTI_IO_GLECMA_TREE_H
#define GHOTI_IO_GLECMA_TREE_H

#include <ghoti.io/lang-ecmascript/macros.h>

#include <ghoti.io/lang-ecmascript/allocator.h>
#include <ghoti.io/lang-ecmascript/core.h>
#include <ghoti.io/lang-ecmascript/options.h>
#include <ghoti.io/lang-ecmascript/source.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief A parsed program. Opaque; destroy with ::glecma_tree_destroy. */
typedef struct GLECMA_Tree GLECMA_Tree;

/** @brief One node of a tree. Opaque; valid until its tree is destroyed. */
typedef struct GLECMA_Node GLECMA_Node;

/**
 * @name Node kinds
 *
 * Kinds are numbers, so that a host may store them; the enumeration is only
 * appended to. Each entry lists the node's children by slot, `?` marking a
 * slot that may be NULL, and `...` a list that may be empty.
 */
///@{
#define GLECMA_NODE_NONE 0u ///< Not a kind: the value of no node.

#define GLECMA_NODE_SCRIPT 1u ///< Root: statements...

/* Statements. */
#define GLECMA_NODE_BLOCK 2u ///< statements...
#define GLECMA_NODE_EMPTY_STATEMENT 3u
#define GLECMA_NODE_EXPRESSION_STATEMENT 4u ///< expression; flag DIRECTIVE
#define GLECMA_NODE_IF_STATEMENT 5u ///< test, consequent, alternate?
#define GLECMA_NODE_FOR_STATEMENT 6u ///< init?, test?, update?, body
#define GLECMA_NODE_FOR_IN_STATEMENT 7u ///< left, right, body
#define GLECMA_NODE_FOR_OF_STATEMENT 8u ///< left, right, body; flag AWAIT
#define GLECMA_NODE_WHILE_STATEMENT 9u ///< test, body
#define GLECMA_NODE_DO_WHILE_STATEMENT 10u ///< body, test
#define GLECMA_NODE_CONTINUE_STATEMENT 11u ///< label?
#define GLECMA_NODE_BREAK_STATEMENT 12u ///< label?
#define GLECMA_NODE_RETURN_STATEMENT 13u ///< argument?
#define GLECMA_NODE_WITH_STATEMENT 14u ///< object, body
#define GLECMA_NODE_SWITCH_STATEMENT 15u ///< discriminant, cases...
#define GLECMA_NODE_SWITCH_CASE 16u ///< test? (NULL with flag DEFAULT), statements...
#define GLECMA_NODE_LABELLED_STATEMENT 17u ///< label, body
#define GLECMA_NODE_THROW_STATEMENT 18u ///< argument
#define GLECMA_NODE_TRY_STATEMENT 19u ///< block, handler?, finalizer?
#define GLECMA_NODE_CATCH_CLAUSE 20u ///< parameter?, body
#define GLECMA_NODE_DEBUGGER_STATEMENT 21u
#define GLECMA_NODE_VARIABLE_DECLARATION 22u ///< declarators...; flag VAR, LET or CONST
#define GLECMA_NODE_VARIABLE_DECLARATOR 23u ///< target, initializer?

/* Functions and classes. */
#define GLECMA_NODE_FUNCTION_DECLARATION 24u ///< id, parameters, body; flags ASYNC, GENERATOR
#define GLECMA_NODE_FUNCTION_EXPRESSION 25u ///< id?, parameters, body; flags ASYNC, GENERATOR
#define GLECMA_NODE_ARROW_FUNCTION 26u ///< NULL, parameters, body; flags ASYNC, EXPRESSION_BODY
#define GLECMA_NODE_PARAMETER_LIST 27u ///< a binding target, ASSIGNMENT_PATTERN or REST_ELEMENT per parameter...
#define GLECMA_NODE_FUNCTION_BODY 28u ///< statements...; flag STRICT when the body is strict code
/** A body that was fully checked and not built. Only the checking builder makes one. */
#define GLECMA_NODE_UNMATERIALIZED_BODY 29u
#define GLECMA_NODE_CLASS_DECLARATION 30u ///< id, superclass?, class body
#define GLECMA_NODE_CLASS_EXPRESSION 31u ///< id?, superclass?, class body
#define GLECMA_NODE_CLASS_BODY 32u ///< members...
#define GLECMA_NODE_METHOD_DEFINITION 33u ///< key, FUNCTION_EXPRESSION; flags STATIC, COMPUTED, GETTER, SETTER, CONSTRUCTOR
#define GLECMA_NODE_FIELD_DEFINITION 34u ///< key, value?; flags STATIC, COMPUTED
#define GLECMA_NODE_STATIC_BLOCK 35u ///< statements...
#define GLECMA_NODE_PRIVATE_NAME 36u ///< text 0: the name without the `#`

/* Patterns: the same kinds serve binding and assignment patterns. */
#define GLECMA_NODE_OBJECT_PATTERN 37u ///< PATTERN_PROPERTY or REST_ELEMENT...
#define GLECMA_NODE_ARRAY_PATTERN 38u ///< a target, ASSIGNMENT_PATTERN, REST_ELEMENT or NULL (a hole)...
#define GLECMA_NODE_PATTERN_PROPERTY 39u ///< key, value; flags COMPUTED, SHORTHAND
#define GLECMA_NODE_ASSIGNMENT_PATTERN 40u ///< target, default value
#define GLECMA_NODE_REST_ELEMENT 41u ///< target

/* Expressions. */
#define GLECMA_NODE_IDENTIFIER 42u ///< text 0: the name, escapes decoded
#define GLECMA_NODE_THIS_EXPRESSION 43u
#define GLECMA_NODE_SUPER 44u ///< Only as the callee of a call or the object of a member.
#define GLECMA_NODE_NULL_LITERAL 45u
#define GLECMA_NODE_BOOLEAN_LITERAL 46u ///< flag TRUE when `true`
#define GLECMA_NODE_NUMBER_LITERAL 47u ///< flags LEGACY_OCTAL, NON_OCTAL_DECIMAL
#define GLECMA_NODE_BIGINT_LITERAL 48u
#define GLECMA_NODE_STRING_LITERAL 49u ///< text 0: the value; flags LEGACY_OCTAL, NON_OCTAL_DECIMAL
#define GLECMA_NODE_REGEXP_LITERAL 50u ///< text 0: the pattern; text 1: the flags
#define GLECMA_NODE_TEMPLATE_LITERAL 51u ///< element, expression, element, ..., element
#define GLECMA_NODE_TEMPLATE_ELEMENT 52u ///< text 0: cooked (flag COOKED_UNDEFINED when none); text 1: raw
#define GLECMA_NODE_TAGGED_TEMPLATE 53u ///< tag, template literal
#define GLECMA_NODE_ARRAY_LITERAL 54u ///< expression, SPREAD_ELEMENT or NULL (a hole)...
#define GLECMA_NODE_OBJECT_LITERAL 55u ///< PROPERTY or SPREAD_ELEMENT...
#define GLECMA_NODE_PROPERTY 56u ///< key, value; flags COMPUTED, SHORTHAND; also GETTER, SETTER, METHOD (value is a FUNCTION_EXPRESSION)
#define GLECMA_NODE_SPREAD_ELEMENT 57u ///< argument
#define GLECMA_NODE_UNARY_EXPRESSION 58u ///< argument; operator
#define GLECMA_NODE_UPDATE_EXPRESSION 59u ///< argument; operator INC or DEC; flag PREFIX
#define GLECMA_NODE_BINARY_EXPRESSION 60u ///< left, right; operator
#define GLECMA_NODE_LOGICAL_EXPRESSION 61u ///< left, right; operator AND, OR or NULLISH
#define GLECMA_NODE_ASSIGNMENT_EXPRESSION 62u ///< target, value; operator NONE for `=`, else the operator of `op=`
#define GLECMA_NODE_CONDITIONAL_EXPRESSION 63u ///< test, consequent, alternate
#define GLECMA_NODE_CALL_EXPRESSION 64u ///< callee, arguments...; flag OPTIONAL for `?.(`
#define GLECMA_NODE_NEW_EXPRESSION 65u ///< callee, arguments...
#define GLECMA_NODE_MEMBER_EXPRESSION 66u ///< object, property; flags COMPUTED, OPTIONAL (`?.`); property is an IDENTIFIER or PRIVATE_NAME when not computed
#define GLECMA_NODE_OPTIONAL_CHAIN 67u ///< expression: wraps a whole chain that holds a `?.`
#define GLECMA_NODE_SEQUENCE_EXPRESSION 68u ///< expressions...
#define GLECMA_NODE_YIELD_EXPRESSION 69u ///< argument?; flag DELEGATE
#define GLECMA_NODE_AWAIT_EXPRESSION 70u ///< argument
#define GLECMA_NODE_META_PROPERTY 71u ///< `new.target` or `import.meta`; operator is NEW_TARGET or IMPORT_META
#define GLECMA_NODE_IMPORT_CALL 72u ///< specifier, options?
#define GLECMA_NODE_INTRINSIC_NAME 73u ///< text 0: the name without the `%`; only under the internal built-in entry
#define GLECMA_NODE_KIND_COUNT 74u ///< Not a kind; one past the last.
///@}

/**
 * @name Node flags
 *
 * Which flags a kind uses is stated beside the kind. A flag a kind does not
 * use is zero.
 */
///@{
#define GLECMA_NODEF_COMPUTED 0x00000001u
#define GLECMA_NODEF_SHORTHAND 0x00000002u
#define GLECMA_NODEF_STATIC 0x00000004u
#define GLECMA_NODEF_ASYNC 0x00000008u
#define GLECMA_NODEF_GENERATOR 0x00000010u
#define GLECMA_NODEF_PREFIX 0x00000020u
#define GLECMA_NODEF_OPTIONAL 0x00000040u
#define GLECMA_NODEF_DELEGATE 0x00000080u
#define GLECMA_NODEF_AWAIT 0x00000100u ///< `for await`
#define GLECMA_NODEF_DIRECTIVE 0x00000200u
#define GLECMA_NODEF_EXPRESSION_BODY 0x00000400u
#define GLECMA_NODEF_STRICT 0x00000800u
#define GLECMA_NODEF_GETTER 0x00001000u
#define GLECMA_NODEF_SETTER 0x00002000u
#define GLECMA_NODEF_CONSTRUCTOR 0x00004000u
#define GLECMA_NODEF_DEFAULT 0x00008000u
#define GLECMA_NODEF_VAR 0x00010000u
#define GLECMA_NODEF_LET 0x00020000u
#define GLECMA_NODEF_CONST 0x00040000u
#define GLECMA_NODEF_TRUE 0x00080000u
#define GLECMA_NODEF_LEGACY_OCTAL 0x00100000u
#define GLECMA_NODEF_NON_OCTAL_DECIMAL 0x00200000u
#define GLECMA_NODEF_COOKED_UNDEFINED 0x00400000u
#define GLECMA_NODEF_METHOD 0x00800000u
#define GLECMA_NODEF_DERIVED 0x01000000u ///< A class with an `extends` clause (on the class node)
///@}

/**
 * @name Operators
 *
 * The operator of a unary, update, binary, logical, assignment or meta
 * property node. An assignment's operator is the binary or logical operator
 * of its `op=` form, or ::GLECMA_OP_NONE for `=`.
 */
///@{
#define GLECMA_OP_NONE 0u
#define GLECMA_OP_ADD 1u
#define GLECMA_OP_SUB 2u
#define GLECMA_OP_MUL 3u
#define GLECMA_OP_DIV 4u
#define GLECMA_OP_MOD 5u
#define GLECMA_OP_EXP 6u
#define GLECMA_OP_SHL 7u
#define GLECMA_OP_SHR 8u
#define GLECMA_OP_USHR 9u
#define GLECMA_OP_BIT_AND 10u
#define GLECMA_OP_BIT_OR 11u
#define GLECMA_OP_BIT_XOR 12u
#define GLECMA_OP_EQ 13u
#define GLECMA_OP_NE 14u
#define GLECMA_OP_SEQ 15u
#define GLECMA_OP_SNE 16u
#define GLECMA_OP_LT 17u
#define GLECMA_OP_GT 18u
#define GLECMA_OP_LE 19u
#define GLECMA_OP_GE 20u
#define GLECMA_OP_IN 21u
#define GLECMA_OP_INSTANCEOF 22u
#define GLECMA_OP_AND 23u
#define GLECMA_OP_OR 24u
#define GLECMA_OP_NULLISH 25u
#define GLECMA_OP_NEG 26u
#define GLECMA_OP_PLUS 27u
#define GLECMA_OP_NOT 28u
#define GLECMA_OP_BIT_NOT 29u
#define GLECMA_OP_TYPEOF 30u
#define GLECMA_OP_VOID 31u
#define GLECMA_OP_DELETE 32u
#define GLECMA_OP_INC 33u
#define GLECMA_OP_DEC 34u
#define GLECMA_OP_NEW_TARGET 35u
#define GLECMA_OP_IMPORT_META 36u
#define GLECMA_OP_COUNT 37u ///< Not an operator; one past the last.
///@}

/**
 * @brief Parses a source into a tree.
 *
 * Reads the whole source for the goal in @p options (a Script when @p options
 * is NULL), through the tree builder, and checks every function body fully:
 * a body is built, never skipped. A feature set that names a feature this
 * build lacks is refused before any work.
 *
 * @param source The source; the tree holds its own counted reference to it,
 *   so the caller may release theirs.
 * @param options The parse options, or NULL for the defaults.
 * @param allocator Allocator for the tree, or NULL for the default; it is
 *   copied into the tree and its context must outlive the tree.
 * @param failure Optional; written on every return. For
 *   ::GLECMA_ERR_SYNTAX it holds the rule, a message and a location inside
 *   the offending construct; for ::GLECMA_ERR_LIMIT the limit's name.
 * @param out_tree Receives the tree; written only on success.
 * @return ::GLECMA_OK; ::GLECMA_ERR_INVALID for a NULL source or output, or
 *   options or limits whose `size` is below the header;
 *   ::GLECMA_ERR_UNSUPPORTED for a feature the build lacks (named in
 *   `failure->feature`) or a goal not built; ::GLECMA_ERR_SYNTAX;
 *   ::GLECMA_ERR_LIMIT naming `tokens`, `literal_length`, `nesting`, `nodes`
 *   or `native_stack_bytes`; ::GLECMA_ERR_OOM, with nothing leaked.
 */
GLECMA_API GLECMA_Result glecma_parse(GLECMA_Source * source,
    const GLECMA_ParseOptions * options, const GLECMA_Allocator * allocator,
    GLECMA_Failure * failure, GLECMA_Tree ** out_tree);

/** @brief Destroys a tree and releases its reference to the source. NULL does nothing. */
GLECMA_API void glecma_tree_destroy(GLECMA_Tree * tree);

/** @brief The tree's root: a ::GLECMA_NODE_SCRIPT. */
GLECMA_API const GLECMA_Node * glecma_tree_root(const GLECMA_Tree * tree);

/** @brief The source the tree was parsed from, which the tree keeps alive. */
GLECMA_API const GLECMA_Source * glecma_tree_source(const GLECMA_Tree * tree);

/** @brief The goal the tree was parsed for: ::GLECMA_PARSE_SCRIPT. */
GLECMA_API uint32_t glecma_tree_goal(const GLECMA_Tree * tree);

/**
 * @brief Whether the program ended in strict code.
 *
 * Strictness can only be added by the source, never removed: a directive
 * prologue that holds `"use strict"` makes the top level strict.
 */
GLECMA_API bool glecma_tree_strict(const GLECMA_Tree * tree);

/** @brief A node's kind. A kind this header does not list is a newer library's. */
GLECMA_API uint32_t glecma_node_kind(const GLECMA_Node * node);

/** @brief A node's flags. */
GLECMA_API uint32_t glecma_node_flags(const GLECMA_Node * node);

/** @brief A node's operator, or ::GLECMA_OP_NONE. */
GLECMA_API uint32_t glecma_node_operator(const GLECMA_Node * node);

/** @brief The offset of a node's first unit, in the source's units. */
GLECMA_API size_t glecma_node_start(const GLECMA_Node * node);

/** @brief The offset one past a node's last unit. */
GLECMA_API size_t glecma_node_end(const GLECMA_Node * node);

/**
 * @brief A node's range as locations.
 *
 * @param tree The tree the node belongs to.
 * @param node The node.
 * @param out_start Receives the start location; may be NULL.
 * @param out_end Receives the end location; may be NULL.
 * @return ::GLECMA_OK; ::GLECMA_ERR_INVALID for NULL arguments.
 */
GLECMA_API GLECMA_Result glecma_node_location(const GLECMA_Tree * tree,
    const GLECMA_Node * node, GLECMA_Location * out_start,
    GLECMA_Location * out_end);

/** @brief The number of child slots a node has. */
GLECMA_API size_t glecma_node_child_count(const GLECMA_Node * node);

/** @brief A node's child at @p index; NULL for an empty slot or an index past the end. */
GLECMA_API const GLECMA_Node * glecma_node_child(const GLECMA_Node * node,
    size_t index);

/**
 * @brief A node's text.
 *
 * @param node The node.
 * @param index 0 or 1; which texts a kind has is stated beside the kind.
 * @param out_units Receives the UTF-16 code units, owned by the tree and
 *   not NUL-terminated; may be NULL.
 * @param out_length Receives their count; may be NULL.
 * @return true when the node has that text.
 */
GLECMA_API bool glecma_node_text(const GLECMA_Node * node, size_t index,
    const uint16_t ** out_units, size_t * out_length);

/** @brief The name of a node kind, such as "BinaryExpression"; "Unknown" for one this header lacks. */
GLECMA_API const char * glecma_node_kind_name(uint32_t kind);

/** @brief The text of an operator, such as ">>>" or "typeof"; "" for none and for an unknown one. */
GLECMA_API const char * glecma_operator_text(uint32_t op);

/**
 * @brief Writes a readable listing of a tree.
 *
 * One node per line, indented by depth: its kind name, range, the flags and
 * operator it carries and its texts. A kind the library does not know is
 * listed by number and its children are still walked, and a
 * ::GLECMA_NODE_UNMATERIALIZED_BODY is listed as what it is.
 *
 * The listing is for people and for tests. It is **not a printer**: it does
 * not reproduce source text and nothing parses it back (design.md says why).
 *
 * @param tree The tree.
 * @param out The stream to write to.
 * @return ::GLECMA_OK; ::GLECMA_ERR_INVALID for a NULL argument;
 *   ::GLECMA_ERR_IO when a write fails; ::GLECMA_ERR_OOM.
 */
GLECMA_API GLECMA_Result glecma_tree_dump(const GLECMA_Tree * tree, FILE * out);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GLECMA_TREE_H

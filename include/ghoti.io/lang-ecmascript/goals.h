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
 * @file goals.h
 * @stability free
 *
 * The goals that take more than a source: a function made from strings
 * (CreateDynamicFunction, clause 20.2.1.1.1), and one function read again from
 * its range of a source it was first read in.
 *
 * The entries take the parse options like every parse entry, and so the limits and the
 * feature set, and refuse a feature set that names a feature the build lacks
 * before any work (::GLECMA_ERR_UNSUPPORTED). Their options name no other goal
 * than the entry's own (::GLECMA_PARSE_SCRIPT, which a defaulted struct holds,
 * stands for it) and carry no strictness and no eval context, because the context
 * a function was read in is what the second takes as its own input.
 */

#ifndef GHOTI_IO_GLECMA_GOALS_H
#define GHOTI_IO_GLECMA_GOALS_H

#include <ghoti.io/lang-ecmascript/macros.h>

#include <ghoti.io/lang-ecmascript/allocator.h>
#include <ghoti.io/lang-ecmascript/core.h>
#include <ghoti.io/lang-ecmascript/options.h>
#include <ghoti.io/lang-ecmascript/source.h>
#include <ghoti.io/lang-ecmascript/tree.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name Dynamic function kinds
 *
 * The four constructors of CreateDynamicFunction: `Function`,
 * `GeneratorFunction`, `AsyncFunction` and `AsyncGeneratorFunction`.
 */
///@{
#define GLECMA_DYNFN_FUNCTION 0u        ///< `function`; its parameters and body are read as a plain function's.
#define GLECMA_DYNFN_GENERATOR 1u       ///< `function*`; `yield` is an operator in the body.
#define GLECMA_DYNFN_ASYNC 2u           ///< `async function`; `await` is an operator in the body.
#define GLECMA_DYNFN_ASYNC_GENERATOR 3u ///< `async function*`.
///@}

/**
 * @brief The pieces CreateDynamicFunction is given: the kind and the texts of
 *   the parameters and of the body.
 *
 * Size-first. The two texts are sources, which carry any code unit a string can
 * (a lone surrogate included, from UTF-16). `parameters` is the text the
 * arguments before the last make when they are joined with `,` (the caller joins
 * them: that is ToString and a concatenation on the caller's values, which a
 * parser does not do), and `body` is the last argument. A NULL source is the empty
 * text.
 */
typedef struct GLECMA_DynamicFunction {
  uint32_t size;                   ///< `sizeof(GLECMA_DynamicFunction)` as the caller knows it.
  uint32_t kind;                   ///< A ::GLECMA_DYNFN_FUNCTION and the others.
  const GLECMA_Source * parameters; ///< The parameters' text, or NULL.
  const GLECMA_Source * body;       ///< The body's text, or NULL.
} GLECMA_DynamicFunction;

/** @brief Initializer for a ::GLECMA_DynamicFunction: its size, a plain function and no text. */
#define GLECMA_DYNAMIC_FUNCTION_INIT                                           \
  { (uint32_t)sizeof(GLECMA_DynamicFunction), GLECMA_DYNFN_FUNCTION, NULL, NULL }

/**
 * @brief Reads the function CreateDynamicFunction makes from its pieces.
 *
 * Does what the algorithm's parsing steps do. The parameters are read alone as
 * FormalParameters with the kind's `[Yield, Await]`; the body alone as the
 * kind's function body (`FunctionBody`, `GeneratorBody`, `AsyncFunctionBody` or
 * `AsyncGeneratorBody`); and then the text the algorithm assembles, the kind's
 * prefix (`function`, `function*`, `async function`, `async function*`), ` anonymous(`,
 * the parameters, a line feed, `) {`, a line feed, the body, a line feed and `}`,
 * as the kind's function expression, with every early error of both. Each piece
 * is read alone so that each is valid alone: a parameter list that opens a comment
 * which the body closes, or a body that begins with `) {`, does not make a
 * function, whatever the assembled text would be.
 *
 * On success the tree's source is the assembled text (``glecma_tree_source``), the
 * text of the function's `toString`, and its root is a ::GLECMA_NODE_FUNCTION_EXPRESSION
 * that covers all of it. Its name, `anonymous`, is not bound inside the function:
 * the algorithm sets the function's name and binds none, and the compiler binds none.
 * The tree's goal is ::GLECMA_PARSE_DYNAMIC_FUNCTION and its strictness that of
 * the body (a "use strict" directive in it makes the function strict).
 *
 * A failure found in one piece alone (the parameters, or the body) is placed in
 * the source the caller gave for it, in that source's own units, line and
 * column: the line feed the algorithm puts before the body is not counted, and
 * an error at the end of the text is at the end of the piece. A failure only the
 * assembled text has (a duplicate parameter in strict code, a directive over
 * non-simple parameters) is in the assembled text, in UTF-16 units, which only a
 * successful call returns.
 *
 * @param function The pieces.
 * @param options The parse options (limits and features), or NULL for the
 *   defaults. The goal, strictness and eval context must be the defaults.
 * @param allocator Allocator for the tree and the sources it makes, or NULL for the
 *   default; copied into them, and its context must outlive the tree.
 * @param failure Optional, as for ::glecma_parse.
 * @param out_tree Receives the tree; written only on success.
 * @return ::GLECMA_OK; ::GLECMA_ERR_INVALID for NULL arguments, a `size` below the
 *   header, an unknown kind or options this entry does not take;
 *   ::GLECMA_ERR_UNSUPPORTED for a feature the build lacks; ::GLECMA_ERR_SYNTAX;
 *   ::GLECMA_ERR_LIMIT naming a limit the text exceeded (`source_units` for the
 *   assembled text); ::GLECMA_ERR_OOM, with nothing leaked.
 */
GLECMA_API GLECMA_Result glecma_parse_dynamic_function(
    const GLECMA_DynamicFunction * function, const GLECMA_ParseOptions * options,
    const GLECMA_Allocator * allocator, GLECMA_Failure * failure,
    GLECMA_Tree ** out_tree);

/**
 * @name Function kinds
 *
 * Every form that has a body of its own: what a function is compiled from, and
 * so what it is read again as.
 */
///@{
#define GLECMA_FNKIND_FUNCTION 1u            ///< `function f() {}`, written as a declaration or as an expression.
#define GLECMA_FNKIND_GENERATOR 2u           ///< `function* f() {}`.
#define GLECMA_FNKIND_ASYNC_FUNCTION 3u      ///< `async function f() {}`.
#define GLECMA_FNKIND_ASYNC_GENERATOR 4u     ///< `async function* f() {}`.
#define GLECMA_FNKIND_ARROW 5u               ///< `(a) => a`, `a => a`.
#define GLECMA_FNKIND_ASYNC_ARROW 6u         ///< `async (a) => a`, `async a => a`.
#define GLECMA_FNKIND_METHOD 7u              ///< `m() {}`, `*m() {}`, `async m() {}`, `async *m() {}`, in an object literal or a class; its range starts at the first modifier or the name.
#define GLECMA_FNKIND_GETTER 8u              ///< `get x() {}`.
#define GLECMA_FNKIND_SETTER 9u              ///< `set x(v) {}`.
#define GLECMA_FNKIND_CONSTRUCTOR 10u        ///< `constructor() {}` of a class with no `extends`.
#define GLECMA_FNKIND_DERIVED_CONSTRUCTOR 11u ///< `constructor() {}` of a class with an `extends` clause.
#define GLECMA_FNKIND_FIELD_INITIALIZER 12u  ///< The initializer of a class field: an expression, which is the function's body.
#define GLECMA_FNKIND_STATIC_BLOCK 13u       ///< `static { }`, from `static` to the closing brace.
#define GLECMA_FNKIND_COUNT 14u              ///< Not a kind; one past the last.
///@}

/**
 * @brief The flag, beside the ::GLECMA_CONTEXT_STRICT family, that says a function
 *   was written as a declaration.
 *
 * Only ::GLECMA_FNKIND_FUNCTION, ::GLECMA_FNKIND_GENERATOR, ::GLECMA_FNKIND_ASYNC_FUNCTION
 * and ::GLECMA_FNKIND_ASYNC_GENERATOR may carry it. A declaration binds its name in
 * the code around it; an expression binds its name inside itself. The tree the
 * function is read as is a ::GLECMA_NODE_FUNCTION_DECLARATION or a
 * ::GLECMA_NODE_FUNCTION_EXPRESSION accordingly.
 */
#define GLECMA_FNCTX_DECLARATION 0x200u

/**
 * @brief What one function needs to be read again: its range, its kind and the
 *   context it had.
 *
 * Size-first. A function that is compiled when it is first called is read again
 * from its range of the source, with this, and gives the tree it gave the first
 * time (equal kinds, children and ranges, locations in the whole source). The
 * inputs are the ones the function's uncompiled constant holds:
 *
 *  - the range: `start` and `end`, offsets in the source's own units. For a method,
 *    a getter, a setter or a constructor it is the text of its `toString`: from the
 *    first modifier or the name to the closing brace (`static` is not part of
 *    it); for a field initializer, the initializer expression; for a static
 *    block, from `static` to the closing brace;
 *  - the `kind`;
 *  - `flags`: ::GLECMA_CONTEXT_STRICT, whether the function's code is strict,
 *    inherited or by its own directive (either gives the same tree); and the
 *    context of the code around the function (``GLECMA_CONTEXT_NEW_TARGET`` and
 *    the others of ::GLECMA_CONTEXT_FUNCTION_MASK): whether `super` property,
 *    `super` call, `new.target`, `yield` and `await` are allowed there, whether it is
 *    inside a class field initializer or a class static block, and whether it is
 *    module code. A function takes what its kind gives it (its own `new.target`,
 *    `yield` if it is a generator, `await` if it is async, `super` property if it
 *    is a method) and an arrow function takes what is around it, so this is the
 *    context the first parse read the function's text in. A method, a field
 *    initializer and a static block are read in the context of the class (or the
 *    object literal) around them, and a class's code is strict: a constructor, a
 *    field initializer and a static block are read as strict code whatever the
 *    flag says, and a method of a class is given ::GLECMA_CONTEXT_STRICT;
 *  - `private_names`: the names a `#x` in the function may refer to, those of every
 *    class around it;
 *  - `features`: the feature set of the first parse. The call also takes the parse
 *    options, as every parse entry does, and the two name the same set: a different
 *    one would not give the first parse's tree, and is ::GLECMA_ERR_INVALID.
 *
 * The function's declaration-or-expression form is the one input beyond the
 * list the cross-milestone contract gives for the uncompiled-function constant:
 * ::GLECMA_FNCTX_DECLARATION.
 */
typedef struct GLECMA_FunctionContext {
  uint32_t size;               ///< `sizeof(GLECMA_FunctionContext)` as the caller knows it.
  uint32_t kind;               ///< A ::GLECMA_FNKIND_FUNCTION and the others.
  uint64_t start;              ///< The range's first unit.
  uint64_t end;                ///< One past its last.
  uint32_t flags;              ///< ::GLECMA_CONTEXT_STRICT and the others of ::GLECMA_CONTEXT_FUNCTION_MASK, and ::GLECMA_FNCTX_DECLARATION.
  uint32_t private_name_count; ///< Entries of `private_names`.
  const GLECMA_PrivateName * private_names; ///< The names in scope, or NULL when there are none.
  const GLECMA_FeatureSet * features; ///< The first parse's feature set, or NULL for none.
} GLECMA_FunctionContext;

/** @brief Initializer for a ::GLECMA_FunctionContext: its size, and nothing else. */
#define GLECMA_FUNCTION_CONTEXT_INIT                                           \
  { (uint32_t)sizeof(GLECMA_FunctionContext), 0u, 0u, 0u, 0u, 0u, NULL, NULL }

/**
 * @brief Reads one function again, from its range of its source, in the context
 *   it had.
 *
 * Reads exactly the range, as the one function its kind says, and nothing
 * outside it: what follows the range is not read, so the same text gives the same
 * tree wherever the function was and whatever came after it. The first parse
 * reported every early error, so a range that does not read as that function,
 * with that context, is a defect of the library or of the caller's record, and is
 * ::GLECMA_ERR_INTERNAL, never ::GLECMA_ERR_SYNTAX. (A cap the options set lower
 * than the first parse's, or a native stack too small, is still ::GLECMA_ERR_LIMIT,
 * and an allocation that fails ::GLECMA_ERR_OOM.)
 *
 * The tree's root is the function's own node: a ::GLECMA_NODE_FUNCTION_DECLARATION,
 * ::GLECMA_NODE_FUNCTION_EXPRESSION or ::GLECMA_NODE_ARROW_FUNCTION; for a method, a
 * getter, a setter and a constructor the ::GLECMA_NODE_FUNCTION_EXPRESSION of the
 * member (its key is read to find where the parameters begin and is not in the tree);
 * for a field initializer its expression; for a static block its
 * ::GLECMA_NODE_STATIC_BLOCK. Ranges are offsets in the whole source. The goal is
 * ::GLECMA_PARSE_FUNCTION and the strictness that of the function's code.
 *
 * @param source The source the function was first read in.
 * @param context The function's range, kind and context.
 * @param options The parse options, or NULL for the defaults. The goal,
 *   strictness and eval context must be the defaults.
 * @param allocator Allocator for the tree, or NULL for the default.
 * @param failure Optional, as for ::glecma_parse.
 * @param out_tree Receives the tree; written only on success.
 * @return ::GLECMA_OK; ::GLECMA_ERR_INVALID for NULL arguments, a `size` below the
 *   header, a kind or flags that are not defined, a flag without the flag it
 *   implies, a range that is empty, backwards, past the source or inside a code
 *   point, a feature set that differs from the options', or options this entry
 *   does not take; ::GLECMA_ERR_UNSUPPORTED for a feature the build lacks;
 *   ::GLECMA_ERR_INTERNAL for a range that does not read again; ::GLECMA_ERR_LIMIT;
 *   ::GLECMA_ERR_OOM, with nothing leaked.
 */
GLECMA_API GLECMA_Result glecma_parse_function(GLECMA_Source * source,
    const GLECMA_FunctionContext * context, const GLECMA_ParseOptions * options,
    const GLECMA_Allocator * allocator, GLECMA_Failure * failure,
    GLECMA_Tree ** out_tree);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GLECMA_GOALS_H

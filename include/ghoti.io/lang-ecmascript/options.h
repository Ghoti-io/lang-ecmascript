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
 * @file options.h
 * @stability free
 *
 * The parse options and the feature set: what every entry that tokenizes, and
 * later every entry that parses, takes besides the source.
 *
 * The pinned edition (ECMAScript 2026 with Annex B) is what an empty feature
 * set accepts. A feature set names features that are finished at TC39 and not
 * in the pinned edition, so that one can be built and scored before an
 * edition bump turns it on. This library defines no feature yet, so an entry
 * that is given a set naming any feature refuses it, before it reads a single
 * token, with ::GLECMA_ERR_UNSUPPORTED and the feature named in
 * ::GLECMA_Failure::feature: a caller that asked for a feature and silently
 * got the plain edition would hold a wrong parse and no sign of it.
 */

#ifndef GHOTI_IO_GLECMA_OPTIONS_H
#define GHOTI_IO_GLECMA_OPTIONS_H

#include <ghoti.io/lang-ecmascript/macros.h>

#include <ghoti.io/lang-ecmascript/core.h>
#include <ghoti.io/lang-ecmascript/limits.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A set of feature names.
 *
 * Size-first: the first member is the size of the struct the caller was
 * compiled against, and the set holds as many names as that size has room
 * for after the header (sixteen in this version), so a caller that declares
 * the header alone holds none. The library touches only the bytes within
 * `size`; room for more names, if it is ever needed, is added at the end. Initialize with
 * ::GLECMA_FEATURE_SET_INIT. Names are lowercase ASCII words: letters,
 * digits and hyphens, beginning with a letter, shorter than
 * ::GLECMA_FEATURE_NAME_MAX.
 *
 * A set is a plain value with no allocation behind it: copy it, and it needs
 * no freeing.
 */
typedef struct GLECMA_FeatureSet {
  uint32_t size;  ///< `sizeof(GLECMA_FeatureSet)` as the caller knows it.
  uint32_t count; ///< Names held. Written only by ::glecma_feature_set_add.
  char names[16][GLECMA_FEATURE_NAME_MAX]; ///< The names, in the order they
                                           ///< were added, each
                                           ///< NUL-terminated.
} GLECMA_FeatureSet;

/** @brief Initializer for a ::GLECMA_FeatureSet: its size, and no features. */
#define GLECMA_FEATURE_SET_INIT                                                \
  { (uint32_t)sizeof(GLECMA_FeatureSet), 0u, {{0}} }

/**
 * @brief Adds a feature name to a set.
 *
 * Adding a name already in the set changes nothing. The name is not checked
 * against the features the build defines: a set can name a feature this build
 * lacks, and it is the entry that is given the set that refuses it.
 *
 * @param set A set initialized with ::GLECMA_FEATURE_SET_INIT.
 * @param name The feature's name.
 * @return ::GLECMA_OK; ::GLECMA_ERR_INVALID for a NULL or malformed argument,
 *   a name that is empty, too long or not of the form above, or a `size`
 *   below the header or a `count` the size has no room for;
 *   ::GLECMA_ERR_LIMIT when the set has no room for another name.
 */
GLECMA_API GLECMA_Result glecma_feature_set_add(GLECMA_FeatureSet * set,
    const char * name);

/**
 * @brief Whether a set holds a feature name.
 *
 * @param set The set. NULL is an empty set.
 * @param name The feature's name.
 * @return true when @p name is in @p set; false for a NULL or absent name.
 */
GLECMA_API bool glecma_feature_set_has(const GLECMA_FeatureSet * set,
    const char * name);

/**
 * @name Goals
 *
 * Which grammar a source is parsed for. A value that is no goal is
 * ::GLECMA_ERR_INVALID.
 *
 * ::glecma_parse serves the first four. The dynamic-function goal and the
 * function re-parse goal have entries of their own, which take more than a
 * source (`goals.h`); their numbers are here because a tree records the goal
 * it was parsed for, and ::glecma_parse refuses them.
 */
///@{
#define GLECMA_PARSE_SCRIPT 0u ///< A Script, sloppy unless a directive or the context makes it strict.
#define GLECMA_PARSE_MODULE 1u ///< A Module (clause 16.2): always strict; `import` and `export` declarations, top-level `await` and `import.meta`.
#define GLECMA_PARSE_DIRECT_EVAL 2u ///< Direct eval code (clause 19.2.1.1): a Script read in the context of the code that called `eval`, which ::GLECMA_ParseOptions::eval_context describes.
#define GLECMA_PARSE_INDIRECT_EVAL 3u ///< Indirect eval code (clause 19.2.1.1): a Script in the global context; it takes no context, and no inherited strictness.
#define GLECMA_PARSE_DYNAMIC_FUNCTION 4u ///< A function made by CreateDynamicFunction (clause 20.2.1.1.1): `glecma_parse_dynamic_function`.
#define GLECMA_PARSE_FUNCTION 5u ///< One function read again from its source range: `glecma_parse_function`.
///@}

/**
 * @name Context flags
 *
 * What the host knows about the code that the source cannot say. They are the
 * flags of the code *around* what is parsed. Strictness can only be added,
 * never removed: a source's own directive can make code strict, and nothing
 * can make strict code sloppy.
 *
 * ::GLECMA_ParseOptions::context takes ::GLECMA_CONTEXT_STRICT alone, a Script's
 * or a Module's strictness before its first token. The direct eval goal takes
 * the first five in ::GLECMA_EvalContext::flags, and the function re-parse
 * goal takes all of them in ::GLECMA_FunctionContext::flags.
 */
///@{
#define GLECMA_CONTEXT_STRICT 0x1u ///< The code is strict before its first token.
#define GLECMA_CONTEXT_NEW_TARGET 0x2u ///< `new.target` is allowed: the code is inside a function.
#define GLECMA_CONTEXT_SUPER_PROPERTY 0x4u ///< `super.x` is allowed: inside a method, a class field initializer or a static block. Implies ::GLECMA_CONTEXT_NEW_TARGET.
#define GLECMA_CONTEXT_SUPER_CALL 0x8u ///< `super()` is allowed: inside the constructor of a derived class. Implies ::GLECMA_CONTEXT_SUPER_PROPERTY.
#define GLECMA_CONTEXT_FIELD_INITIALIZER 0x10u ///< `arguments` is not a name: inside a class field initializer or a class static block, an arrow function in either included. Implies ::GLECMA_CONTEXT_SUPER_PROPERTY.
#define GLECMA_CONTEXT_STATIC_BLOCK 0x20u ///< The code of a class static block itself: `arguments` is not a name and `await` is reserved. An arrow function in a static block takes ::GLECMA_CONTEXT_FIELD_INITIALIZER, not this: its body is not the block's own code. Implies ::GLECMA_CONTEXT_SUPER_PROPERTY.
#define GLECMA_CONTEXT_YIELD 0x40u ///< `yield` is an operator: inside a generator.
#define GLECMA_CONTEXT_AWAIT 0x80u ///< `await` is an operator: inside an async function, or a Module's top level.
#define GLECMA_CONTEXT_MODULE 0x100u ///< The code is a Module's, and `await` is reserved in it and in every function inside it. Implies ::GLECMA_CONTEXT_STRICT.
/** What ::GLECMA_ParseOptions::context accepts. */
#define GLECMA_CONTEXT_MASK 0x1u
/** What ::GLECMA_EvalContext::flags accepts. */
#define GLECMA_CONTEXT_EVAL_MASK 0x1Fu
/** What ::GLECMA_FunctionContext::flags accepts, beside ::GLECMA_FNCTX_DECLARATION. */
#define GLECMA_CONTEXT_FUNCTION_MASK 0x1FFu
///@}

/**
 * @brief The name of a private member (`#x`), without the `#`.
 *
 * UTF-16 code units, as the tree's texts are. The units are the caller's and
 * must outlive the call they are passed to.
 */
typedef struct GLECMA_PrivateName {
  const uint16_t * units; ///< The name's code units, not NUL-terminated.
  uint32_t length;        ///< How many; at least one.
  uint32_t reserved;      ///< Padding; must be zero.
} GLECMA_PrivateName;

/**
 * @brief The context direct eval code inherits from the code that called
 *   `eval` (clause 19.2.1.1).
 *
 * Size-first: the first member is the size of the struct the caller was
 * compiled against; a member beyond it takes its default. Initialize with
 * ::GLECMA_EVAL_CONTEXT_INIT or ::glecma_eval_context_default.
 *
 * `flags` is a combination of the first five ::GLECMA_CONTEXT_STRICT ...
 * ::GLECMA_CONTEXT_FIELD_INITIALIZER, each implying the one before it where its
 * comment says so (a flag whose implied flag is missing is
 * ::GLECMA_ERR_INVALID). The private names are the names the code that called
 * `eval` could write as `#x`: those of every class around it. A `#x` in the
 * eval code that no class in the code itself declares must be one of them
 * (AllPrivateIdentifiersValid, clause 15.7.1), or the eval code is
 * ::GLECMA_RULE_PRIVATE_NAME_UNDECLARED. The parse copies what it needs and
 * the caller's memory may be released when the call returns; the tree keeps its
 * own copy (::glecma_tree_eval_context).
 */
typedef struct GLECMA_EvalContext {
  uint32_t size;               ///< `sizeof(GLECMA_EvalContext)` as the caller knows it.
  uint32_t flags;              ///< ::GLECMA_CONTEXT_STRICT and the other flags of ::GLECMA_CONTEXT_EVAL_MASK.
  uint32_t private_name_count; ///< Entries of `private_names`.
  uint32_t reserved;           ///< Padding; must be zero.
  const GLECMA_PrivateName * private_names; ///< The names in scope, or NULL when there are none.
} GLECMA_EvalContext;

/** @brief Initializer for a ::GLECMA_EvalContext: its size, and the global context. */
#define GLECMA_EVAL_CONTEXT_INIT                                               \
  { (uint32_t)sizeof(GLECMA_EvalContext), 0u, 0u, 0u, NULL }

/**
 * @brief Writes the global context (no flags, no private names) into @p context.
 *
 * Reads `context->size` and writes only the members within it.
 *
 * @param context Caller-owned storage with `size` set. NULL, or a `size` below
 *   the header, is ::GLECMA_ERR_INVALID and writes nothing.
 * @return ::GLECMA_OK, or ::GLECMA_ERR_INVALID.
 */
GLECMA_API GLECMA_Result glecma_eval_context_default(
    GLECMA_EvalContext * context);

/**
 * @brief What every entry that tokenizes or parses takes.
 *
 * Size-first, like ::GLECMA_Limits: the first member is the size of the
 * struct the caller was compiled against, set with
 * ::GLECMA_PARSE_OPTIONS_INIT or ::glecma_parse_options_default, and a member
 * beyond it takes its default, so a caller built before a member was added
 * keeps working. Members are only ever added at the end. A `size` smaller
 * than the header is ::GLECMA_ERR_INVALID. `reserved` must be zero.
 *
 * The limits and the feature set are what the tokenizer reads; the goal, its
 * context flags and the eval context are what the parser adds. No member
 * selects the internal built-in mode of the tokenizer: that mode is reached
 * only through an internal entry, so user source can never spell an intrinsic.
 */
typedef struct GLECMA_ParseOptions {
  uint32_t size;     ///< `sizeof(GLECMA_ParseOptions)` as the caller knows it.
  uint32_t reserved; ///< Padding; must be zero.
  const GLECMA_Limits * limits;       ///< The caps, or NULL for the defaults.
                                      ///< Must outlive the call it is passed
                                      ///< to.
  const GLECMA_FeatureSet * features; ///< The features asked for, or NULL for
                                      ///< none. Must outlive the call.
  uint32_t goal;      ///< A ::GLECMA_PARSE_SCRIPT and the other goals.
  uint32_t context;   ///< ::GLECMA_CONTEXT_STRICT; bits beyond
                      ///< ::GLECMA_CONTEXT_MASK must be zero.
  const GLECMA_EvalContext * eval_context; ///< For ::GLECMA_PARSE_DIRECT_EVAL
                      ///< only: the context of the code that called `eval`,
                      ///< or NULL for the global context. Any other goal
                      ///< refuses a context with ::GLECMA_ERR_INVALID. Must
                      ///< outlive the call.
} GLECMA_ParseOptions;

/** @brief Initializer for a ::GLECMA_ParseOptions: its size, and defaults. */
#define GLECMA_PARSE_OPTIONS_INIT                                              \
  { (uint32_t)sizeof(GLECMA_ParseOptions), 0u, NULL, NULL, GLECMA_PARSE_SCRIPT, \
    0u, NULL }

/**
 * @brief Writes the default options into @p options.
 *
 * Reads `options->size` and writes only the members within it, so it never
 * writes past the struct the caller was compiled against.
 *
 * @param options Caller-owned storage with `size` set. NULL, or a `size`
 *   below the header, is ::GLECMA_ERR_INVALID and writes nothing.
 * @return ::GLECMA_OK, or ::GLECMA_ERR_INVALID.
 */
GLECMA_API GLECMA_Result glecma_parse_options_default(
    GLECMA_ParseOptions * options);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GLECMA_OPTIONS_H

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
 * @brief What every entry that tokenizes or parses takes.
 *
 * Size-first, like ::GLECMA_Limits: the first member is the size of the
 * struct the caller was compiled against, set with
 * ::GLECMA_PARSE_OPTIONS_INIT or ::glecma_parse_options_default, and a member
 * beyond it takes its default, so a caller built before a member was added
 * keeps working. Members are only ever added at the end. A `size` smaller
 * than the header is ::GLECMA_ERR_INVALID. `reserved` must be zero.
 *
 * The limits and the feature set are what the tokenizer reads; the goal and
 * its context are what the parser adds. No member selects the internal built-in mode of the tokenizer: that mode
 * is reached only through an internal entry, so user source can never spell
 * an intrinsic.
 */
/**
 * @name Goals
 *
 * Which grammar a source is parsed for. A value that is no goal is
 * ::GLECMA_ERR_INVALID.
 */
///@{
#define GLECMA_PARSE_SCRIPT 0u ///< A Script, sloppy unless a directive or the context makes it strict.
#define GLECMA_PARSE_MODULE 1u ///< A Module (clause 16.2): always strict; `import` and `export` declarations, top-level `await` and `import.meta`.
///@}

/**
 * @name Context flags
 *
 * What the host knows about the code that the source cannot say.
 * Strictness can only be added, never removed: a source's own directive can
 * make code strict, and nothing can make strict code sloppy.
 */
///@{
#define GLECMA_CONTEXT_STRICT 0x1u ///< The code is strict before its first token.
#define GLECMA_CONTEXT_MASK 0x1u   ///< Every bit defined; the rest must be zero.
///@}

typedef struct GLECMA_ParseOptions {
  uint32_t size;     ///< `sizeof(GLECMA_ParseOptions)` as the caller knows it.
  uint32_t reserved; ///< Padding; must be zero.
  const GLECMA_Limits * limits;       ///< The caps, or NULL for the defaults.
                                      ///< Must outlive the call it is passed
                                      ///< to.
  const GLECMA_FeatureSet * features; ///< The features asked for, or NULL for
                                      ///< none. Must outlive the call.
  uint32_t goal;      ///< ::GLECMA_PARSE_SCRIPT or ::GLECMA_PARSE_MODULE.
  uint32_t context;   ///< ::GLECMA_CONTEXT_STRICT and the other flags; bits
                      ///< beyond ::GLECMA_CONTEXT_MASK must be zero.
} GLECMA_ParseOptions;

/** @brief Initializer for a ::GLECMA_ParseOptions: its size, and defaults. */
#define GLECMA_PARSE_OPTIONS_INIT                                              \
  { (uint32_t)sizeof(GLECMA_ParseOptions), 0u, NULL, NULL, GLECMA_PARSE_SCRIPT, \
    0u }

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

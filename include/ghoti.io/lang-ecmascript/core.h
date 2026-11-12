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
 * @file core.h
 * @stability free
 *
 * Result codes, the failure record and source location every parse call
 * reports through, the version this build reports, and the edition of the
 * language it implements.
 */

#ifndef GHOTI_IO_GLECMA_CORE_H
#define GHOTI_IO_GLECMA_CORE_H

#include <ghoti.io/lang-ecmascript/macros.h>

#include <ghoti.io/lang-ecmascript/allocator.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The edition of ECMA-262 this library implements: the 17th,
 *   ECMAScript 2026, approved by the Ecma General Assembly on 30 June 2026.
 *
 * Annex B (additional features for web browsers) is in scope. ECMA-402
 * (internationalization) is a separate standard and is not implemented.
 * Moving to a later edition is a deliberate change of this value, of the
 * pinned conformance suite and of the documentation together, never a
 * feature picked up from a draft. `documentation/design.md` says what
 * conformance to this edition means and how it is measured.
 */
#define GLECMA_ECMA262_EDITION 17

/** @brief The year name of ::GLECMA_ECMA262_EDITION: ECMAScript 2026. */
#define GLECMA_ECMA262_YEAR 2026

/**
 * @brief Result of an operation.
 *
 * Zero is success. ::GLECMA_RESULT_COUNT closes the enum so a test can check
 * the string table is complete. The first nine are the suite's shared
 * vocabulary; a value is appended before the count only when an operation
 * needs to report something none of them means, so the nine keep their
 * numbers.
 *
 * ::GLECMA_ERR_SYNTAX is the first such value: source text that decodes
 * cleanly but is not ECMAScript for the goal it was parsed in. It is kept
 * apart from ::GLECMA_ERR_CORRUPT (the text is not well-formed UTF-8, so it
 * is not source text at all) and from ::GLECMA_ERR_INVALID (the caller's
 * arguments are wrong), so that a host can tell a script's author's mistake
 * from its own.
 */
typedef enum {
  GLECMA_OK = 0,          ///< The operation succeeded.
  GLECMA_ERR_IO,          ///< A read, write, or seek failed.
  GLECMA_ERR_FORMAT,      ///< Well-formed input, but not a format handled
                          ///< here.
  GLECMA_ERR_UNSUPPORTED, ///< A feature this library does not implement.
  GLECMA_ERR_LIMIT,       ///< A stated cap or budget was exceeded.
  GLECMA_ERR_CORRUPT,     ///< The input is not a valid encoding.
  GLECMA_ERR_OOM,         ///< The allocator returned NULL.
  GLECMA_ERR_INVALID,     ///< A caller-supplied argument is wrong.
  GLECMA_ERR_INTERNAL,    ///< The library's own invariant failed.
  GLECMA_ERR_SYNTAX,      ///< Well-formed source text that is not valid
                          ///< ECMAScript for the requested goal: a grammar
                          ///< error or an early error.
  GLECMA_RESULT_COUNT     ///< Not a result; closes the enum.
} GLECMA_Result;

/**
 * @brief A position in source text.
 *
 * `offset` counts the input's own units: bytes for UTF-8 source, 16-bit code
 * units for UTF-16 source. `line` is 1-based and advances at each of
 * ECMA-262's line terminators (LF, CR, CRLF as one, U+2028, U+2029); the LF of
 * a CRLF is on the CR's line. `column` is 0-based and counts UTF-16 code units
 * from the start of the line, whatever the input encoding, because that is
 * how source maps and browser developer tools count: a code point above
 * U+FFFF is two columns wide, and a UTF-8 host still gets columns a debugger
 * front end can use.
 */
typedef struct GLECMA_Location {
  size_t offset; ///< Index into the input, in input units.
  size_t line;   ///< 1-based line number.
  size_t column; ///< 0-based column, in UTF-16 code units.
} GLECMA_Location;

/**
 * @brief Why a call failed, written on every return when the caller asks.
 *
 * A call that takes a `GLECMA_Failure *` writes all of it on every return,
 * success included, whenever the pointer is not NULL; a field that does not
 * apply to the result is zero or NULL. Every string is static, never
 * allocated and never caller data, so the record needs no freeing and stays
 * valid for the life of the process.
 */
typedef struct GLECMA_Failure {
  GLECMA_Result result;     ///< The same code the function returned.
  const char * message;     ///< A static English description, or NULL.
  GLECMA_Location location; ///< Where in the source the failure is, for
                            ///< ::GLECMA_ERR_CORRUPT (the first byte of the
                            ///< first ill-formed sequence) and
                            ///< ::GLECMA_ERR_SYNTAX; all zero otherwise.
  const char * limit;       ///< For ::GLECMA_ERR_LIMIT, the static name of
                            ///< the ::GLECMA_Limits field that was exceeded;
                            ///< NULL otherwise.
} GLECMA_Failure;

/**
 * @brief Static description of a result code.
 *
 * The string is never NULL, never allocated, and never contains caller data.
 *
 * @param result The result code, including values outside the enum.
 * @return A static string; "Unknown error" for a value outside the enum.
 */
GLECMA_API const char * glecma_result_string(GLECMA_Result result);

/**
 * @brief This build's version, as the string the Makefile generated.
 *
 * @return A static string, never NULL. Equal to ::GLECMA_VERSION_STRING of
 *   the headers the library was built with.
 */
GLECMA_API const char * glecma_version_string(void);

/**
 * @brief This build's version, packed as ::GLECMA_MAKE_VERSION packs it.
 *
 * @return `(major << 16) | (minor << 8) | patch`.
 */
GLECMA_API unsigned glecma_version_number(void);

/**
 * @brief The ECMA-262 edition the library was built to implement.
 *
 * A host that loads the shared library at run time compares this with the
 * ::GLECMA_ECMA262_EDITION of the headers it was compiled against.
 *
 * @return ::GLECMA_ECMA262_EDITION as the library saw it.
 */
GLECMA_API unsigned glecma_ecma262_edition(void);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GLECMA_CORE_H

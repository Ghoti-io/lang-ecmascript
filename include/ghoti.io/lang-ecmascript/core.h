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
 * Result codes, the version this build reports, and the edition of the
 * language it implements.
 */

#ifndef GHOTI_IO_GLECMA_CORE_H
#define GHOTI_IO_GLECMA_CORE_H

#include <ghoti.io/lang-ecmascript/macros.h>

#include <ghoti.io/lang-ecmascript/allocator.h>

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
 * the string table is complete. These are the suite's shared nine; a value is
 * appended before the count only when an operation needs to report something
 * none of them means (a script's early error, an uncaught guest exception),
 * so the nine keep their numbers.
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
  GLECMA_RESULT_COUNT     ///< Not a result; closes the enum.
} GLECMA_Result;

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

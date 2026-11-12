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
 * @file namespace.h
 * @stability free
 *
 * Maps every public name of this library into its version namespace.
 *
 * One list, included from macros.h, so a type rename is in effect before any
 * struct tag that uses the name. A rename spread across headers fails when an
 * internal header defines a struct tag without including the public header
 * that declares its typedef, and the error is nowhere near its cause.
 *
 * Every function and type a public header declares is listed here, and
 * `make check-symbols` fails on an exported symbol that escaped the list.
 */

#ifndef GHOTI_IO_GLECMA_NAMESPACE_H
#define GHOTI_IO_GLECMA_NAMESPACE_H

#include <ghoti.io/lang-ecmascript/libver.h>

/// @cond HIDDEN_SYMBOLS

// core.h and allocator.h
#define GLECMA_Allocator GHOTIIO_LANG_ECMASCRIPT(GLECMA_Allocator)
#define GLECMA_Result GHOTIIO_LANG_ECMASCRIPT(GLECMA_Result)
#define glecma_allocator_default GHOTIIO_LANG_ECMASCRIPT(glecma_allocator_default)
#define glecma_ecma262_edition GHOTIIO_LANG_ECMASCRIPT(glecma_ecma262_edition)
#define glecma_result_string GHOTIIO_LANG_ECMASCRIPT(glecma_result_string)
#define glecma_version_number GHOTIIO_LANG_ECMASCRIPT(glecma_version_number)
#define glecma_version_string GHOTIIO_LANG_ECMASCRIPT(glecma_version_string)

// engine.h
#define glecma_engine_descriptor GHOTIIO_LANG_ECMASCRIPT(glecma_engine_descriptor)
#define glecma_engine_find GHOTIIO_LANG_ECMASCRIPT(glecma_engine_find)
#define glecma_engine_register GHOTIIO_LANG_ECMASCRIPT(glecma_engine_register)

// Internal (src/); hidden by -fvisibility=hidden, renamed to keep one rule.
#define glecma_result_from_grcore                                              \
  GHOTIIO_LANG_ECMASCRIPT(glecma_result_from_grcore)

/// @endcond

#endif // GHOTI_IO_GLECMA_NAMESPACE_H

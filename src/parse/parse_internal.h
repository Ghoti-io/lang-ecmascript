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
 * The parse entry that has more than the public one: the checking builder and
 * the internal built-in mode. Never installed.
 *
 * ::glecma_parse is this with no flags. The tests reach the checking builder
 * through it, and the image tool, which compiles the engine's own built-ins,
 * reaches the built-in mode through it; no public header declares it and no
 * parse option selects either.
 */

#ifndef GHOTI_IO_GLECMA_SRC_PARSE_PARSE_INTERNAL_H
#define GHOTI_IO_GLECMA_SRC_PARSE_PARSE_INTERNAL_H

#include <ghoti.io/lang-ecmascript/macros.h>

#include <ghoti.io/lang-ecmascript/tree.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The built-in entry: a strict Script in which `%Name` is an intrinsic. */
#define GLECMA_PARSE_INTERNAL_BUILTIN 0x1u
/** Build with the checking builder: no function body is materialized. */
#define GLECMA_PARSE_INTERNAL_CHECKING 0x2u
/**
 * @brief Parses a source, with the internal flags.
 *
 * Takes what ::glecma_parse takes and returns what it returns.
 *
 * @param internal ::GLECMA_PARSE_INTERNAL_BUILTIN and
 *   ::GLECMA_PARSE_INTERNAL_CHECKING, or 0.
 */
GLECMA_INTERNAL_API GLECMA_Result glecma_parse_internal(GLECMA_Source * source,
    const GLECMA_ParseOptions * options, const GLECMA_Allocator * allocator,
    uint32_t internal, GLECMA_Failure * failure, GLECMA_Tree ** out_tree);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GLECMA_SRC_PARSE_PARSE_INTERNAL_H

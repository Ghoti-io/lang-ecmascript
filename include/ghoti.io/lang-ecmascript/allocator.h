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
 * @file allocator.h
 * @stability free
 *
 * Allocator abstraction for the Ghoti.io Lang-ecmascript library.
 *
 * This is cutil's @ref GCU_Allocator under a local name, the arrangement every
 * library in the suite uses. One definition across the suite means an
 * allocator written for any of them works with all of them, rather than
 * needing a near-identical copy per library.
 *
 * Guest memory - objects, strings, environments - will not come from here: it
 * belongs to the runtime-core context the engine runs in, whose counting
 * allocator charges it against the context's memory budget. This allocator is
 * for what the library allocates outside any context.
 */

#ifndef GHOTI_IO_GLECMA_ALLOCATOR_H
#define GHOTI_IO_GLECMA_ALLOCATOR_H

#include <ghoti.io/lang-ecmascript/macros.h>

#include <ghoti.io/cutil/allocator.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Allocator interface used by the library.
 *
 * All function pointers must be non-NULL. Each receives the `ctx` pointer from
 * the struct as its first argument.
 *
 * Two requirements beyond the C library equivalents: `calloc_fn` must treat
 * overflow of `nitems * size` as an allocation failure and return NULL rather
 * than allocating a truncated block, and a zero-size request should return a
 * usable non-NULL pointer, so that NULL always means failure.
 */
typedef GCU_Allocator GLECMA_Allocator;

/**
 * @brief Get the default allocator (stdlib-backed).
 *
 * @return Pointer to a process-global allocator instance; never NULL.
 */
GLECMA_API const GLECMA_Allocator * glecma_allocator_default(void);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GLECMA_ALLOCATOR_H

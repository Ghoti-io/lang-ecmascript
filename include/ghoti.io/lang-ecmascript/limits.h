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
 * @file limits.h
 * @stability free
 *
 * Caps on what a hostile input may cost. A NULL limits pointer means
 * ::glecma_limits_default. A field of zero is a real cap of zero, not a
 * request for the default.
 *
 * Each cap is checked before the allocation that would satisfy it, and
 * exceeding one is ::GLECMA_ERR_LIMIT with the field's name in
 * ::GLECMA_Failure::limit - never a truncated result and never
 * ::GLECMA_ERR_OOM. A field is added here in the same change that enforces
 * it, with its default and a test that drives it one past the cap.
 */

#ifndef GHOTI_IO_GLECMA_LIMITS_H
#define GHOTI_IO_GLECMA_LIMITS_H

#include <ghoti.io/lang-ecmascript/macros.h>

#include <ghoti.io/lang-ecmascript/core.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The default for ::GLECMA_Limits::source_units: 64 Mi units.
 *
 * Real bundles reach tens of megabytes, and a source costs about 1.03 times
 * its input (the copy, and one location checkpoint per 1024 units), so the
 * default admits them while still bounding what a hostile host-supplied
 * string can make the library allocate. A host that needs more raises it
 * deliberately.
 */
#define GLECMA_LIMIT_SOURCE_UNITS_DEFAULT (UINT64_C(64) * 1024 * 1024)

/**
 * @brief Caps checked before the allocation they bound.
 *
 * The first member is the size of the struct the caller was compiled
 * against; set it with ::GLECMA_LIMITS_INIT. The library reads only the
 * members that lie wholly within that size and takes the default for any
 * later one, so a caller built against a shorter struct keeps working when
 * a field is added (fields are only ever added at the end, and never
 * removed or reordered). A `size` smaller than the header is
 * ::GLECMA_ERR_INVALID.
 *
 * Members are fixed-width integers with the padding written out, and `reserved`
 * must be zero. A failure names the field in ::GLECMA_Failure::limit, spelled
 * as the member is; the names are kept in an append-only registry
 * (tools/limit-names.txt) so that a name a host matches on is never changed.
 */
typedef struct GLECMA_Limits {
  uint32_t size;         ///< `sizeof(GLECMA_Limits)` as the caller knows it.
  uint32_t reserved;     ///< Padding; must be zero.
  uint64_t source_units; ///< Input units of one source: bytes for UTF-8,
                         ///< 16-bit code units for UTF-16.
} GLECMA_Limits;

/** @brief Initializer for a ::GLECMA_Limits: its size, and every default. */
#define GLECMA_LIMITS_INIT                                                     \
  { (uint32_t)sizeof(GLECMA_Limits), 0u, GLECMA_LIMIT_SOURCE_UNITS_DEFAULT }

/**
 * @brief Writes the default caps into @p limits.
 *
 * Reads `limits->size` (set it with ::GLECMA_LIMITS_INIT or by hand) and
 * writes only the members within it, so it never writes past the struct the
 * caller was compiled against.
 *
 * @param limits Caller-owned storage with `size` set. NULL, or a `size` below
 *   the header, is ::GLECMA_ERR_INVALID and writes nothing.
 * @return ::GLECMA_OK, or ::GLECMA_ERR_INVALID.
 */
GLECMA_API GLECMA_Result glecma_limits_default(GLECMA_Limits * limits);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GLECMA_LIMITS_H

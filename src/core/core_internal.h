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
 * Declarations shared between the library's modules. Never installed.
 */

#ifndef GHOTI_IO_GLECMA_SRC_CORE_CORE_INTERNAL_H
#define GHOTI_IO_GLECMA_SRC_CORE_CORE_INTERNAL_H

#include <ghoti.io/lang-ecmascript/macros.h>

#include <ghoti.io/lang-ecmascript/core.h>
#include <ghoti.io/lang-ecmascript/limits.h>
#include <ghoti.io/runtime-core/core.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Bytes of the `size` and `reserved` members of a size-first struct. */
#define GLECMA_LIMITS_HEADER_SIZE 8u
#define GLECMA_FAILURE_HEADER_SIZE 8u

/**
 * @brief Converts a runtime-core result into this library's.
 *
 * The shared nine have the same meaning in both vocabularies and convert one
 * to one. ::GRCORE_ERR_GUEST, and any value runtime-core adds later, has no
 * counterpart yet, so it becomes ::GLECMA_ERR_INTERNAL: reaching it means a
 * call returned something its contract did not list, which is this library's
 * defect to find, not a result to pass on as if it were understood.
 *
 * @param result A runtime-core result.
 * @return The corresponding result.
 */
GLECMA_INTERNAL_API GLECMA_Result glecma_result_from_grcore(GRCORE_Result result);

/**
 * @brief Resolves a caller's limits against the defaults.
 *
 * @param limits The caller's limits, or NULL for the defaults.
 * @param out Receives every member: the caller's where its `size` covers
 *   them, the default elsewhere.
 * @return false when @p limits has a `size` below the header (the caller
 *   must report ::GLECMA_ERR_INVALID).
 */
GLECMA_INTERNAL_API bool glecma_limits_resolve(const GLECMA_Limits * limits,
    GLECMA_Limits * out);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GLECMA_SRC_CORE_CORE_INTERNAL_H

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
 * The default caps. documentation/design.md records why each default is what
 * it is.
 */

#include <ghoti.io/lang-ecmascript/macros.h>

#include <ghoti.io/lang-ecmascript/limits.h>

#include "core/core_internal.h"

#include <stdbool.h>
#include <stddef.h>

// A member is within the caller's struct when it ends at or before `size`.
#define WITHIN(size, member)                                                   \
  ((size_t)(size) >= offsetof(GLECMA_Limits, member) + sizeof(((GLECMA_Limits *)0)->member))

GLECMA_Result glecma_limits_default(GLECMA_Limits * limits) {
  if (limits == NULL || limits->size < GLECMA_LIMITS_HEADER_SIZE) {
    return GLECMA_ERR_INVALID;
  }
  limits->reserved = 0;
  if (WITHIN(limits->size, source_units)) {
    limits->source_units = GLECMA_LIMIT_SOURCE_UNITS_DEFAULT;
  }
  if (WITHIN(limits->size, tokens)) {
    limits->tokens = GLECMA_LIMIT_TOKENS_DEFAULT;
  }
  if (WITHIN(limits->size, literal_length)) {
    limits->literal_length = GLECMA_LIMIT_LITERAL_LENGTH_DEFAULT;
  }
  if (WITHIN(limits->size, nesting)) {
    limits->nesting = GLECMA_LIMIT_NESTING_DEFAULT;
  }
  if (WITHIN(limits->size, nodes)) {
    limits->nodes = GLECMA_LIMIT_NODES_DEFAULT;
  }
  if (WITHIN(limits->size, native_stack_bytes)) {
    limits->native_stack_bytes = GLECMA_LIMIT_NATIVE_STACK_BYTES_DEFAULT;
  }
  return GLECMA_OK;
}

bool glecma_limits_resolve(const GLECMA_Limits * limits, GLECMA_Limits * out) {
  GLECMA_Limits resolved = GLECMA_LIMITS_INIT;
  if (limits != NULL) {
    if (limits->size < GLECMA_LIMITS_HEADER_SIZE) {
      return false;
    }
    if (WITHIN(limits->size, source_units)) {
      resolved.source_units = limits->source_units;
    }
    if (WITHIN(limits->size, tokens)) {
      resolved.tokens = limits->tokens;
    }
    if (WITHIN(limits->size, literal_length)) {
      resolved.literal_length = limits->literal_length;
    }
    if (WITHIN(limits->size, nesting)) {
      resolved.nesting = limits->nesting;
    }
    if (WITHIN(limits->size, nodes)) {
      resolved.nodes = limits->nodes;
    }
    if (WITHIN(limits->size, native_stack_bytes)) {
      resolved.native_stack_bytes = limits->native_stack_bytes;
    }
  }
  *out = resolved;
  return true;
}

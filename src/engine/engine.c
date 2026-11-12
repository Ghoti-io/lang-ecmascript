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
 * The engine descriptor and its registration with a runtime-core context.
 */

#include <ghoti.io/lang-ecmascript/macros.h>

#include <ghoti.io/lang-ecmascript/engine.h>

#include "core/core_internal.h"

#include <stddef.h>
#include <stdint.h>

// Every callback is NULL until the interpreter has frames for it to describe:
// runtime-core reads a NULL callback as "nothing to say" (every slot raw, no
// location, hexadecimal inspection, no scopes, no roots beyond VALUE slots),
// never as an error. The decoder's zero mask decodes nothing.
static const GRCORE_EngineDescriptor glecma_descriptor =
    GRCORE_ENGINE_DESCRIPTOR_INIT(GLECMA_ENGINE_NAME, NULL, NULL, NULL,
        {NULL, NULL, NULL}, {0, 0, 0}, NULL, NULL, NULL, NULL);

const GRCORE_EngineDescriptor * glecma_engine_descriptor(void) {
  return &glecma_descriptor;
}

GLECMA_Result glecma_engine_find(const GRCORE_Context * context,
    GRCORE_EngineId * out_id) {
  if (context == NULL || out_id == NULL) {
    return GLECMA_ERR_INVALID;
  }
  // Ids run from 1 to the count; runtime-core never reuses or removes one
  // while the context lives.
  size_t count = grcore_engine_count(context);
  for (size_t i = 1; i <= count && i <= (size_t)UINT32_MAX; i++) {
    GRCORE_EngineId id = (GRCORE_EngineId)i;
    if (grcore_engine_descriptor(context, id) == &glecma_descriptor) {
      *out_id = id;
      return GLECMA_OK;
    }
  }
  return GLECMA_ERR_INVALID;
}

GLECMA_Result glecma_engine_register(GRCORE_Context * context,
    GRCORE_EngineId * out_id) {
  if (context == NULL || out_id == NULL) {
    return GLECMA_ERR_INVALID;
  }
  // The same conditions grcore_engine_register checks, applied before the
  // lookup too: reading the engine table is the owner's, and a second call
  // must refuse what the first would have refused rather than answer from a
  // table the caller had no right to read.
  if (!grcore_context_is_owner(context) ||
      grcore_context_state(context) == GRCORE_CONTEXT_RUNNING) {
    return GLECMA_ERR_INVALID;
  }
  GRCORE_EngineId id = 0;
  if (glecma_engine_find(context, &id) == GLECMA_OK) {
    *out_id = id;
    return GLECMA_OK;
  }
  GLECMA_Result result = glecma_result_from_grcore(
      grcore_engine_register(context, &glecma_descriptor, &id));
  if (result != GLECMA_OK) {
    return result;
  }
  if (id == 0) {
    return GLECMA_ERR_INTERNAL;
  }
  *out_id = id;
  return GLECMA_OK;
}

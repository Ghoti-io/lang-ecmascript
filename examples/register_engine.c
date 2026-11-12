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
 * @file register_engine.c
 *
 * Hosting the engine on a runtime-core context: make a group and a context,
 * register the ECMAScript engine with it, and look it up again.
 *
 * This is the first step every host will take before it runs a script, and
 * all the library does yet. A context is where the budgets live (fuel,
 * memory, stack depth) and what a debugger or collector walks; the engine id
 * is what an ECMAScript frame on that context's guest stack will carry.
 */

#include <ghoti.io/lang-ecmascript/lang-ecmascript.h>

#include <ghoti.io/runtime-core/b/group.h>

#include <stdio.h>

int main(void) {
  GRCORE_Group * group = NULL;
  GRCORE_Context * context = NULL;
  int status = 1;

  printf("lang-ecmascript %s implements ECMA-262 edition %u (ECMAScript %d)\n",
      glecma_version_string(), glecma_ecma262_edition(), GLECMA_ECMA262_YEAR);

  if (grcore_group_create(NULL, NULL, &group) != GRCORE_OK ||
      grcore_context_create(group, NULL, &context) != GRCORE_OK) {
    fprintf(stderr, "could not create a runtime-core context\n");
    goto done;
  }

  GRCORE_EngineId id = 0;
  GLECMA_Result result = glecma_engine_register(context, &id);
  if (result != GLECMA_OK) {
    fprintf(stderr, "register: %s\n", glecma_result_string(result));
    goto done;
  }
  // A second registration is not an error: it answers with the same id.
  GRCORE_EngineId again = 0;
  result = glecma_engine_register(context, &again);
  if (result != GLECMA_OK || again != id) {
    fprintf(stderr, "a second registration did not return the first id\n");
    goto done;
  }

  const GRCORE_EngineDescriptor * descriptor =
      grcore_engine_descriptor(context, id);
  printf("engine \"%s\" is id %u of %zu in this context\n", descriptor->name,
      (unsigned)id, grcore_engine_count(context));
  status = 0;

done:
  if (context != NULL && grcore_context_destroy(context) != GRCORE_OK) {
    status = 1;
  }
  if (group != NULL && grcore_group_destroy(group) != GRCORE_OK) {
    status = 1;
  }
  return status;
}

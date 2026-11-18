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
 * The feature set and the parse options.
 */

#include <ghoti.io/lang-ecmascript/macros.h>

#include <ghoti.io/lang-ecmascript/options.h>

#include "core/core_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define OPTIONS_WITHIN(size, member)                                           \
  ((size_t)(size) >= offsetof(GLECMA_ParseOptions, member) +                   \
          sizeof(((GLECMA_ParseOptions *)0)->member))

#define FEATURE_SET_HEADER_SIZE offsetof(GLECMA_FeatureSet, names)

// Names the caller's struct has room for.
static size_t feature_set_capacity(const GLECMA_FeatureSet * set) {
  if ((size_t)set->size < FEATURE_SET_HEADER_SIZE) {
    return 0;
  }
  size_t room = ((size_t)set->size - FEATURE_SET_HEADER_SIZE) /
      GLECMA_FEATURE_NAME_MAX;
  size_t declared = sizeof(set->names) / sizeof(set->names[0]);
  return room < declared ? room : declared;
}

// Lowercase letters, digits and hyphens, beginning with a letter.
static bool feature_name_valid(const char * name) {
  if (name == NULL || name[0] < 'a' || name[0] > 'z') {
    return false;
  }
  size_t length = 0;
  for (; name[length] != '\0'; length++) {
    char c = name[length];
    if (length + 1 >= GLECMA_FEATURE_NAME_MAX) {
      return false;
    }
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) {
      return false;
    }
  }
  return true;
}

GLECMA_Result glecma_feature_set_add(GLECMA_FeatureSet * set,
    const char * name) {
  if (set == NULL || (size_t)set->size < FEATURE_SET_HEADER_SIZE ||
      !feature_name_valid(name)) {
    return GLECMA_ERR_INVALID;
  }
  size_t capacity = feature_set_capacity(set);
  if (set->count > capacity) {
    return GLECMA_ERR_INVALID;
  }
  for (size_t i = 0; i < set->count; i++) {
    if (strcmp(set->names[i], name) == 0) {
      return GLECMA_OK;
    }
  }
  if (set->count == capacity) {
    return GLECMA_ERR_LIMIT;
  }
  size_t length = strlen(name);
  memcpy(set->names[set->count], name, length + 1);
  set->count++;
  return GLECMA_OK;
}

bool glecma_feature_set_has(const GLECMA_FeatureSet * set, const char * name) {
  if (set == NULL || name == NULL ||
      (size_t)set->size < FEATURE_SET_HEADER_SIZE) {
    return false;
  }
  size_t count = set->count;
  size_t capacity = feature_set_capacity(set);
  if (count > capacity) {
    count = capacity;
  }
  for (size_t i = 0; i < count; i++) {
    if (strcmp(set->names[i], name) == 0) {
      return true;
    }
  }
  return false;
}

// The names this build defines, from the registry. The registry is empty, so
// the table holds only its terminator.
static const char * const defined_features[] = {
#define GLECMA_FEATURE(identifier, name) name,
#include "core/features.def"
#undef GLECMA_FEATURE
  NULL
};

bool glecma_feature_built(const char * name) {
  if (name == NULL) {
    return false;
  }
  for (size_t i = 0; defined_features[i] != NULL; i++) {
    if (strcmp(defined_features[i], name) == 0) {
      return true;
    }
  }
  return false;
}

const char * glecma_feature_set_first_unbuilt(
    const GLECMA_FeatureSet * features) {
  if (features == NULL || (size_t)features->size < FEATURE_SET_HEADER_SIZE) {
    return NULL;
  }
  size_t count = features->count;
  size_t capacity = feature_set_capacity(features);
  if (count > capacity) {
    count = capacity;
  }
  for (size_t i = 0; i < count; i++) {
    if (!glecma_feature_built(features->names[i])) {
      return features->names[i];
    }
  }
  return NULL;
}

#define EVAL_WITHIN(size, member)                                              \
  ((size_t)(size) >= offsetof(GLECMA_EvalContext, member) +                    \
          sizeof(((GLECMA_EvalContext *)0)->member))

GLECMA_Result glecma_eval_context_default(GLECMA_EvalContext * context) {
  if (context == NULL || context->size < GLECMA_EVAL_CONTEXT_HEADER_SIZE) {
    return GLECMA_ERR_INVALID;
  }
  if (EVAL_WITHIN(context->size, flags)) {
    context->flags = 0;
  }
  if (EVAL_WITHIN(context->size, private_name_count)) {
    context->private_name_count = 0;
  }
  if (EVAL_WITHIN(context->size, reserved)) {
    context->reserved = 0;
  }
  if (EVAL_WITHIN(context->size, private_names)) {
    context->private_names = NULL;
  }
  return GLECMA_OK;
}

GLECMA_Result glecma_parse_options_default(GLECMA_ParseOptions * options) {
  if (options == NULL || options->size < GLECMA_PARSE_OPTIONS_HEADER_SIZE) {
    return GLECMA_ERR_INVALID;
  }
  options->reserved = 0;
  if (OPTIONS_WITHIN(options->size, limits)) {
    options->limits = NULL;
  }
  if (OPTIONS_WITHIN(options->size, features)) {
    options->features = NULL;
  }
  if (OPTIONS_WITHIN(options->size, goal)) {
    options->goal = GLECMA_PARSE_SCRIPT;
  }
  if (OPTIONS_WITHIN(options->size, context)) {
    options->context = 0;
  }
  if (OPTIONS_WITHIN(options->size, eval_context)) {
    options->eval_context = NULL;
  }
  return GLECMA_OK;
}

bool glecma_parse_options_resolve(const GLECMA_ParseOptions * options,
    GLECMA_Limits * out_limits, const GLECMA_FeatureSet ** out_features) {
  const GLECMA_Limits * limits = NULL;
  const GLECMA_FeatureSet * features = NULL;
  if (options != NULL) {
    if (options->size < GLECMA_PARSE_OPTIONS_HEADER_SIZE) {
      return false;
    }
    if (OPTIONS_WITHIN(options->size, limits)) {
      limits = options->limits;
    }
    if (OPTIONS_WITHIN(options->size, features)) {
      features = options->features;
    }
  }
  if (!glecma_limits_resolve(limits, out_limits)) {
    return false;
  }
  *out_features = features;
  return true;
}

void glecma_parse_options_raw(const GLECMA_ParseOptions * options,
    uint32_t * out_goal, uint32_t * out_context,
    const GLECMA_EvalContext ** out_eval) {
  *out_goal = GLECMA_PARSE_SCRIPT;
  *out_context = 0;
  *out_eval = NULL;
  if (options == NULL || options->size < GLECMA_PARSE_OPTIONS_HEADER_SIZE) {
    return;
  }
  if (OPTIONS_WITHIN(options->size, goal)) {
    *out_goal = options->goal;
  }
  if (OPTIONS_WITHIN(options->size, context)) {
    *out_context = options->context;
  }
  if (OPTIONS_WITHIN(options->size, eval_context)) {
    *out_eval = options->eval_context;
  }
}

GLECMA_Result glecma_parse_options_goal(const GLECMA_ParseOptions * options,
    uint32_t * out_goal, uint32_t * out_context) {
  uint32_t goal = GLECMA_PARSE_SCRIPT;
  uint32_t context = 0;
  if (options != NULL) {
    if (options->size < GLECMA_PARSE_OPTIONS_HEADER_SIZE) {
      return GLECMA_ERR_INVALID;
    }
    if (OPTIONS_WITHIN(options->size, goal)) {
      goal = options->goal;
    }
    if (OPTIONS_WITHIN(options->size, context)) {
      context = options->context;
    }
  }
  // The first four goals are ::glecma_parse's; the others have entries of
  // their own, which take more than a source.
  if (goal > GLECMA_PARSE_INDIRECT_EVAL) {
    return GLECMA_ERR_INVALID;
  }
  if ((context & ~GLECMA_CONTEXT_MASK) != 0) {
    return GLECMA_ERR_INVALID;
  }
  // Indirect eval code has no caller: nothing it could inherit strictness from.
  if (goal == GLECMA_PARSE_INDIRECT_EVAL && context != 0) {
    return GLECMA_ERR_INVALID;
  }
  *out_goal = goal;
  *out_context = context;
  return GLECMA_OK;
}

bool glecma_context_flags_valid(uint32_t flags, uint32_t mask) {
  if ((flags & ~mask) != 0) {
    return false;
  }
  // Each flag with the one it implies: a derived constructor is a method, a
  // method, a field initializer and a static block are inside a function.
  const uint32_t super_property = GLECMA_CONTEXT_SUPER_CALL |
      GLECMA_CONTEXT_FIELD_INITIALIZER | GLECMA_CONTEXT_STATIC_BLOCK;
  if ((flags & GLECMA_CONTEXT_SUPER_CALL) != 0 &&
      (flags & GLECMA_CONTEXT_SUPER_PROPERTY) == 0) {
    return false;
  }
  if ((flags & super_property) != 0 &&
      (flags & GLECMA_CONTEXT_SUPER_PROPERTY) == 0) {
    return false;
  }
  if ((flags & GLECMA_CONTEXT_SUPER_PROPERTY) != 0 &&
      (flags & GLECMA_CONTEXT_NEW_TARGET) == 0) {
    return false;
  }
  // Module code is strict.
  if ((flags & GLECMA_CONTEXT_MODULE) != 0 && (flags & GLECMA_CONTEXT_STRICT) == 0) {
    return false;
  }
  return true;
}

bool glecma_private_names_valid(const GLECMA_PrivateName * names,
    uint32_t count) {
  if (count != 0 && names == NULL) {
    return false;
  }
  for (uint32_t i = 0; i < count; i++) {
    if (names[i].units == NULL || names[i].length == 0 || names[i].reserved != 0) {
      return false;
    }
  }
  return true;
}

GLECMA_Result glecma_parse_options_eval(const GLECMA_ParseOptions * options,
    uint32_t goal, GLECMA_EvalContext * out) {
  memset(out, 0, sizeof(*out));
  out->size = (uint32_t)sizeof(*out);
  const GLECMA_EvalContext * given = NULL;
  if (options != NULL && OPTIONS_WITHIN(options->size, eval_context)) {
    given = options->eval_context;
  }
  if (given == NULL) {
    return GLECMA_OK;
  }
  // Only direct eval code has a context to inherit.
  if (goal != GLECMA_PARSE_DIRECT_EVAL ||
      given->size < GLECMA_EVAL_CONTEXT_HEADER_SIZE) {
    return GLECMA_ERR_INVALID;
  }
  if (EVAL_WITHIN(given->size, reserved) && given->reserved != 0) {
    return GLECMA_ERR_INVALID;
  }
  if (EVAL_WITHIN(given->size, flags)) {
    out->flags = given->flags;
  }
  if (!glecma_context_flags_valid(out->flags, GLECMA_CONTEXT_EVAL_MASK)) {
    return GLECMA_ERR_INVALID;
  }
  if (EVAL_WITHIN(given->size, private_name_count) &&
      EVAL_WITHIN(given->size, private_names)) {
    out->private_name_count = given->private_name_count;
    out->private_names = given->private_names;
  }
  if (!glecma_private_names_valid(out->private_names, out->private_name_count)) {
    return GLECMA_ERR_INVALID;
  }
  return GLECMA_OK;
}

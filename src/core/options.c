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

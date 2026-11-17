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
 * @file parse_options.c
 *
 * The parse options, the feature set and the Unicode version.
 *
 * Every entry that reads source takes the parse options: size-first, so they
 * can grow without breaking a caller built earlier, and holding the limits
 * and a feature set. The pinned edition is what an empty feature set
 * accepts. A set that names a feature this build does not define is refused
 * before anything is read, with the feature named in the failure record,
 * rather than ignored. The engine is pinned to one Unicode version, which
 * the library reports.
 */

#include <ghoti.io/lang-ecmascript/lang-ecmascript.h>

#include <stdio.h>
#include <string.h>

int main(void) {
  printf("ECMAScript %d, Unicode %s\n", GLECMA_ECMA262_YEAR,
      glecma_unicode_version());
  if (strcmp(glecma_unicode_version(), GLECMA_UNICODE_VERSION) != 0) {
    fprintf(stderr, "the library runs with another Unicode version than it was built for\n");
    return 1;
  }

  // Options start with their defaults and name caps of their own.
  GLECMA_Limits limits = GLECMA_LIMITS_INIT;
  limits.tokens = 1000;
  limits.literal_length = 4096;
  GLECMA_FeatureSet features = GLECMA_FEATURE_SET_INIT;
  GLECMA_ParseOptions options = GLECMA_PARSE_OPTIONS_INIT;
  options.limits = &limits;
  options.features = &features;  // empty: the pinned edition
  // The options of a caller built earlier hold only what it knew: the rest
  // is filled with defaults by this call, which reads the declared size.
  if (glecma_parse_options_default(&options) != GLECMA_OK) {
    return 1;
  }
  options.limits = &limits;
  options.features = &features;
  printf("tokens up to %llu, literals up to %llu code points\n",
      (unsigned long long)limits.tokens,
      (unsigned long long)limits.literal_length);

  // A feature set names features by their lowercase names.
  if (glecma_feature_set_add(&features, "a-future-feature") != GLECMA_OK ||
      !glecma_feature_set_has(&features, "a-future-feature")) {
    fprintf(stderr, "could not name a feature\n");
    return 1;
  }
  printf("the set holds %u feature\n", (unsigned)features.count);

  // The rule codes name what a syntax error broke, and the clause it is from.
  printf("rule %d is \"%s\", clause %s\n", (int)GLECMA_RULE_UNTERMINATED_STRING,
      glecma_rule_name(GLECMA_RULE_UNTERMINATED_STRING),
      glecma_rule_clause(GLECMA_RULE_UNTERMINATED_STRING));
  return 0;
}

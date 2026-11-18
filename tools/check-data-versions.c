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
/*
 * The data-version gate: the Unicode version `unicode` reports, the one
 * `regex` reports and the one this library is pinned to must be one string.
 * Identifier syntax, case mapping and regular expression property escapes
 * move together, and a pair of libraries built from different Unicode data
 * would accept different programs (clause 12.7, clause 22.2.1.1).
 *
 * Both are asked of the built libraries, not of their pin files: this
 * compares what a build will do. (The suite's UCD pin check compares the pin
 * files of every library that generates tables; this is the other half.)
 */
#include <ghoti.io/lang-ecmascript/lang-ecmascript.h>
#include <ghoti.io/regex/regex.h>
#include <ghoti.io/unicode/char.h>

#include <stdio.h>
#include <string.h>

int main(void) {
  const char * pin = GLECMA_UNICODE_VERSION;
  const char * engine = glecma_unicode_version();
  const char * unicode = guni_ucd_version();
  const char * regex = grx_unicode_version();
  printf("check-data-versions: pin %s, engine %s, unicode %s, regex %s (built libraries)\n",
      pin, engine, unicode, regex);
  if (strcmp(pin, engine) != 0 || strcmp(engine, unicode) != 0 ||
      strcmp(unicode, regex) != 0) {
    fprintf(stderr, "check-data-versions: the libraries report different Unicode versions\n");
    return 1;
  }
  printf("check-data-versions: one Unicode version, %s\n", engine);
  return 0;
}

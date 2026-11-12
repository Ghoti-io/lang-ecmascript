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
 * Result strings, the version this build reports, and the result conversion
 * from runtime-core.
 */

#include <ghoti.io/lang-ecmascript/macros.h>

#include <ghoti.io/lang-ecmascript/core.h>

#include "core/core_internal.h"

const char * glecma_result_string(GLECMA_Result result) {
  switch (result) {
    case GLECMA_OK:
      return "No error";
    case GLECMA_ERR_IO:
      return "I/O error";
    case GLECMA_ERR_FORMAT:
      return "Not a format handled here";
    case GLECMA_ERR_UNSUPPORTED:
      return "Unsupported feature";
    case GLECMA_ERR_LIMIT:
      return "Limit exceeded";
    case GLECMA_ERR_CORRUPT:
      return "Corrupt data";
    case GLECMA_ERR_OOM:
      return "Out of memory";
    case GLECMA_ERR_INVALID:
      return "Invalid argument";
    case GLECMA_ERR_INTERNAL:
      return "Internal error";
    case GLECMA_RESULT_COUNT:
    default:
      return "Unknown error";
  }
}

const char * glecma_version_string(void) {
  return GLECMA_VERSION_STRING;
}

unsigned glecma_version_number(void) {
  return GLECMA_VERSION_NUMBER;
}

unsigned glecma_ecma262_edition(void) {
  return GLECMA_ECMA262_EDITION;
}

GLECMA_Result glecma_result_from_grcore(GRCORE_Result result) {
  switch (result) {
    case GRCORE_OK:
      return GLECMA_OK;
    case GRCORE_ERR_IO:
      return GLECMA_ERR_IO;
    case GRCORE_ERR_FORMAT:
      return GLECMA_ERR_FORMAT;
    case GRCORE_ERR_UNSUPPORTED:
      return GLECMA_ERR_UNSUPPORTED;
    case GRCORE_ERR_LIMIT:
      return GLECMA_ERR_LIMIT;
    case GRCORE_ERR_CORRUPT:
      return GLECMA_ERR_CORRUPT;
    case GRCORE_ERR_OOM:
      return GLECMA_ERR_OOM;
    case GRCORE_ERR_INVALID:
      return GLECMA_ERR_INVALID;
    case GRCORE_ERR_INTERNAL:
    default:
      return GLECMA_ERR_INTERNAL;
  }
}

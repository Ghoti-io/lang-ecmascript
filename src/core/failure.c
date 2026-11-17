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
 * The failure record's writer and the table of syntax rules: each rule's
 * stable name, ECMA-262 clause and message.
 */

#include <ghoti.io/lang-ecmascript/macros.h>

#include <ghoti.io/lang-ecmascript/core.h>

#include "core/core_internal.h"

#include <stddef.h>
#include <string.h>

// Writes only the members that lie wholly within the caller's `size`, so a
// caller built against a shorter struct is never written past.
#define FAILURE_WITHIN(failure, member)                                        \
  ((size_t)(failure)->size >= offsetof(GLECMA_Failure, member) +               \
          sizeof((failure)->member))

void glecma_failure_set(GLECMA_Failure * failure, GLECMA_Result result,
    const char * message, const GLECMA_Location * location, const char * limit,
    uint32_t rule, const char * feature) {
  if (failure == NULL) {
    return;
  }
  failure->result = (uint32_t)result;
  if (FAILURE_WITHIN(failure, message)) {
    failure->message = message;
  }
  if (FAILURE_WITHIN(failure, location)) {
    if (location != NULL) {
      failure->location = *location;
    } else {
      failure->location.offset = 0;
      failure->location.line = 0;
      failure->location.column = 0;
    }
  }
  if (FAILURE_WITHIN(failure, limit)) {
    failure->limit = limit;
  }
  if (FAILURE_WITHIN(failure, rule)) {
    failure->rule = rule;
  }
  if (FAILURE_WITHIN(failure, reserved)) {
    failure->reserved = 0;
  }
  if (FAILURE_WITHIN(failure, feature)) {
    memset(failure->feature, 0, sizeof(failure->feature));
    if (feature != NULL) {
      // Names are checked to be shorter than the field when they are added to
      // a set; the bound here is for a caller that passes anything else.
      strncpy(failure->feature, feature, sizeof(failure->feature) - 1);
    }
  }
}

// One row per rule. The index is the rule's number, so a row out of place is
// a wrong name for a code: the test that pins every code's value reads the
// enum, and the one that reads this table checks each row against its code.
typedef struct {
  uint32_t rule;
  const char * name;
  const char * clause;
  const char * message;
} RuleRow;

static const RuleRow rules[] = {
  {GLECMA_RULE_NONE, "none", "", "No syntax rule"},
  {GLECMA_RULE_UNEXPECTED_CHARACTER, "unexpected-character", "12.6",
      "Unexpected character"},
  {GLECMA_RULE_UNTERMINATED_COMMENT, "unterminated-comment", "12.4",
      "Unterminated comment"},
  {GLECMA_RULE_UNTERMINATED_STRING, "unterminated-string", "12.9.4",
      "Unterminated string literal"},
  {GLECMA_RULE_UNTERMINATED_TEMPLATE, "unterminated-template", "12.9.6",
      "Unterminated template literal"},
  {GLECMA_RULE_UNTERMINATED_REGEXP, "unterminated-regexp", "12.9.5",
      "Unterminated regular expression literal"},
  {GLECMA_RULE_REGEXP_FLAGS, "regexp-flags", "12.9.5",
      "Invalid regular expression flags"},
  {GLECMA_RULE_IDENTIFIER_ESCAPE, "identifier-escape", "12.7",
      "Invalid Unicode escape sequence in an identifier"},
  {GLECMA_RULE_PRIVATE_NAME, "private-name", "12.7",
      "A private name needs an identifier after #"},
  {GLECMA_RULE_ESCAPE_HEX, "escape-hex", "12.9.4",
      "Invalid hexadecimal escape sequence"},
  {GLECMA_RULE_ESCAPE_UNICODE, "escape-unicode", "12.9.4",
      "Invalid Unicode escape sequence"},
  {GLECMA_RULE_ESCAPE_CODE_POINT, "escape-code-point", "12.9.4",
      "Invalid Unicode code point escape"},
  {GLECMA_RULE_ESCAPE_LEGACY_OCTAL_STRICT, "escape-legacy-octal-strict",
      "12.9.4", "Octal escape sequences are not allowed in strict mode"},
  {GLECMA_RULE_ESCAPE_NON_OCTAL_DECIMAL_STRICT,
      "escape-non-octal-decimal-strict", "12.9.4",
      "\\8 and \\9 are not allowed in strict mode"},
  {GLECMA_RULE_TEMPLATE_ESCAPE_OCTAL, "template-escape-octal", "12.9.6",
      "Octal escape sequences are not allowed in template literals"},
  {GLECMA_RULE_NUMERIC_EXPECTED_DIGIT, "numeric-expected-digit", "12.9.3",
      "A numeric literal needs a digit here"},
  {GLECMA_RULE_NUMERIC_SEPARATOR, "numeric-separator", "12.9.3",
      "A numeric separator must be between two digits"},
  {GLECMA_RULE_NUMERIC_SEPARATOR_LEGACY, "numeric-separator-legacy", "12.9.3",
      "Numeric separators are not allowed in a legacy octal or "
      "leading-zero literal"},
  {GLECMA_RULE_NUMERIC_BIGINT, "numeric-bigint", "12.9.3",
      "A BigInt literal must be an integer without a leading zero"},
  {GLECMA_RULE_NUMERIC_TRAILING, "numeric-trailing", "12.9.3",
      "An identifier or digit cannot follow a numeric literal"},
  {GLECMA_RULE_NUMERIC_LEGACY_OCTAL_STRICT, "numeric-legacy-octal-strict",
      "12.9.3", "Octal literals are not allowed in strict mode"},
  {GLECMA_RULE_NUMERIC_NON_OCTAL_DECIMAL_STRICT,
      "numeric-non-octal-decimal-strict", "12.9.3",
      "Decimal literals with a leading zero are not allowed in strict mode"},
  {GLECMA_RULE_ESCAPED_RESERVED_WORD, "escaped-reserved-word", "12.7.1",
      "A reserved word cannot contain escape sequences"},
  {GLECMA_RULE_RESERVED_WORD, "reserved-word", "13.1.1",
      "Unexpected reserved word"},
  {GLECMA_RULE_RESERVED_WORD_STRICT, "reserved-word-strict", "13.1.1",
      "Unexpected strict mode reserved word"},
};

#define RULE_ROWS (sizeof(rules) / sizeof(rules[0]))

// The table must hold one row per rule, in order, or an index is a wrong
// name. Checked at compile time so a rule added to the enum without a row
// does not build.
_Static_assert(RULE_ROWS == (size_t)GLECMA_RULE_COUNT,
    "every GLECMA_Rule needs a row in the rule table");

static const RuleRow * rule_row(uint32_t rule) {
  if (rule >= RULE_ROWS || rules[rule].rule != rule) {
    return NULL;
  }
  return &rules[rule];
}

const char * glecma_rule_name(uint32_t rule) {
  const RuleRow * row = rule_row(rule);
  return row != NULL ? row->name : "unknown";
}

const char * glecma_rule_clause(uint32_t rule) {
  const RuleRow * row = rule_row(rule);
  return row != NULL ? row->clause : "";
}

const char * glecma_rule_message(uint32_t rule) {
  const RuleRow * row = rule_row(rule);
  return row != NULL ? row->message : "Syntax error";
}

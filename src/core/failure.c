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
  {GLECMA_RULE_UNEXPECTED_TOKEN, "unexpected-token", "5.1.4",
      "Unexpected token"},
  {GLECMA_RULE_UNEXPECTED_END, "unexpected-end", "5.1.4",
      "Unexpected end of input"},
  {GLECMA_RULE_MISSING_SEMICOLON, "missing-semicolon", "12.10.1",
      "A semicolon is required here"},
  {GLECMA_RULE_INVALID_ASSIGNMENT_TARGET, "invalid-assignment-target", "13.15.1",
      "Invalid left-hand side in assignment"},
  {GLECMA_RULE_INVALID_UPDATE_TARGET, "invalid-update-target", "13.4.1",
      "Invalid operand of an increment or decrement"},
  {GLECMA_RULE_INVALID_FOR_TARGET, "invalid-for-target", "14.7.5.1",
      "Invalid left-hand side in a for-in or for-of loop"},
  {GLECMA_RULE_COVER_INITIALIZED_NAME, "cover-initialized-name", "13.2.5.1",
      "An initializer is only valid in a destructuring pattern"},
  {GLECMA_RULE_REST_ELEMENT_POSITION, "rest-element-position", "13.15.5.1",
      "A rest element must be last and have no trailing comma or initializer"},
  {GLECMA_RULE_EXPONENT_UNARY_OPERAND, "exponent-unary-operand", "13.6",
      "A unary operator cannot be the base of **; use parentheses"},
  {GLECMA_RULE_NULLISH_MIXED, "nullish-mixed", "13.13",
      "?? cannot be mixed with && or || without parentheses"},
  {GLECMA_RULE_OPTIONAL_CHAIN_TEMPLATE, "optional-chain-template", "13.3.9",
      "A template literal cannot follow an optional chain"},
  {GLECMA_RULE_OPTIONAL_CHAIN_NEW, "optional-chain-new", "13.3.9",
      "An optional chain cannot be the callee of new"},
  {GLECMA_RULE_INVALID_SUPER, "invalid-super", "13.3.7",
      "super must be followed by a call or a property access"},
  {GLECMA_RULE_IMPORT_META_OUTSIDE_MODULE, "import-meta-outside-module", "13.3.12",
      "import.meta is only valid in a module"},
  {GLECMA_RULE_IMPORT_CALL_ARGUMENTS, "import-call-arguments", "13.3.10",
      "import() takes a specifier and optionally an options argument"},
  {GLECMA_RULE_ARROW_LINE_TERMINATOR, "arrow-line-terminator", "15.3",
      "A line terminator is not allowed before =>"},
  {GLECMA_RULE_INVALID_ARROW_PARAMETERS, "invalid-arrow-parameters", "15.3.1",
      "Invalid arrow function parameters"},
  {GLECMA_RULE_DECLARATION_POSITION, "declaration-position", "14.1",
      "A declaration is not allowed in this position"},
  {GLECMA_RULE_FOR_HEAD_RESTRICTED, "for-head-restricted", "14.7.5.1",
      "This for loop head is not allowed"},
  {GLECMA_RULE_FOR_AWAIT_POSITION, "for-await-position", "14.7.5.1",
      "for await is only valid in an async function"},
  {GLECMA_RULE_INVALID_CLASS_ELEMENT, "invalid-class-element", "15.7",
      "Invalid class element"},
  {GLECMA_RULE_THROW_LINE_TERMINATOR, "throw-line-terminator", "14.14",
      "A line terminator is not allowed after throw"},
  {GLECMA_RULE_BINDING_EVAL_ARGUMENTS, "binding-eval-arguments", "13.1.1",
      "eval and arguments cannot be bound in strict mode"},
  {GLECMA_RULE_RETURN_POSITION, "return-position", "14.10",
      "return is only valid in a function body"},
  {GLECMA_RULE_SUPER_PROPERTY_POSITION, "super-property-position", "16.1.1",
      "super property access is not valid here"},
  {GLECMA_RULE_SUPER_CALL_POSITION, "super-call-position", "16.1.1",
      "super() is only valid in a derived class constructor"},
  {GLECMA_RULE_NEW_TARGET_POSITION, "new-target-position", "16.1.1",
      "new.target is only valid in a function"},
  {GLECMA_RULE_ARGUMENTS_IN_INITIALIZER, "arguments-in-initializer", "15.7.1",
      "arguments is not allowed in a class initializer"},
  {GLECMA_RULE_YIELD_IN_PARAMETERS, "yield-in-parameters", "15.5.1",
      "A yield expression is not allowed in parameters"},
  {GLECMA_RULE_AWAIT_IN_PARAMETERS, "await-in-parameters", "15.8.1",
      "An await expression is not allowed in parameters"},
  {GLECMA_RULE_DELETE_IDENTIFIER_STRICT, "delete-identifier-strict", "13.5.1.1",
      "delete of an unqualified identifier is not allowed in strict mode"},
  {GLECMA_RULE_DELETE_PRIVATE_REFERENCE, "delete-private-reference", "13.5.1.1",
      "A private name cannot be deleted"},
  {GLECMA_RULE_WITH_STRICT, "with-strict", "14.11.1",
      "with is not allowed in strict mode"},
  {GLECMA_RULE_DUPLICATE_PROTO, "duplicate-proto", "13.2.5.1",
      "Duplicate __proto__ property in an object literal"},
  {GLECMA_RULE_CONST_WITHOUT_INITIALIZER, "const-without-initializer", "14.3.1.1",
      "A const declaration needs an initializer"},
  {GLECMA_RULE_LET_BOUND_NAME, "let-bound-name", "14.3.1.1",
      "let cannot be the name of a lexical binding"},
  {GLECMA_RULE_USE_STRICT_NON_SIMPLE_PARAMETERS, "use-strict-non-simple-parameters", "15.2.1",
      "A function with non-simple parameters cannot have a use strict directive"},
  {GLECMA_RULE_LABEL_REDECLARED, "label-redeclared", "14.13.1",
      "Label has already been declared"},
  {GLECMA_RULE_BREAK_TARGET, "break-target", "14.9.1",
      "break has no target here"},
  {GLECMA_RULE_CONTINUE_TARGET, "continue-target", "14.8.1",
      "continue has no target here"},
  {GLECMA_RULE_LABELLED_FUNCTION_BODY, "labelled-function-body", "14.13.1",
      "A labelled function cannot be the body of this statement"},
  {GLECMA_RULE_CLASS_CONSTRUCTOR_DUPLICATE, "class-constructor-duplicate", "15.7.1",
      "A class may have only one constructor"},
  {GLECMA_RULE_CLASS_CONSTRUCTOR_KIND, "class-constructor-kind", "15.7.1",
      "A class constructor cannot be an accessor, generator, async method or field"},
  {GLECMA_RULE_CLASS_STATIC_PROTOTYPE, "class-static-prototype", "15.7.1",
      "A static member cannot be named prototype"},
  {GLECMA_RULE_PRIVATE_NAME_CONSTRUCTOR, "private-name-constructor", "15.7.1",
      "A private name cannot be #constructor"},
  {GLECMA_RULE_PRIVATE_NAME_DUPLICATE, "private-name-duplicate", "15.7.1",
      "Private name has already been declared"},
  {GLECMA_RULE_PRIVATE_NAME_UNDECLARED, "private-name-undeclared", "15.7.1",
      "Private name is not declared in an enclosing class"},
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

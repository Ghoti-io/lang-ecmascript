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
 * @file core.h
 * @stability free
 *
 * Result codes, the failure record and source location every parse call
 * reports through, the version this build reports, and the edition of the
 * language it implements.
 */

#ifndef GHOTI_IO_GLECMA_CORE_H
#define GHOTI_IO_GLECMA_CORE_H

#include <ghoti.io/lang-ecmascript/macros.h>

#include <ghoti.io/lang-ecmascript/allocator.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The edition of ECMA-262 this library implements: the 17th,
 *   ECMAScript 2026, approved by the Ecma General Assembly on 30 June 2026.
 *
 * Annex B (additional features for web browsers) is in scope. ECMA-402
 * (internationalization) is a separate standard and is not implemented.
 * Moving to a later edition is a deliberate change of this value, of the
 * pinned conformance suite and of the documentation together, never a
 * feature picked up from a draft. `documentation/design.md` says what
 * conformance to this edition means and how it is measured.
 */
#define GLECMA_ECMA262_EDITION 17

/** @brief The year name of ::GLECMA_ECMA262_EDITION: ECMAScript 2026. */
#define GLECMA_ECMA262_YEAR 2026

/**
 * @brief The Unicode Character Database version the engine is pinned to.
 *
 * One version for the whole engine: identifiers (ID_Start, ID_Continue) and
 * white space here, and case mapping, normalization, collation and RegExp
 * property escapes in later milestones, all read tables of this version. It
 * moves only in an edition bump, together with `unicode`, `regex` and the
 * collation data, never on its own. ::glecma_unicode_version reports the
 * version actually in use, and a test requires that it equal this macro.
 */
#define GLECMA_UNICODE_VERSION "17.0.0"

/**
 * @brief Bytes in a feature name, the terminating NUL included.
 *
 * Names are short ASCII identifiers such as "decorators"; a longer one is
 * refused when it is added to a feature set. The same size is the width of
 * ::GLECMA_Failure::feature.
 */
#define GLECMA_FEATURE_NAME_MAX 48

/**
 * @brief Result of an operation.
 *
 * Zero is success. ::GLECMA_RESULT_COUNT closes the enum so a test can check
 * the string table is complete. The first nine are the suite's shared
 * vocabulary; a value is appended before the count only when an operation
 * needs to report something none of them means, so the nine keep their
 * numbers.
 *
 * ::GLECMA_ERR_SYNTAX is the first such value: source text that decodes
 * cleanly but is not ECMAScript for the goal it was parsed in. It is kept
 * apart from ::GLECMA_ERR_CORRUPT (the text is not well-formed UTF-8, so it
 * is not source text at all) and from ::GLECMA_ERR_INVALID (the caller's
 * arguments are wrong), so that a host can tell a script's author's mistake
 * from its own.
 */
typedef enum {
  GLECMA_OK = 0,          ///< The operation succeeded.
  GLECMA_ERR_IO,          ///< A read, write, or seek failed.
  GLECMA_ERR_FORMAT,      ///< Well-formed input, but not a format handled
                          ///< here.
  GLECMA_ERR_UNSUPPORTED, ///< A feature this library does not implement.
  GLECMA_ERR_LIMIT,       ///< A stated cap or budget was exceeded.
  GLECMA_ERR_CORRUPT,     ///< The input is not a valid encoding.
  GLECMA_ERR_OOM,         ///< The allocator returned NULL.
  GLECMA_ERR_INVALID,     ///< A caller-supplied argument is wrong.
  GLECMA_ERR_INTERNAL,    ///< The library's own invariant failed.
  GLECMA_ERR_SYNTAX,      ///< Well-formed source text that is not valid
                          ///< ECMAScript for the requested goal: a grammar
                          ///< error or an early error.
  GLECMA_RESULT_COUNT     ///< Not a result; closes the enum.
} GLECMA_Result;

/**
 * @brief A position in source text.
 *
 * `offset` counts the input's own units: bytes for UTF-8 source, 16-bit code
 * units for UTF-16 source. `line` is 1-based and advances at each of
 * ECMA-262's line terminators (LF, CR, CRLF as one, U+2028, U+2029); the LF of
 * a CRLF is on the CR's line. `column` is 0-based and counts UTF-16 code units
 * from the start of the line, whatever the input encoding, because that is
 * how source maps and browser developer tools count: a code point above
 * U+FFFF is two columns wide, and a UTF-8 host still gets columns a debugger
 * front end can use.
 *
 * This struct is fixed forever: it has these three members and will never
 * gain another. It is a plain value that hosts copy, store and compare, so
 * it carries no size member; a position that needed more would be a new
 * type.
 */
typedef struct GLECMA_Location {
  size_t offset; ///< Index into the input, in input units.
  size_t line;   ///< 1-based line number.
  size_t column; ///< 0-based column, in UTF-16 code units.
} GLECMA_Location;

/**
 * @brief The rule a syntax error broke.
 *
 * Each code names one rule of the lexical or syntactic grammar or one early
 * error, so that a test and a host can assert the reason for a refusal and
 * not only the refusal. The codes are an append-only enumeration: a value is
 * never renumbered, reused or removed, and a new rule is appended before
 * ::GLECMA_RULE_COUNT with the next number. A host may store and compare the
 * numbers. ::glecma_rule_name and ::glecma_rule_clause give each its stable
 * name and the ECMA-262 clause it comes from (the clause numbers are the
 * 17th edition's; `documentation/clause-concordance.tsv` maps the 16th's,
 * and `make check-clauses` fails a number the 17th lacks).
 *
 * Zero is no rule: the value of ::GLECMA_Failure::rule for every result but
 * ::GLECMA_ERR_SYNTAX. The lexical rules come first; the rules of the parser
 * and of the early errors are appended after them.
 */
typedef enum {
  GLECMA_RULE_NONE = 0, ///< No rule: the failure is not a syntax error.

  GLECMA_RULE_UNEXPECTED_CHARACTER = 1, ///< A code point that begins no token
                                        ///< (clause 12.6).
  GLECMA_RULE_UNTERMINATED_COMMENT = 2, ///< A multi-line comment that never
                                        ///< ends (clause 12.4).
  GLECMA_RULE_UNTERMINATED_STRING = 3,  ///< A string literal ended by a line
                                        ///< terminator or the end of input
                                        ///< (clause 12.9.4).
  GLECMA_RULE_UNTERMINATED_TEMPLATE = 4, ///< A template literal that never
                                         ///< ends (clause 12.9.6).
  GLECMA_RULE_UNTERMINATED_REGEXP = 5,  ///< A regular expression literal
                                        ///< ended by a line terminator or the
                                        ///< end of input (clause 12.9.5).
  GLECMA_RULE_REGEXP_FLAGS = 6,         ///< A backslash where the flags of a
                                        ///< regular expression literal end
                                        ///< (clause 12.9.5).
  GLECMA_RULE_IDENTIFIER_ESCAPE = 7,    ///< A malformed `\u` escape in a name,
                                        ///< or one that is not an
                                        ///< IdentifierPart (clause 12.7).
  GLECMA_RULE_PRIVATE_NAME = 8,         ///< `#` not followed by an
                                        ///< IdentifierName (clause 12.7).
  GLECMA_RULE_ESCAPE_HEX = 9,           ///< A malformed `\x` escape (clause 12.9.4).
  GLECMA_RULE_ESCAPE_UNICODE = 10,      ///< A malformed `\u` escape in a
                                        ///< string or template (clause 12.9.4).
  GLECMA_RULE_ESCAPE_CODE_POINT = 11,   ///< A malformed or out-of-range
                                        ///< `\u{...}` escape (clause 12.9.4).
  GLECMA_RULE_ESCAPE_LEGACY_OCTAL_STRICT = 12, ///< A legacy octal escape in
                                               ///< strict code (clauses 12.9.4,
                                               ///< 12.9.4.1).
  GLECMA_RULE_ESCAPE_NON_OCTAL_DECIMAL_STRICT = 13, ///< `\8` or `\9` in
                                                    ///< strict code (clause 12.9.4).
  GLECMA_RULE_TEMPLATE_ESCAPE_OCTAL = 14, ///< A decimal digit escape in a
                                          ///< template: not an escape
                                          ///< sequence there (clause 12.9.6).
  GLECMA_RULE_NUMERIC_EXPECTED_DIGIT = 15, ///< No digit where a numeric
                                           ///< literal needs one (clause 12.9.3).
  GLECMA_RULE_NUMERIC_SEPARATOR = 16,   ///< A numeric separator that is not
                                        ///< between two digits (clause 12.9.3).
  GLECMA_RULE_NUMERIC_SEPARATOR_LEGACY = 17, ///< A separator in a legacy
                                             ///< octal or leading-zero
                                             ///< literal (clause 12.9.3).
  GLECMA_RULE_NUMERIC_BIGINT = 18,      ///< The BigInt suffix on a literal
                                        ///< that cannot be one (clause 12.9.3).
  GLECMA_RULE_NUMERIC_TRAILING = 19,    ///< An IdentifierStart or digit
                                        ///< directly after a numeric
                                        ///< literal (clause 12.9.3).
  GLECMA_RULE_NUMERIC_LEGACY_OCTAL_STRICT = 20, ///< A legacy octal literal in
                                                ///< strict code (clause 12.9.3).
  GLECMA_RULE_NUMERIC_NON_OCTAL_DECIMAL_STRICT = 21, ///< A leading-zero
                                                     ///< decimal literal in
                                                     ///< strict code
                                                     ///< (clause 12.9.3).
  GLECMA_RULE_ESCAPED_RESERVED_WORD = 22, ///< A reserved word written with a
                                          ///< `\u` escape, where an
                                          ///< Identifier is required (clause 12.7.1).
  GLECMA_RULE_RESERVED_WORD = 23,       ///< A reserved word where an
                                        ///< Identifier is required (clause 13.1.1).
  GLECMA_RULE_RESERVED_WORD_STRICT = 24, ///< A word reserved only in strict
                                         ///< code, or in a module, where an
                                         ///< Identifier is required (clause 13.1.1).

  GLECMA_RULE_UNEXPECTED_TOKEN = 25, ///< A token the grammar does not allow here (clause 5.1.4).
  GLECMA_RULE_UNEXPECTED_END = 26, ///< The source ended where the grammar needs more (clause 5.1.4).
  GLECMA_RULE_MISSING_SEMICOLON = 27, ///< No semicolon where one is required and automatic semicolon insertion does not supply it (clause 12.10.1).
  GLECMA_RULE_INVALID_ASSIGNMENT_TARGET = 28, ///< The left side of an assignment is not a valid target (clause 13.15.1).
  GLECMA_RULE_INVALID_UPDATE_TARGET = 29, ///< The operand of `++` or `--` is not a valid target (clause 13.4.1).
  GLECMA_RULE_INVALID_FOR_TARGET = 30, ///< The left side of a `for-in` or `for-of` is not a valid target (clause 14.7.5.1).
  GLECMA_RULE_COVER_INITIALIZED_NAME = 31, ///< An object literal's `{a = 1}` where it is not a pattern (clause 13.2.5.1).
  GLECMA_RULE_REST_ELEMENT_POSITION = 32, ///< A rest element that is not last, or has a trailing comma or an initializer (clause 13.15.5.1).
  GLECMA_RULE_EXPONENT_UNARY_OPERAND = 33, ///< A unary operator directly before `**` (clause 13.6).
  GLECMA_RULE_NULLISH_MIXED = 34, ///< `??` mixed with `&&` or `||` without parentheses (clause 13.13).
  GLECMA_RULE_OPTIONAL_CHAIN_TEMPLATE = 35, ///< A template literal in an optional chain (clause 13.3.9).
  GLECMA_RULE_OPTIONAL_CHAIN_NEW = 36, ///< `?.` in the callee of `new` (clause 13.3.9).
  GLECMA_RULE_INVALID_SUPER = 37, ///< `super` not followed by a call or a property access (clause 13.3.7).
  GLECMA_RULE_IMPORT_META_OUTSIDE_MODULE = 38, ///< `import.meta` outside a Module (clause 13.3.12).
  GLECMA_RULE_IMPORT_CALL_ARGUMENTS = 39, ///< An `import()` call with other than one or two arguments (clause 13.3.10).
  GLECMA_RULE_ARROW_LINE_TERMINATOR = 40, ///< A line terminator before `=>` (clause 15.3).
  GLECMA_RULE_INVALID_ARROW_PARAMETERS = 41, ///< Arrow parameters that are not a parameter list (clause 15.3.1).
  GLECMA_RULE_DECLARATION_POSITION = 42, ///< A declaration where only a statement is allowed (clause 14.1).
  GLECMA_RULE_FOR_HEAD_RESTRICTED = 43, ///< A `for` head the grammar's lookahead restrictions refuse (clause 14.7.5.1).
  GLECMA_RULE_FOR_AWAIT_POSITION = 44, ///< `for await` outside an async function (clause 14.7.5.1).
  GLECMA_RULE_INVALID_CLASS_ELEMENT = 45, ///< A class element the grammar does not allow (clause 15.7).
  GLECMA_RULE_THROW_LINE_TERMINATOR = 46, ///< A line terminator after `throw` (clause 14.14).
  GLECMA_RULE_BINDING_EVAL_ARGUMENTS = 47, ///< `eval` or `arguments` bound in strict code (clause 13.1.1).
  GLECMA_RULE_RETURN_POSITION = 48, ///< A `return` outside a function body (clause 14.10).
  GLECMA_RULE_SUPER_PROPERTY_POSITION = 49, ///< `super.x` or `super[x]` where no method, field initializer or static block encloses it (clause 16.1.1).
  GLECMA_RULE_SUPER_CALL_POSITION = 50, ///< `super(...)` outside the constructor of a derived class (clause 16.1.1).
  GLECMA_RULE_NEW_TARGET_POSITION = 51, ///< `new.target` outside a function, a field initializer and a static block (clause 16.1.1).
  GLECMA_RULE_ARGUMENTS_IN_INITIALIZER = 52, ///< `arguments` in a class field initializer or static block (clause 15.7.1).
  GLECMA_RULE_YIELD_IN_PARAMETERS = 53, ///< A `yield` expression in a generator's or an arrow function's parameters (clause 15.5.1).
  GLECMA_RULE_AWAIT_IN_PARAMETERS = 54, ///< An `await` expression in an async function's or an arrow function's parameters (clause 15.8.1).
  GLECMA_RULE_DELETE_IDENTIFIER_STRICT = 55, ///< `delete x` in strict code (clause 13.5.1.1).
  GLECMA_RULE_DELETE_PRIVATE_REFERENCE = 56, ///< `delete` of a private member (clause 13.5.1.1).
  GLECMA_RULE_WITH_STRICT = 57, ///< A `with` statement in strict code (clause 14.11.1).
  GLECMA_RULE_DUPLICATE_PROTO = 58, ///< Two `__proto__: value` properties in an object literal that is not a pattern (clause 13.2.5.1).
  GLECMA_RULE_CONST_WITHOUT_INITIALIZER = 59, ///< A `const` binding with no initializer (clause 14.3.1.1).
  GLECMA_RULE_LET_BOUND_NAME = 60, ///< `let` as the name in a `let` or `const` declaration (clause 14.3.1.1).
  GLECMA_RULE_USE_STRICT_NON_SIMPLE_PARAMETERS = 61, ///< A "use strict" directive in a function whose parameters are not a simple list (clause 15.2.1).
  GLECMA_RULE_LABEL_REDECLARED = 62, ///< A label inside a statement that already carries the same label (clause 14.13.1).
  GLECMA_RULE_BREAK_TARGET = 63, ///< A `break` with no enclosing loop or switch, or with a label that is not in scope (clause 14.9.1).
  GLECMA_RULE_CONTINUE_TARGET = 64, ///< A `continue` with no enclosing loop, or with a label that does not label a loop (clause 14.8.1).
  GLECMA_RULE_LABELLED_FUNCTION_BODY = 65, ///< A labelled function declaration as the body of an `if`, loop or `with` (clause 14.13.1).
  GLECMA_RULE_CLASS_CONSTRUCTOR_DUPLICATE = 66, ///< A second `constructor` method in a class (clause 15.7.1).
  GLECMA_RULE_CLASS_CONSTRUCTOR_KIND = 67, ///< A special method or a field named `constructor` (clause 15.7.1).
  GLECMA_RULE_CLASS_STATIC_PROTOTYPE = 68, ///< A static method, accessor or field named `prototype` (clause 15.7.1).
  GLECMA_RULE_PRIVATE_NAME_CONSTRUCTOR = 69, ///< A private member named `#constructor` (clause 15.7.1).
  GLECMA_RULE_PRIVATE_NAME_DUPLICATE = 70, ///< A private name declared twice in one class, other than a getter and setter pair of one kind (clause 15.7.1).
  GLECMA_RULE_PRIVATE_NAME_UNDECLARED = 71, ///< A reference to a private name that no enclosing class declares (clause 15.7.1).
  GLECMA_RULE_BLOCK_REDECLARATION = 72, ///< A name declared twice in a block, or both lexically and with var (clause 14.2.1).
  GLECMA_RULE_SWITCH_REDECLARATION = 73, ///< A name declared twice in a switch's case block, or both lexically and with var (clause 14.12.1).
  GLECMA_RULE_SCRIPT_REDECLARATION = 74, ///< A name declared twice at the top level of a script, or both lexically and with var (clause 16.1.1).
  GLECMA_RULE_FUNCTION_REDECLARATION = 75, ///< A lexical name in a function body that is also a parameter, declared twice, or also var-declared (clause 15.2.1).
  GLECMA_RULE_CATCH_REDECLARATION = 76, ///< A catch parameter declared twice or redeclared by its block, other than by a var (Annex B.3.4) (clause 14.15.1).
  GLECMA_RULE_FOR_REDECLARATION = 77, ///< A lexical name of a for head that is declared twice or also var-declared in the body (clause 14.7.5.1).
  GLECMA_RULE_STATIC_BLOCK_REDECLARATION = 78, ///< A name declared twice in a class static block, or both lexically and with var (clause 15.7.1).
  GLECMA_RULE_DUPLICATE_PARAMETER = 79, ///< A repeated parameter name in strict code, an arrow function, a method or a parameter list that is not simple (clause 15.1.1).
  GLECMA_RULE_REGEXP_PATTERN = 80, ///< A regular expression literal whose pattern is not valid for its flags (clause 22.2.1.1).
  GLECMA_RULE_REGEXP_FLAG_SET = 81, ///< A regular expression literal whose flags are not among dgimsuvy, repeat a flag or hold both u and v (clause 13.2.7.1).
  GLECMA_RULE_MODULE_REDECLARATION = 82, ///< A name declared twice at the top level of a module, or both lexically and with var (clause 16.2.1.1).
  GLECMA_RULE_MODULE_DUPLICATE_EXPORT = 83, ///< A name exported more than once, `export default` twice included (clause 16.2.1.1).
  GLECMA_RULE_MODULE_EXPORT_UNDECLARED = 84, ///< An export of a local name the module neither declares nor imports (clause 16.2.1.1).
  GLECMA_RULE_MODULE_EXPORT_STRING_LOCAL = 85, ///< A string literal as the local name of an export that has no `from` clause (clause 16.2.3.1).
  GLECMA_RULE_MODULE_NAME_ILL_FORMED = 86, ///< A string literal used as a module export name that holds a lone surrogate (clause 16.2.1.1).
  GLECMA_RULE_IMPORT_ATTRIBUTE_DUPLICATE = 87, ///< Two import attributes with the same key (clause 16.2.2.1).

  GLECMA_RULE_COUNT ///< Not a rule; closes the enum. Appended rules go before
                    ///< it.
} GLECMA_Rule;

/**
 * @brief The stable name of a rule code, such as "unterminated-string".
 *
 * @param rule A rule code, including values outside the enum.
 * @return A static string, never NULL; "unknown" for a value outside the
 *   enum, and "none" for ::GLECMA_RULE_NONE.
 */
GLECMA_API const char * glecma_rule_name(uint32_t rule);

/**
 * @brief The ECMA-262 clause a rule comes from, such as "12.9.4".
 *
 * @param rule A rule code, including values outside the enum.
 * @return A static string, never NULL; "" for ::GLECMA_RULE_NONE and for a
 *   value outside the enum.
 */
GLECMA_API const char * glecma_rule_clause(uint32_t rule);

/**
 * @brief The Unicode version the engine's identifier and white space tables
 *   are generated from.
 *
 * This is the version of the `unicode` library the engine is running with,
 * which is the one version every part of the engine uses and which equals
 * ::GLECMA_UNICODE_VERSION in a correctly built library (a test requires it).
 *
 * @return A static string such as "17.0.0", never NULL.
 */
GLECMA_API const char * glecma_unicode_version(void);

/**
 * @brief Why a call failed, written on every return when the caller asks.
 *
 * The first member is the size of the struct the caller was compiled
 * against, and a caller sets it with ::GLECMA_FAILURE_INIT. The library
 * writes only the members that lie wholly within that size, so a caller
 * built against a shorter struct keeps working when a member is added at the
 * end (members are only ever added at the end). A `size` smaller than the
 * header is ::GLECMA_ERR_INVALID and nothing is written.
 *
 * A call that takes a `GLECMA_Failure *` writes all of it on every return,
 * success included, whenever the pointer is not NULL; a field that does not
 * apply to the result is zero, NULL or empty. Every string pointer is
 * static, never allocated and never caller data, so the record needs no
 * freeing and stays valid for the life of the process; the one piece of
 * caller text it carries, a feature name, is copied into `feature`.
 *
 * The members are fixed-width integers (the result is the ::GLECMA_Result
 * value as a `uint32_t`, so the layout does not depend on the compiler's
 * choice of enum width), pointers and the fixed ::GLECMA_Location; there is
 * no padding to be read.
 */
typedef struct GLECMA_Failure {
  uint32_t size;            ///< `sizeof(GLECMA_Failure)` as the caller knows it.
  uint32_t result;          ///< The same code the function returned.
  const char * message;     ///< A static English description, or NULL.
  GLECMA_Location location; ///< Where in the source the failure is, for
                            ///< ::GLECMA_ERR_CORRUPT (the first byte of the
                            ///< first ill-formed sequence) and
                            ///< ::GLECMA_ERR_SYNTAX; all zero otherwise.
  const char * limit;       ///< For ::GLECMA_ERR_LIMIT, the static name of
                            ///< the ::GLECMA_Limits field that was exceeded;
                            ///< NULL otherwise.
  uint32_t rule;            ///< For ::GLECMA_ERR_SYNTAX, the ::GLECMA_Rule
                            ///< that was broken, as a number; zero otherwise.
  uint32_t reserved;        ///< Padding; always zero.
  char feature[GLECMA_FEATURE_NAME_MAX]; ///< For ::GLECMA_ERR_UNSUPPORTED from
                            ///< a feature set, the name of the first feature
                            ///< the build lacks, NUL-terminated; empty
                            ///< otherwise. A copy, because a feature name is
                            ///< the caller's text and the record outlives it.
} GLECMA_Failure;

/** @brief Initializer for a ::GLECMA_Failure: sets `size`, zeroes the rest. */
#define GLECMA_FAILURE_INIT                                                    \
  { (uint32_t)sizeof(GLECMA_Failure), 0u, NULL, {0, 0, 0}, NULL, 0u, 0u, {0} }

/**
 * @brief Static description of a result code.
 *
 * The string is never NULL, never allocated, and never contains caller data.
 *
 * @param result The result code, including values outside the enum.
 * @return A static string; "Unknown error" for a value outside the enum.
 */
GLECMA_API const char * glecma_result_string(GLECMA_Result result);

/**
 * @brief This build's version, as the string the Makefile generated.
 *
 * @return A static string, never NULL. Equal to ::GLECMA_VERSION_STRING of
 *   the headers the library was built with.
 */
GLECMA_API const char * glecma_version_string(void);

/**
 * @brief This build's version, packed as ::GLECMA_MAKE_VERSION packs it.
 *
 * @return `(major << 16) | (minor << 8) | patch`.
 */
GLECMA_API unsigned glecma_version_number(void);

/**
 * @brief The ECMA-262 edition the library was built to implement.
 *
 * A host that loads the shared library at run time compares this with the
 * ::GLECMA_ECMA262_EDITION of the headers it was compiled against.
 *
 * @return ::GLECMA_ECMA262_EDITION as the library saw it.
 */
GLECMA_API unsigned glecma_ecma262_edition(void);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GLECMA_CORE_H

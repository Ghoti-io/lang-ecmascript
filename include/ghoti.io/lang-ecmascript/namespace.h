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
 * @file namespace.h
 * @stability free
 *
 * Maps every public name of this library into its version namespace.
 *
 * One list, included from macros.h, so a type rename is in effect before any
 * struct tag that uses the name. A rename spread across headers fails when an
 * internal header defines a struct tag without including the public header
 * that declares its typedef, and the error is nowhere near its cause.
 *
 * Every function and type a public header declares is listed here, and
 * `make check-symbols` fails on an exported symbol that escaped the list.
 */

#ifndef GHOTI_IO_GLECMA_NAMESPACE_H
#define GHOTI_IO_GLECMA_NAMESPACE_H

#include <ghoti.io/lang-ecmascript/libver.h>

/// @cond HIDDEN_SYMBOLS

// core.h and allocator.h
#define GLECMA_Allocator GHOTIIO_LANG_ECMASCRIPT(GLECMA_Allocator)
#define GLECMA_Failure GHOTIIO_LANG_ECMASCRIPT(GLECMA_Failure)
#define GLECMA_Location GHOTIIO_LANG_ECMASCRIPT(GLECMA_Location)
#define GLECMA_Result GHOTIIO_LANG_ECMASCRIPT(GLECMA_Result)
#define GLECMA_Rule GHOTIIO_LANG_ECMASCRIPT(GLECMA_Rule)
#define glecma_allocator_default GHOTIIO_LANG_ECMASCRIPT(glecma_allocator_default)
#define glecma_ecma262_edition GHOTIIO_LANG_ECMASCRIPT(glecma_ecma262_edition)
#define glecma_result_string GHOTIIO_LANG_ECMASCRIPT(glecma_result_string)
#define glecma_rule_clause GHOTIIO_LANG_ECMASCRIPT(glecma_rule_clause)
#define glecma_rule_name GHOTIIO_LANG_ECMASCRIPT(glecma_rule_name)
#define glecma_unicode_version GHOTIIO_LANG_ECMASCRIPT(glecma_unicode_version)
#define glecma_version_number GHOTIIO_LANG_ECMASCRIPT(glecma_version_number)
#define glecma_version_string GHOTIIO_LANG_ECMASCRIPT(glecma_version_string)

// engine.h
#define glecma_engine_descriptor GHOTIIO_LANG_ECMASCRIPT(glecma_engine_descriptor)
#define glecma_engine_find GHOTIIO_LANG_ECMASCRIPT(glecma_engine_find)
#define glecma_engine_register GHOTIIO_LANG_ECMASCRIPT(glecma_engine_register)

// limits.h
#define GLECMA_Limits GHOTIIO_LANG_ECMASCRIPT(GLECMA_Limits)
#define glecma_limits_default GHOTIIO_LANG_ECMASCRIPT(glecma_limits_default)

// options.h
#define GLECMA_FeatureSet GHOTIIO_LANG_ECMASCRIPT(GLECMA_FeatureSet)
#define GLECMA_ParseOptions GHOTIIO_LANG_ECMASCRIPT(GLECMA_ParseOptions)
#define glecma_feature_set_add GHOTIIO_LANG_ECMASCRIPT(glecma_feature_set_add)
#define glecma_feature_set_has GHOTIIO_LANG_ECMASCRIPT(glecma_feature_set_has)
#define glecma_parse_options_default                                           \
  GHOTIIO_LANG_ECMASCRIPT(glecma_parse_options_default)

// source.h
#define GLECMA_Encoding GHOTIIO_LANG_ECMASCRIPT(GLECMA_Encoding)
#define GLECMA_Source GHOTIIO_LANG_ECMASCRIPT(GLECMA_Source)
#define glecma_source_code_points                                              \
  GHOTIIO_LANG_ECMASCRIPT(glecma_source_code_points)
#define glecma_source_create_utf16                                             \
  GHOTIIO_LANG_ECMASCRIPT(glecma_source_create_utf16)
#define glecma_source_create_utf8                                              \
  GHOTIIO_LANG_ECMASCRIPT(glecma_source_create_utf8)
#define glecma_source_release GHOTIIO_LANG_ECMASCRIPT(glecma_source_release)
#define glecma_source_retain GHOTIIO_LANG_ECMASCRIPT(glecma_source_retain)
#define glecma_source_dump GHOTIIO_LANG_ECMASCRIPT(glecma_source_dump)
#define glecma_source_encoding GHOTIIO_LANG_ECMASCRIPT(glecma_source_encoding)
#define glecma_source_lines GHOTIIO_LANG_ECMASCRIPT(glecma_source_lines)
#define glecma_source_location GHOTIIO_LANG_ECMASCRIPT(glecma_source_location)
#define glecma_source_units GHOTIIO_LANG_ECMASCRIPT(glecma_source_units)

// Internal (src/); hidden by -fvisibility=hidden, renamed to keep one rule.
#define glecma_limits_resolve GHOTIIO_LANG_ECMASCRIPT(glecma_limits_resolve)
#define glecma_result_from_grcore                                              \
  GHOTIIO_LANG_ECMASCRIPT(glecma_result_from_grcore)
#define GLECMA_SourceCheckpoint                                                \
  GHOTIIO_LANG_ECMASCRIPT(GLECMA_SourceCheckpoint)
#define GLECMA_SourceCursor GHOTIIO_LANG_ECMASCRIPT(GLECMA_SourceCursor)
#define glecma_source_cursor_location                                          \
  GHOTIIO_LANG_ECMASCRIPT(glecma_source_cursor_location)
#define glecma_source_cursor_next                                              \
  GHOTIIO_LANG_ECMASCRIPT(glecma_source_cursor_next)
#define glecma_source_cursor_seek                                              \
  GHOTIIO_LANG_ECMASCRIPT(glecma_source_cursor_seek)
#define glecma_source_cursor_start                                             \
  GHOTIIO_LANG_ECMASCRIPT(glecma_source_cursor_start)
#define glecma_source_utf8_decode                                              \
  GHOTIIO_LANG_ECMASCRIPT(glecma_source_utf8_decode)

// Internal: the tokenizer, and the failure and options helpers.
#define GLECMA_Keyword GHOTIIO_LANG_ECMASCRIPT(GLECMA_Keyword)
#define GLECMA_LexError GHOTIIO_LANG_ECMASCRIPT(GLECMA_LexError)
#define GLECMA_LexText GHOTIIO_LANG_ECMASCRIPT(GLECMA_LexText)
#define GLECMA_LexVisitor GHOTIIO_LANG_ECMASCRIPT(GLECMA_LexVisitor)
#define GLECMA_Lexer GHOTIIO_LANG_ECMASCRIPT(GLECMA_Lexer)
#define GLECMA_TemplatePart GHOTIIO_LANG_ECMASCRIPT(GLECMA_TemplatePart)
#define GLECMA_Token GHOTIIO_LANG_ECMASCRIPT(GLECMA_Token)
#define GLECMA_TokenKind GHOTIIO_LANG_ECMASCRIPT(GLECMA_TokenKind)
#define GLECMA_ValueKind GHOTIIO_LANG_ECMASCRIPT(GLECMA_ValueKind)
#define glecma_failure_set GHOTIIO_LANG_ECMASCRIPT(glecma_failure_set)
#define glecma_feature_built GHOTIIO_LANG_ECMASCRIPT(glecma_feature_built)
#define glecma_feature_set_first_unbuilt \
  GHOTIIO_LANG_ECMASCRIPT(glecma_feature_set_first_unbuilt)
#define glecma_keyword_text GHOTIIO_LANG_ECMASCRIPT(glecma_keyword_text)
#define glecma_lex_ascii_class GHOTIIO_LANG_ECMASCRIPT(glecma_lex_ascii_class)
#define glecma_lex_ascii_is GHOTIIO_LANG_ECMASCRIPT(glecma_lex_ascii_is)
#define glecma_lex_count_cps GHOTIIO_LANG_ECMASCRIPT(glecma_lex_count_cps)
#define glecma_lex_cp GHOTIIO_LANG_ECMASCRIPT(glecma_lex_cp)
#define glecma_lex_hex_value GHOTIIO_LANG_ECMASCRIPT(glecma_lex_hex_value)
#define glecma_lex_is_id_part GHOTIIO_LANG_ECMASCRIPT(glecma_lex_is_id_part)
#define glecma_lex_is_id_start GHOTIIO_LANG_ECMASCRIPT(glecma_lex_is_id_start)
#define glecma_lex_is_line_terminator \
  GHOTIIO_LANG_ECMASCRIPT(glecma_lex_is_line_terminator)
#define glecma_lex_literal_limit \
  GHOTIIO_LANG_ECMASCRIPT(glecma_lex_literal_limit)
#define glecma_lex_locate GHOTIIO_LANG_ECMASCRIPT(glecma_lex_locate)
#define glecma_lex_scan_name_escape \
  GHOTIIO_LANG_ECMASCRIPT(glecma_lex_scan_name_escape)
#define glecma_lex_scan_number GHOTIIO_LANG_ECMASCRIPT(glecma_lex_scan_number)
#define glecma_lex_scan_regexp GHOTIIO_LANG_ECMASCRIPT(glecma_lex_scan_regexp)
#define glecma_lex_scan_string GHOTIIO_LANG_ECMASCRIPT(glecma_lex_scan_string)
#define glecma_lex_scan_template \
  GHOTIIO_LANG_ECMASCRIPT(glecma_lex_scan_template)
#define glecma_lex_syntax_error GHOTIIO_LANG_ECMASCRIPT(glecma_lex_syntax_error)
#define glecma_lex_text_init GHOTIIO_LANG_ECMASCRIPT(glecma_lex_text_init)
#define glecma_lex_unit GHOTIIO_LANG_ECMASCRIPT(glecma_lex_unit)
#define glecma_lex_walk GHOTIIO_LANG_ECMASCRIPT(glecma_lex_walk)
#define glecma_lexer_failure GHOTIIO_LANG_ECMASCRIPT(glecma_lexer_failure)
#define glecma_lexer_next GHOTIIO_LANG_ECMASCRIPT(glecma_lexer_next)
#define glecma_lexer_open GHOTIIO_LANG_ECMASCRIPT(glecma_lexer_open)
#define glecma_lexer_open_builtin \
  GHOTIIO_LANG_ECMASCRIPT(glecma_lexer_open_builtin)
#define glecma_lexer_reset GHOTIIO_LANG_ECMASCRIPT(glecma_lexer_reset)
#define glecma_lexer_run GHOTIIO_LANG_ECMASCRIPT(glecma_lexer_run)
#define glecma_lexer_set_strict GHOTIIO_LANG_ECMASCRIPT(glecma_lexer_set_strict)
#define glecma_parse_options_resolve \
  GHOTIIO_LANG_ECMASCRIPT(glecma_parse_options_resolve)
#define glecma_rule_message GHOTIIO_LANG_ECMASCRIPT(glecma_rule_message)
#define glecma_token_identifier_check \
  GHOTIIO_LANG_ECMASCRIPT(glecma_token_identifier_check)
#define glecma_token_kind_name GHOTIIO_LANG_ECMASCRIPT(glecma_token_kind_name)
#define glecma_token_punctuator GHOTIIO_LANG_ECMASCRIPT(glecma_token_punctuator)
#define glecma_token_value GHOTIIO_LANG_ECMASCRIPT(glecma_token_value)
#define glecma_token_value_free GHOTIIO_LANG_ECMASCRIPT(glecma_token_value_free)

/// @endcond

#endif // GHOTI_IO_GLECMA_NAMESPACE_H

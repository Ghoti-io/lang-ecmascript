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
 * The tokenizer: ECMA-262's lexical grammar (clause 12) and Annex B's
 * additions to it, read from a ::GLECMA_Source. Never installed.
 *
 * ECMA-262 has no single token stream. The lexical grammar is a set of
 * *input element goals*, and the syntactic grammar says which one applies at
 * each point: where a `/` is division and where it begins a regular
 * expression, where a `}` is a punctuator and where it continues a template.
 * Only the parser knows. So the tokenizer is pulled: ::glecma_lexer_next
 * reads one token under the goal its caller names, and a caller that needs
 * to look ahead and take it back copies the whole ::GLECMA_Lexer, which is a
 * plain value.
 *
 * A token is a kind, a few flags and a start and end ::GLECMA_Location; it
 * points into the source by offsets, never into text, and holds no
 * allocation. What a token *means* - an identifier's name with its escapes
 * decoded, a string's value, a template's cooked and raw text - is produced
 * on demand by ::glecma_token_value, so a parser that never asks (a
 * keyword, a punctuator) pays nothing. The values of numeric and BigInt
 * literals are not computed here: the compiler computes them, with `math`.
 *
 * The tokenizer validates as it scans: every numeric literal and string
 * escape is checked, including the strict-mode rules, and a bad one is
 * ::GLECMA_ERR_SYNTAX with a rule code and a location inside the construct.
 * A template whose escape is invalid is not an error here: its cooked value
 * is undefined and the token says so, because only the parser knows whether
 * the template is tagged.
 */

#ifndef GHOTI_IO_GLECMA_SRC_LEX_LEXER_H
#define GHOTI_IO_GLECMA_SRC_LEX_LEXER_H

#include <ghoti.io/lang-ecmascript/macros.h>

#include <ghoti.io/lang-ecmascript/allocator.h>
#include <ghoti.io/lang-ecmascript/core.h>
#include <ghoti.io/lang-ecmascript/limits.h>
#include <ghoti.io/lang-ecmascript/options.h>
#include <ghoti.io/lang-ecmascript/source.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The kind of a token.
 *
 * The punctuators have a kind each. ::GLECMA_TOK_NAME is every
 * IdentifierName: an identifier, a reserved word or a contextual keyword,
 * told apart by ::GLECMA_Token::keyword.
 */
typedef enum {
  GLECMA_TOK_EOF = 0,
  GLECMA_TOK_NAME,
  GLECMA_TOK_PRIVATE_NAME, ///< `#name`
  GLECMA_TOK_INTRINSIC_NAME, ///< `%Name`, only in the built-in mode
  GLECMA_TOK_NUMBER,
  GLECMA_TOK_BIGINT,
  GLECMA_TOK_STRING,
  GLECMA_TOK_TEMPLATE,
  GLECMA_TOK_REGEXP,
  GLECMA_TOK_LBRACE, GLECMA_TOK_RBRACE, GLECMA_TOK_LPAREN, GLECMA_TOK_RPAREN,
  GLECMA_TOK_LBRACKET, GLECMA_TOK_RBRACKET, GLECMA_TOK_DOT,
  GLECMA_TOK_ELLIPSIS, GLECMA_TOK_SEMICOLON, GLECMA_TOK_COMMA,
  GLECMA_TOK_LT, GLECMA_TOK_GT, GLECMA_TOK_LE, GLECMA_TOK_GE, GLECMA_TOK_EQ,
  GLECMA_TOK_NE, GLECMA_TOK_SEQ, GLECMA_TOK_SNE,
  GLECMA_TOK_PLUS, GLECMA_TOK_MINUS, GLECMA_TOK_STAR, GLECMA_TOK_SLASH,
  GLECMA_TOK_PERCENT, GLECMA_TOK_STARSTAR, GLECMA_TOK_INC, GLECMA_TOK_DEC,
  GLECMA_TOK_SHL, GLECMA_TOK_SHR, GLECMA_TOK_USHR,
  GLECMA_TOK_AMP, GLECMA_TOK_PIPE, GLECMA_TOK_CARET, GLECMA_TOK_BANG,
  GLECMA_TOK_TILDE, GLECMA_TOK_ANDAND, GLECMA_TOK_OROR, GLECMA_TOK_QQ,
  GLECMA_TOK_QUESTION, GLECMA_TOK_QDOT, GLECMA_TOK_COLON,
  GLECMA_TOK_ASSIGN, GLECMA_TOK_PLUS_ASSIGN, GLECMA_TOK_MINUS_ASSIGN,
  GLECMA_TOK_STAR_ASSIGN, GLECMA_TOK_SLASH_ASSIGN, GLECMA_TOK_PERCENT_ASSIGN,
  GLECMA_TOK_STARSTAR_ASSIGN, GLECMA_TOK_SHL_ASSIGN, GLECMA_TOK_SHR_ASSIGN,
  GLECMA_TOK_USHR_ASSIGN, GLECMA_TOK_AMP_ASSIGN, GLECMA_TOK_PIPE_ASSIGN,
  GLECMA_TOK_CARET_ASSIGN, GLECMA_TOK_ANDAND_ASSIGN, GLECMA_TOK_OROR_ASSIGN,
  GLECMA_TOK_QQ_ASSIGN, GLECMA_TOK_ARROW,
  GLECMA_TOK_COUNT ///< Not a kind.
} GLECMA_TokenKind;

/**
 * @brief The word an IdentifierName spells, when it spells one the grammar
 *   gives a meaning to.
 *
 * The value is what the name spells *after* its escapes are decoded, so
 * `if` has ::GLECMA_KW_IF with ::GLECMA_TOKF_ESCAPED set; the parser
 * must not treat an escaped word as the keyword. Appended to, never
 * renumbered: the enumeration is internal, but a table is indexed by it.
 */
typedef enum {
  GLECMA_KW_NONE = 0,
  // Reserved words (clause 12.7.2): never an Identifier.
  GLECMA_KW_BREAK, GLECMA_KW_CASE, GLECMA_KW_CATCH, GLECMA_KW_CLASS,
  GLECMA_KW_CONST, GLECMA_KW_CONTINUE, GLECMA_KW_DEBUGGER, GLECMA_KW_DEFAULT,
  GLECMA_KW_DELETE, GLECMA_KW_DO, GLECMA_KW_ELSE, GLECMA_KW_ENUM,
  GLECMA_KW_EXPORT, GLECMA_KW_EXTENDS, GLECMA_KW_FALSE, GLECMA_KW_FINALLY,
  GLECMA_KW_FOR, GLECMA_KW_FUNCTION, GLECMA_KW_IF, GLECMA_KW_IMPORT,
  GLECMA_KW_IN, GLECMA_KW_INSTANCEOF, GLECMA_KW_NEW, GLECMA_KW_NULL,
  GLECMA_KW_RETURN, GLECMA_KW_SUPER, GLECMA_KW_SWITCH, GLECMA_KW_THIS,
  GLECMA_KW_THROW, GLECMA_KW_TRUE, GLECMA_KW_TRY, GLECMA_KW_TYPEOF,
  GLECMA_KW_VAR, GLECMA_KW_VOID, GLECMA_KW_WHILE, GLECMA_KW_WITH,
  // Reserved in some contexts: `await` in a module and in async code,
  // `yield` in strict code and in generators.
  GLECMA_KW_AWAIT, GLECMA_KW_YIELD,
  // Reserved only in strict code (clause 13.1.1).
  GLECMA_KW_IMPLEMENTS, GLECMA_KW_INTERFACE, GLECMA_KW_LET,
  GLECMA_KW_PACKAGE, GLECMA_KW_PRIVATE, GLECMA_KW_PROTECTED,
  GLECMA_KW_PUBLIC, GLECMA_KW_STATIC,
  // Contextual words the grammar gives a meaning to in some positions and
  // that are identifiers everywhere else.
  GLECMA_KW_ASYNC, GLECMA_KW_OF, GLECMA_KW_GET, GLECMA_KW_SET, GLECMA_KW_AS,
  GLECMA_KW_FROM, GLECMA_KW_TARGET, GLECMA_KW_META, GLECMA_KW_ARGUMENTS,
  GLECMA_KW_EVAL,
  GLECMA_KW_COUNT ///< Not a keyword.
} GLECMA_Keyword;

/** @name Token flags */
///@{
#define GLECMA_TOKF_NEWLINE_BEFORE 0x01u ///< A line terminator lies between
                                         ///< the previous token and this one,
                                         ///< in whitespace or in a comment.
#define GLECMA_TOKF_ESCAPED 0x02u        ///< A name written with a `\u`
                                         ///< escape.
#define GLECMA_TOKF_LEGACY_OCTAL 0x04u   ///< A legacy octal literal (`017`),
                                         ///< or a string holding a legacy
                                         ///< octal escape (`"\07"`).
#define GLECMA_TOKF_NON_OCTAL_DECIMAL 0x08u ///< A leading-zero decimal
                                            ///< literal (`089`), or a string
                                            ///< holding `\8` or `\9`.
#define GLECMA_TOKF_COOKED_UNDEFINED 0x10u ///< A template part whose cooked
                                           ///< value is undefined because of
                                           ///< an invalid escape.
///@}

/** @brief Which part of a template literal a template token is. */
typedef enum {
  GLECMA_TEMPLATE_NO_SUBSTITUTION = 0, ///< `` `...` ``
  GLECMA_TEMPLATE_HEAD,                ///< `` `...${ ``
  GLECMA_TEMPLATE_MIDDLE,              ///< `` }...${ ``
  GLECMA_TEMPLATE_TAIL                 ///< `` }...` ``
} GLECMA_TemplatePart;

/** @brief One token. */
typedef struct GLECMA_Token {
  GLECMA_TokenKind kind;
  GLECMA_Keyword keyword;          ///< For ::GLECMA_TOK_NAME.
  GLECMA_TemplatePart template_part; ///< For ::GLECMA_TOK_TEMPLATE.
  uint32_t flags;                  ///< ::GLECMA_TOKF_NEWLINE_BEFORE and the
                                   ///< other flags.
  GLECMA_Location start;           ///< The first code point of the token.
  GLECMA_Location end;             ///< The position after the last.
  size_t regexp_flags_offset;      ///< For ::GLECMA_TOK_REGEXP: the offset of
                                   ///< the flags, equal to `end.offset` when
                                   ///< there are none. The body lies between
                                   ///< the slashes.
  uint32_t escape_rule;            ///< For a template with
                                   ///< ::GLECMA_TOKF_COOKED_UNDEFINED: the
                                   ///< rule of the first invalid escape,
                                   ///< which an untagged template reports.
  GLECMA_Location escape_location; ///< And where that escape is.
} GLECMA_Token;

/** @name Input element goals (clause 12) */
///@{
#define GLECMA_GOAL_DIV 0u            ///< InputElementDiv.
#define GLECMA_GOAL_REGEXP 1u         ///< InputElementRegExp: a `/` begins a
                                      ///< regular expression.
#define GLECMA_GOAL_TEMPLATE_TAIL 2u  ///< InputElementTemplateTail: a `}` is
                                      ///< a template's middle or tail.
#define GLECMA_GOAL_REGEXP_OR_TEMPLATE_TAIL 3u ///< Both.
///@}

/** @name Lexer mode flags */
///@{
#define GLECMA_LEXF_STRICT 0x01u  ///< Strict code: legacy octal and `\8` are
                                  ///< errors.
#define GLECMA_LEXF_MODULE 0x02u  ///< The Module goal: strict, and no
                                  ///< HTML-like comments.
#define GLECMA_LEXF_BUILTIN 0x04u ///< The internal built-in mode: `%Name` is
                                  ///< one token where a primary expression
                                  ///< is expected. Set only by
                                  ///< ::glecma_lexer_open_builtin.
///@}

/** @brief What the tokenizer last failed with. */
typedef struct GLECMA_LexError {
  GLECMA_Result result;     ///< ::GLECMA_OK until something fails.
  uint32_t rule;            ///< A ::GLECMA_Rule, for ::GLECMA_ERR_SYNTAX.
  const char * message;     ///< Static.
  GLECMA_Location location; ///< Inside the offending construct.
  const char * limit;       ///< Static, for ::GLECMA_ERR_LIMIT.
} GLECMA_LexError;

/**
 * @brief The tokenizer's state: a plain value, copied to save a position.
 *
 * Fields are read-only to callers; the state holds a borrowed pointer to the
 * source, which must outlive it.
 */
typedef struct GLECMA_Lexer {
  const GLECMA_Source * source;
  GLECMA_Location position; ///< Where the next token's trivia begins.
  uint64_t tokens;          ///< Tokens produced so far.
  uint64_t max_tokens;      ///< ::GLECMA_Limits::tokens.
  uint64_t max_literal;     ///< ::GLECMA_Limits::literal_length.
  uint32_t mode;            ///< ::GLECMA_LEXF_STRICT and the others.
  bool bounded;             ///< The text ends at `limit`, not at the source's end.
  size_t limit;             ///< The offset the text ends at, when `bounded`.
  bool line_start;          ///< Only white space and comments lie between the
                            ///< last line terminator, or the start of the
                            ///< source, and here: what `-->` needs.
  GLECMA_LexError error;
} GLECMA_Lexer;

/**
 * @brief Opens a tokenizer on a source for one of the public goals.
 *
 * Refuses a feature set that names a feature the build lacks before reading
 * any token. The built-in mode bit is never set by this entry.
 *
 * @param lexer Receives the state; written only on success.
 * @param source The source; must outlive the lexer.
 * @param options The parse options, or NULL for the defaults.
 * @param mode ::GLECMA_LEXF_STRICT, ::GLECMA_LEXF_MODULE (which implies
 *   strict) or 0 for a sloppy Script.
 * @param failure Optional; written on every return.
 * @return ::GLECMA_OK; ::GLECMA_ERR_INVALID for a NULL argument, a
 *   ::GLECMA_LEXF_BUILTIN flag or options and limits with a `size` below
 *   the header; ::GLECMA_ERR_UNSUPPORTED for a feature the build lacks, named
 *   in `failure->feature`.
 */
GLECMA_INTERNAL_API GLECMA_Result glecma_lexer_open(GLECMA_Lexer * lexer,
    const GLECMA_Source * source, const GLECMA_ParseOptions * options,
    uint32_t mode, GLECMA_Failure * failure);

/**
 * @brief Opens a tokenizer in the internal built-in mode.
 *
 * The one entry that sets ::GLECMA_LEXF_BUILTIN, which turns
 * `%IdentifierName` into one ::GLECMA_TOK_INTRINSIC_NAME token where the
 * parser expects a primary expression. The mode is a strict Script. Its
 * caller is the image tool; no public header declares it.
 */
GLECMA_INTERNAL_API GLECMA_Result glecma_lexer_open_builtin(
    GLECMA_Lexer * lexer, const GLECMA_Source * source,
    const GLECMA_ParseOptions * options, GLECMA_Failure * failure);

/**
 * @brief Changes the strictness of the code being read.
 *
 * Called by the parser when a directive prologue makes a function strict.
 * Tokens already read are not re-checked; the flags on a token let the
 * parser report a legacy octal literal or escape that came before the
 * directive.
 */
GLECMA_INTERNAL_API void glecma_lexer_set_strict(GLECMA_Lexer * lexer,
    bool strict);

/**
 * @brief Moves the tokenizer to a position of its own source, clearing any
 *   error.
 *
 * @param lexer The tokenizer.
 * @param where A location obtained from this source (a token's start or
 *   end, or ::glecma_source_location).
 */
GLECMA_INTERNAL_API void glecma_lexer_reset(GLECMA_Lexer * lexer,
    const GLECMA_Location * where);

/**
 * @brief Makes the tokenizer read only [start, end) of its source.
 *
 * Moves it to @p start, and what lies at or after @p end is the end of the
 * text: the token after the last one of the range is the end of input, and
 * nothing past the range is read, so a token that only the code around would
 * have read differently (a `/` after a function declaration) cannot fail here.
 *
 * @return ::GLECMA_OK; ::GLECMA_ERR_INVALID for an empty or backwards range, one
 *   past the source, or an offset that is inside a code point.
 */
GLECMA_INTERNAL_API GLECMA_Result glecma_lexer_set_range(GLECMA_Lexer * lexer,
    size_t start, size_t end);

/**
 * @brief Reads the next token under an input element goal.
 *
 * Skips white space and comments first. The end of input is a
 * ::GLECMA_TOK_EOF token, which is not counted against the token cap and
 * which every later call returns again.
 *
 * @param lexer The tokenizer.
 * @param goal A ::GLECMA_GOAL_DIV, ::GLECMA_GOAL_REGEXP,
 *   ::GLECMA_GOAL_TEMPLATE_TAIL or ::GLECMA_GOAL_REGEXP_OR_TEMPLATE_TAIL.
 * @param token Receives the token; written only on success.
 * @return ::GLECMA_OK; ::GLECMA_ERR_SYNTAX (the rule and location are in the
 *   lexer's error); ::GLECMA_ERR_LIMIT naming `tokens` or `literal_length`.
 *   After a failure every further call returns the same failure.
 */
GLECMA_INTERNAL_API GLECMA_Result glecma_lexer_next(GLECMA_Lexer * lexer,
    uint32_t goal, GLECMA_Token * token);

/**
 * @brief Writes the lexer's error into a failure record.
 *
 * @param lexer The tokenizer.
 * @param failure The record, or NULL (which does nothing); its `size` must
 *   cover the header.
 */
GLECMA_INTERNAL_API void glecma_lexer_failure(const GLECMA_Lexer * lexer,
    GLECMA_Failure * failure);

/** @brief Which value of a token ::glecma_token_value produces. */
typedef enum {
  GLECMA_VALUE_COOKED = 0, ///< An identifier's name, a string's value (SV),
                           ///< a template part's cooked value (TV).
  GLECMA_VALUE_RAW         ///< A template part's raw value (TRV).
} GLECMA_ValueKind;

/**
 * @brief Produces a token's text value as UTF-16 code units.
 *
 * For a name, the IdentifierName with its escapes decoded; for a string
 * literal, its value (a lone surrogate stays one); for a template part, its
 * cooked value (not defined when ::GLECMA_TOKF_COOKED_UNDEFINED is set) or
 * its raw value, with CRLF and CR read as LF. A private name's value
 * excludes the `#` and an intrinsic name's the `%`.
 *
 * @param source The source the token was read from.
 * @param token A ::GLECMA_TOK_NAME, ::GLECMA_TOK_PRIVATE_NAME,
 *   ::GLECMA_TOK_INTRINSIC_NAME, ::GLECMA_TOK_STRING or ::GLECMA_TOK_TEMPLATE
 *   token.
 * @param kind Which value.
 * @param allocator Allocator, or NULL for the default.
 * @param out_units Receives the units, allocated through @p allocator; free
 *   with ::glecma_token_value_free. NULL when there are none.
 * @param out_length Receives the number of units.
 * @return ::GLECMA_OK; ::GLECMA_ERR_INVALID for a token without a value, a
 *   cooked value that is undefined or NULL arguments; ::GLECMA_ERR_OOM.
 */
GLECMA_INTERNAL_API GLECMA_Result glecma_token_value(
    const GLECMA_Source * source, const GLECMA_Token * token,
    GLECMA_ValueKind kind, const GLECMA_Allocator * allocator,
    uint16_t ** out_units, size_t * out_length);

/**
 * @brief Produces a token's text value into a caller's buffer.
 *
 * The same value as ::glecma_token_value, with no allocation: @p buffer must
 * hold at least as many units as the token spans in the source (the most a
 * value can take). When the value is empty, @p out_length is zero.
 *
 * @return ::GLECMA_OK; ::GLECMA_ERR_INVALID for what ::glecma_token_value
 *   refuses or a buffer that is too small.
 */
GLECMA_INTERNAL_API GLECMA_Result glecma_token_value_into(
    const GLECMA_Source * source, const GLECMA_Token * token,
    GLECMA_ValueKind kind, uint16_t * buffer, size_t capacity,
    size_t * out_length);

/** @brief Frees what ::glecma_token_value returned. */
GLECMA_INTERNAL_API void glecma_token_value_free(
    const GLECMA_Allocator * allocator, uint16_t * units);

/** @name Context for ::glecma_token_identifier_check */
///@{
#define GLECMA_IDF_STRICT 0x01u ///< The code is strict.
#define GLECMA_IDF_YIELD 0x02u  ///< `yield` is reserved here (a generator).
#define GLECMA_IDF_AWAIT 0x04u  ///< `await` is reserved here (a module or an
                                ///< async function).
///@}

/**
 * @brief Whether a name token may be an Identifier in a context.
 *
 * Where the grammar needs an Identifier (a binding, a reference, a label)
 * the parser asks here: a reserved word, and a word reserved in the context,
 * is refused, and so is a reserved word written with an escape (clause 12.7.1). A
 * name that is not an Identifier in the grammar's sense (a property name
 * after a dot, say) is never asked about.
 *
 * @param token A ::GLECMA_TOK_NAME token.
 * @param context ::GLECMA_IDF_STRICT and the others.
 * @return 0 when the name may be an Identifier, else the ::GLECMA_Rule it
 *   breaks.
 */
GLECMA_INTERNAL_API uint32_t glecma_token_identifier_check(
    const GLECMA_Token * token, uint32_t context);

/** @brief The text of a punctuator kind, such as ">>>="; NULL for others. */
GLECMA_INTERNAL_API const char * glecma_token_punctuator(GLECMA_TokenKind kind);

/** @brief The name of a token kind, such as "NAME" or ">>>=". */
GLECMA_INTERNAL_API const char * glecma_token_kind_name(GLECMA_TokenKind kind);

/** @brief The text a keyword spells; NULL for ::GLECMA_KW_NONE. */
GLECMA_INTERNAL_API const char * glecma_keyword_text(GLECMA_Keyword keyword);

/** @brief Whether a code point is IdentifierStartChar (clause 12.7). */
GLECMA_INTERNAL_API bool glecma_lex_is_id_start(uint32_t code_point);

/** @brief Whether a code point is IdentifierPartChar (clause 12.7). */
GLECMA_INTERNAL_API bool glecma_lex_is_id_part(uint32_t code_point);

/**
 * @brief Called by ::glecma_lexer_run for each token.
 *
 * @return false to stop.
 */
typedef bool (*GLECMA_LexVisitor)(void * context, const GLECMA_Token * token);

/**
 * @brief Tokenizes to the end, choosing each goal from the previous token.
 *
 * A driver for tests, the fuzzers and the benchmark. Only a parser can say
 * whether a `/` after `)` or `}` is division, so this guesses: a `/` begins
 * a regular expression after nothing, after a punctuator other than `)`,
 * `]` or `}`, and after a keyword that is followed by an expression, and is
 * division otherwise. It follows `${ ... }` of templates by counting braces,
 * so a template's pieces are found. It is exact for sources where that
 * guess is right and is never used by the parser.
 *
 * @param lexer The tokenizer, positioned anywhere.
 * @param visit Called for each token, the end of input included; may be NULL.
 * @param context Passed to @p visit.
 * @return ::GLECMA_OK when the end of input was reached or @p visit stopped,
 *   else the failing result (the details are in the lexer's error).
 */
GLECMA_INTERNAL_API GLECMA_Result glecma_lexer_run(GLECMA_Lexer * lexer,
    GLECMA_LexVisitor visit, void * context);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GLECMA_SRC_LEX_LEXER_H

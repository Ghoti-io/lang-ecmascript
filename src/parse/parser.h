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
 * The parser's state and what its translation units share. Never installed.
 *
 * The parser is recursive descent over ::GLECMA_Lexer, which it pulls one
 * token at a time under the input element goal the grammar names at that
 * point: a token is lexed as division or `}` by default, and the parser
 * *rescans* it as a regular expression, a template continuation or an
 * intrinsic name at the points where the grammar expects one. Lookahead
 * copies the lexer, which is a plain value.
 *
 * Errors are not returned up the call chain: the first one is recorded in the
 * parser and control returns to the entry by `longjmp`. Nothing the parser
 * owns is released on the way, so everything it owns hangs off ::Parser and
 * is released once, at the entry, whichever way the parse ended. The tree is
 * the builder's, and is destroyed by the entry on failure.
 *
 * The parser never reads a node (see builder.h). What it needs to know about
 * an expression it has parsed is in a ::PExpr.
 */

#ifndef GHOTI_IO_GLECMA_SRC_PARSE_PARSER_H
#define GHOTI_IO_GLECMA_SRC_PARSE_PARSER_H

#include <ghoti.io/lang-ecmascript/macros.h>

#include <ghoti.io/lang-ecmascript/tree.h>

#include "core/core_internal.h"
#include "lex/lexer.h"
#include "lex/lex_internal.h"
#include "parse/builder.h"

#include <setjmp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A function the compiler must not inline.
 *
 * The functions that hold a ::Snap, and the other large frames on the
 * recursive path, are kept out of line so that the native stack one level of
 * nesting takes is the sum of small frames and not whatever inlining made of
 * them (design.md, "Recursive descent and the stack").
 */
#if defined(__GNUC__) || defined(__clang__)
#define PARSER_NOINLINE __attribute__((noinline))
#elif defined(_MSC_VER)
#define PARSER_NOINLINE __declspec(noinline)
#else
#define PARSER_NOINLINE
#endif

/** @brief A function that never returns: it leaves by `longjmp`. */
#define GLECMA_NORETURN _Noreturn

/** @name Parser context flags */
///@{
#define PF_STRICT 0x01u         ///< Strict code.
#define PF_YIELD 0x02u          ///< `yield` is an operator (a generator's code).
#define PF_AWAIT 0x04u          ///< `await` is an operator (an async function's code).
#define PF_AWAIT_RESERVED 0x08u ///< `await` is not an identifier.
#define PF_BUILTIN 0x10u        ///< The internal built-in entry.
///@}

/** @name What a parsed expression is, as far as its parent needs to know */
///@{
#define SH_OTHER 0u
#define SH_IDENT 1u   ///< A plain identifier reference.
#define SH_MEMBER 2u  ///< A member access without `?.`, `super.x` included.
#define SH_CALL 3u    ///< A call (not `super(...)`, not `import(...)`).
#define SH_OPTCHAIN 4u ///< An optional chain.
#define SH_ARRAY 5u   ///< An array literal.
#define SH_OBJECT 6u  ///< An object literal.
#define SH_STRING 7u  ///< A string literal.
#define SH_ARROW 8u   ///< An arrow function: nothing can follow it.
#define SH_PRIVATE 9u ///< A bare `#name`, valid only before `in`.
#define SH_UNARY 10u  ///< A unary-operator expression: not an operand of `**`.
#define SH_FUNCTION 11u ///< A function or class expression.
///@}

/** @name ::PExpr aux bits */
///@{
#define PA_NULLISH 0x01u ///< An unparenthesized `??` expression.
#define PA_ANDOR 0x02u   ///< An unparenthesized `&&` or `||` expression.
#define PA_EVAL 0x04u    ///< The identifier `eval`.
#define PA_ASYNC 0x08u   ///< The identifier `async`, written without an escape.
///@}

/** @brief A parsed expression: its node, range and the facts its parent needs. */
typedef struct PExpr {
  GLECMA_BNode node;
  size_t start;
  size_t end;
  uint8_t shape;
  uint8_t paren; ///< Written in parentheses.
  uint8_t aux;
} PExpr;

/** @brief A saved position of the whole parse, to return to for a cover grammar. */
typedef struct Snap {
  GLECMA_Lexer lex;
  GLECMA_Token tok;
  size_t prev_end;
  GLECMA_BMark mark;
  uint64_t nodes;
  size_t kids;
  size_t pending;
  GLECMA_Location pending_at;
} Snap;

#define NO_PENDING SIZE_MAX

typedef struct Parser {
  const GLECMA_Source * source;
  GLECMA_Lexer lex;
  GLECMA_Token tok;
  size_t prev_end; ///< End offset of the token before `tok`.
  GLECMA_TreeBuilder * builder;
  GLECMA_LexText text;
  const GLECMA_Allocator * allocator;
  jmp_buf jump;

  // The first failure.
  GLECMA_Result result;
  uint32_t rule;
  const char * message;
  GLECMA_Location where;
  const char * limit;

  // Caps and what has been used.
  uint64_t max_nesting;
  uint64_t max_nodes;
  uint64_t native_stack_bytes;
  uint64_t depth;
  uint64_t nodes;
  uintptr_t stack_base;

  // Context.
  uint32_t flags;
  bool no_in;           ///< `in` is not an operator here (a `for` head).
  size_t assign_start;  ///< Where the assignment expression being parsed began.
  size_t pending;       ///< Offset of a CoverInitializedName `=` not yet ruled on.
  GLECMA_Location pending_at;

  // Children being collected for the node about to be built.
  GLECMA_BNode * kids;
  size_t kids_len;
  size_t kids_cap;

  // Room for the text of a token that is short, so that the common name
  // is decoded without an allocation.
  uint16_t scratch[256];

  // The directive prologue being read (stmt.c).
  bool prologue_item;  ///< The statement being parsed may be a directive.
  bool directive_hit;  ///< It was one.

  // Offsets at which a parenthesized group was found to be arrow parameters.
  size_t * memo;
  size_t memo_cap;
  size_t memo_count;
} Parser;

/** @brief The context a name is checked in (GLECMA_IDF_*). */
static inline uint32_t parser_idf(const Parser * p) {
  return ((p->flags & PF_STRICT) ? GLECMA_IDF_STRICT : 0u) |
      ((p->flags & PF_YIELD) ? GLECMA_IDF_YIELD : 0u) |
      ((p->flags & PF_AWAIT_RESERVED) ? GLECMA_IDF_AWAIT : 0u);
}

/** @brief A property name as parsed: its node and what the token was. */
typedef struct PKey {
  GLECMA_BNode node;
  GLECMA_Token tok;  ///< The name token; for a computed key, the `[`.
  size_t start;
  size_t end;
  bool computed;
  bool is_name;      ///< An IdentifierName (a reserved word included).
  bool is_private;
} PKey;

/* ---- parser.c: infrastructure -------------------------------------------- */

GLECMA_INTERNAL_API GLECMA_NORETURN void parser_fail(Parser * p, uint32_t rule,
    const GLECMA_Location * where);
GLECMA_INTERNAL_API GLECMA_NORETURN void parser_fail_token(Parser * p,
    uint32_t rule);
GLECMA_INTERNAL_API GLECMA_NORETURN void parser_fail_limit(Parser * p,
    const char * limit);
GLECMA_INTERNAL_API GLECMA_NORETURN void parser_fail_result(Parser * p,
    GLECMA_Result result);
GLECMA_INTERNAL_API GLECMA_NORETURN void parser_unexpected(Parser * p);
GLECMA_INTERNAL_API void parser_next(Parser * p);
GLECMA_INTERNAL_API bool parser_peek(Parser * p, GLECMA_Token * out);
GLECMA_INTERNAL_API bool parser_peek2(Parser * p, GLECMA_Token * first,
    GLECMA_Token * second);
/** @brief Whether a token can begin a property name (and so follow `get`, `set`). */
GLECMA_INTERNAL_API bool token_starts_property_name(const GLECMA_Token * t);
GLECMA_INTERNAL_API void parser_rescan(Parser * p, uint32_t goal);
GLECMA_INTERNAL_API void parser_expect(Parser * p, GLECMA_TokenKind kind);
GLECMA_INTERNAL_API bool parser_eat(Parser * p, GLECMA_TokenKind kind);
GLECMA_INTERNAL_API void parser_semicolon(Parser * p);
GLECMA_INTERNAL_API void parser_snap(const Parser * p, Snap * snap);
GLECMA_INTERNAL_API void parser_restore(Parser * p, const Snap * snap);
GLECMA_INTERNAL_API void parser_enter(Parser * p);
GLECMA_INTERNAL_API void parser_leave(Parser * p);
GLECMA_INTERNAL_API void parser_check_stack(Parser * p);
GLECMA_INTERNAL_API void parser_set_strict(Parser * p, bool strict);
GLECMA_INTERNAL_API void parser_memo_add(Parser * p, size_t key);
GLECMA_INTERNAL_API bool parser_memo_has(const Parser * p, size_t key);

GLECMA_INTERNAL_API void parser_kid(Parser * p, GLECMA_BNode node);
GLECMA_INTERNAL_API GLECMA_BNode parser_make(Parser * p, uint32_t kind,
    uint32_t flags, uint32_t op, size_t start, size_t end, size_t kids_mark);
GLECMA_INTERNAL_API GLECMA_BNode parser_make_body(Parser * p, uint32_t kind,
    uint32_t flags, size_t start, size_t end, size_t kids_mark);
GLECMA_INTERNAL_API GLECMA_BNode parser_make_text(Parser * p, uint32_t kind,
    uint32_t flags, size_t start, size_t end, const GLECMA_BText * text0,
    const GLECMA_BText * text1);
GLECMA_INTERNAL_API void parser_text(Parser * p, const GLECMA_Token * token,
    GLECMA_ValueKind kind, bool intern, GLECMA_BText * out);
GLECMA_INTERNAL_API void parser_slice(Parser * p, size_t from, size_t to,
    GLECMA_BText * out);
GLECMA_INTERNAL_API void parser_event(Parser * p, uint32_t type, uint32_t what,
    const GLECMA_BText * name, size_t start, size_t end);
GLECMA_INTERNAL_API void parser_amend_reference(Parser * p, size_t start,
    uint32_t add, uint32_t clear);
GLECMA_INTERNAL_API bool parser_materializing(const Parser * p);

/** @brief Whether the current token is the keyword (and not written with an escape). */
static inline bool parser_is_kw(const Parser * p, GLECMA_Keyword keyword) {
  return p->tok.kind == GLECMA_TOK_NAME && p->tok.keyword == keyword &&
      (p->tok.flags & GLECMA_TOKF_ESCAPED) == 0;
}

/** @brief Whether a token is the keyword (and not written with an escape). */
static inline bool token_is_kw(const GLECMA_Token * t, GLECMA_Keyword keyword) {
  return t->kind == GLECMA_TOK_NAME && t->keyword == keyword &&
      (t->flags & GLECMA_TOKF_ESCAPED) == 0;
}

/* ---- expr.c --------------------------------------------------------------- */

#define EF_COVER 0x01u ///< The caller may still turn the result into a pattern.

GLECMA_INTERNAL_API PExpr parse_expression(Parser * p);
GLECMA_INTERNAL_API PExpr parse_assignment(Parser * p, uint32_t ef);
GLECMA_INTERNAL_API PExpr parse_unary(Parser * p);
GLECMA_INTERNAL_API PExpr parse_lhs(Parser * p);
GLECMA_INTERNAL_API PExpr parse_ident_ref(Parser * p);
GLECMA_INTERNAL_API void parse_arguments(Parser * p, size_t * out_end);
GLECMA_INTERNAL_API PExpr parse_template(Parser * p, bool tagged);
GLECMA_INTERNAL_API PExpr parse_function_expression(Parser * p, size_t start,
    bool is_async);
GLECMA_INTERNAL_API PExpr parse_class_expression(Parser * p);
GLECMA_INTERNAL_API PExpr parse_arrow_paren(Parser * p, size_t start,
    bool is_async);
GLECMA_INTERNAL_API PExpr parse_arrow_ident(Parser * p, size_t start,
    bool is_async, const GLECMA_Token * ident, const GLECMA_BText * name);
GLECMA_INTERNAL_API bool expr_valid_target(const Parser * p, const PExpr * e,
    bool allow_call);
GLECMA_INTERNAL_API void expr_mark_write(Parser * p, const PExpr * e,
    bool also_read);
GLECMA_INTERNAL_API void parse_property_key(Parser * p, PKey * key,
    bool allow_private);
GLECMA_INTERNAL_API bool token_is_ascii(const Parser * p,
    const GLECMA_Token * token, const char * ascii);

/* ---- pattern.c ------------------------------------------------------------ */

GLECMA_INTERNAL_API PExpr parse_binding_target(Parser * p, uint32_t decl);
GLECMA_INTERNAL_API PExpr parse_binding_element(Parser * p, uint32_t decl);
GLECMA_INTERNAL_API PExpr parse_assignment_pattern(Parser * p);
GLECMA_INTERNAL_API PExpr parse_binding_identifier(Parser * p, uint32_t decl);

/* ---- func.c --------------------------------------------------------------- */

#define FN_ASYNC 0x01u
#define FN_GENERATOR 0x02u
#define FN_METHOD 0x04u
#define FN_GETTER 0x08u
#define FN_SETTER 0x10u
#define FN_CONSTRUCTOR 0x20u
#define FN_DERIVED 0x40u

GLECMA_INTERNAL_API PExpr parse_function_declaration(Parser * p, size_t start,
    bool is_async, bool allow_generator);
GLECMA_INTERNAL_API PExpr parse_method(Parser * p, size_t start, uint32_t fn);
GLECMA_INTERNAL_API PExpr parse_class_declaration(Parser * p);
GLECMA_INTERNAL_API GLECMA_BNode parse_function_params(Parser * p,
    uint32_t decl_kind, uint32_t fn, size_t * out_end);
GLECMA_INTERNAL_API GLECMA_BNode parse_function_body(Parser * p,
    size_t * out_end, bool * out_strict);

/* ---- stmt.c --------------------------------------------------------------- */

GLECMA_INTERNAL_API GLECMA_BNode parse_statement(Parser * p);
GLECMA_INTERNAL_API GLECMA_BNode parse_statement_list_item(Parser * p);
GLECMA_INTERNAL_API GLECMA_BNode parse_block(Parser * p, bool scoped);
GLECMA_INTERNAL_API void parse_statement_list(Parser * p, GLECMA_TokenKind end,
    bool prologue, size_t * out_end, bool * out_strict);
/** @name What a declaration list reported to a `for` head */
///@{
#define VDI_PATTERN_NO_INIT 0x01u ///< A destructuring declarator without an initializer.
#define VDI_CONST_NO_INIT 0x02u   ///< A `const` declarator without an initializer.
#define VDI_MULTIPLE 0x04u        ///< More than one declarator.
#define VDI_HAS_INIT 0x08u        ///< The first declarator has an initializer.
#define VDI_PATTERN 0x10u         ///< The first declarator is a destructuring pattern.
///@}

GLECMA_INTERNAL_API GLECMA_BNode parse_variable_declaration(Parser * p,
    uint32_t kind, bool in_for, size_t start, uint32_t * out_info);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GLECMA_SRC_PARSE_PARSER_H

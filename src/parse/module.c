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
 * The Module goal's own syntax and early errors (clauses 16.2.1.1, 16.2.2,
 * 16.2.3): import and export declarations, the names a module exports and the
 * local names those exports refer to.
 *
 * Like every check in the parser these read the parser's own records and
 * never a node. Module items occur only at the top level, outside every
 * cover grammar, so none of the records here is cut back by a rewind. The
 * exported names of a declaration are taken from the scope records the
 * declaration made, which are the module scope's own names (a nested
 * function's, block's or class's are gone when its scope ends), and the local
 * names `export { a }` refers to are matched against the module scope's
 * declarations once the whole module is read, because a declaration may come
 * after the export that names it.
 */

#include <ghoti.io/lang-ecmascript/macros.h>

#include "parse/parser.h"

#include <string.h>

/* ---- Sets of interned names ----------------------------------------------- */

static size_t ptr_slot(const uint16_t * key, size_t mask) {
  return (size_t)(((uintptr_t)key >> 4) * (uintptr_t)0x9E3779B97F4A7C15ull >> 20) & mask;
}

// Adds a key; false when it was already there.
static bool ptrset_add(Parser * p, PtrSet * set, const uint16_t * key) {
  if ((set->count + 1) * 2 > set->capacity) {
    size_t capacity = set->capacity == 0 ? 16 : set->capacity * 2;
    if (capacity > SIZE_MAX / sizeof(*set->keys)) {
      parser_fail_result(p, GLECMA_ERR_OOM);
    }
    const uint16_t ** keys = (const uint16_t **)p->allocator->calloc_fn(
        p->allocator->ctx, capacity, sizeof(*keys));
    if (keys == NULL) {
      parser_fail_result(p, GLECMA_ERR_OOM);
    }
    for (size_t i = 0; i < set->capacity; i++) {
      if (set->keys[i] != NULL) {
        size_t slot = ptr_slot(set->keys[i], capacity - 1);
        while (keys[slot] != NULL) {
          slot = (slot + 1) & (capacity - 1);
        }
        keys[slot] = set->keys[i];
      }
    }
    if (set->keys != NULL) {
      p->allocator->free_fn(p->allocator->ctx, (void *)set->keys);
    }
    set->keys = keys;
    set->capacity = capacity;
  }
  size_t mask = set->capacity - 1;
  size_t slot = ptr_slot(key, mask);
  while (set->keys[slot] != NULL) {
    if (set->keys[slot] == key) {
      return false;
    }
    slot = (slot + 1) & mask;
  }
  set->keys[slot] = key;
  set->count++;
  return true;
}

static void ptrset_release(Parser * p, PtrSet * set) {
  if (set->keys != NULL) {
    p->allocator->free_fn(p->allocator->ctx, (void *)set->keys);
  }
  memset(set, 0, sizeof(*set));
}

// Empties a set for the next use, without a cost out of proportion to what it
// held: a big one is released, a small one is wiped.
static void ptrset_clear(Parser * p, PtrSet * set) {
  if (set->capacity > 64) {
    ptrset_release(p, set);
    return;
  }
  if (set->keys != NULL) {
    memset((void *)set->keys, 0, set->capacity * sizeof(*set->keys));
  }
  set->count = 0;
}

void module_free(Parser * p) {
  ptrset_release(p, &p->export_names);
  ptrset_release(p, &p->attribute_keys);
  if (p->export_refs != NULL) {
    p->allocator->free_fn(p->allocator->ctx, p->export_refs);
    p->export_refs = NULL;
  }
}

/* ---- Locations ---------------------------------------------------------------- */

static GLECMA_Location location_of(const Parser * p, size_t offset) {
  GLECMA_Location at = {0, 0, 0};
  (void)glecma_source_location(p->source, offset, &at);
  return at;
}

/* ---- Names ------------------------------------------------------------------------ */

// A ModuleExportName, an import attribute's key or a module specifier as read.
typedef struct ModuleName {
  GLECMA_Token token;
  GLECMA_BText text; ///< Interned.
  GLECMA_BNode node;
  bool is_string;
} ModuleName;

// Whether the units hold a surrogate that is not half of a pair
// (IsStringWellFormedUnicode, clause 6.1.4).
static bool well_formed(const GLECMA_BText * text) {
  for (size_t i = 0; i < text->length; i++) {
    uint16_t u = text->units[i];
    if (u >= 0xD800u && u <= 0xDBFFu) {
      if (i + 1 < text->length && text->units[i + 1] >= 0xDC00u &&
          text->units[i + 1] <= 0xDFFFu) {
        i++;
        continue;
      }
      return false;
    }
    if (u >= 0xDC00u && u <= 0xDFFFu) {
      return false;
    }
  }
  return true;
}

// An IdentifierName or a StringLiteral, as a node. `export_name` is a
// ModuleExportName, whose string must be well formed (clause 16.2.1.1); an
// attribute's key is not.
static ModuleName read_name(Parser * p, bool export_name) {
  ModuleName name;
  memset(&name, 0, sizeof(name));
  name.token = p->tok;
  const size_t start = p->tok.start.offset;
  const size_t end = p->tok.end.offset;
  if (p->tok.kind == GLECMA_TOK_NAME) {
    parser_text(p, &p->tok, GLECMA_VALUE_COOKED, true, &name.text);
    name.node = parser_make_text(p, GLECMA_NODE_IDENTIFIER, 0, start, end,
        &name.text, NULL);
  } else if (p->tok.kind == GLECMA_TOK_STRING) {
    name.is_string = true;
    parser_text(p, &p->tok, GLECMA_VALUE_COOKED, true, &name.text);
    if (export_name && !well_formed(&name.text)) {
      parser_fail_token(p, GLECMA_RULE_MODULE_NAME_ILL_FORMED);
    }
    name.node = parser_make_text(p, GLECMA_NODE_STRING_LITERAL, 0, start, end,
        &name.text, NULL);
  } else {
    parser_unexpected(p);
  }
  parser_next(p);
  return name;
}

// The module specifier or attribute value: a StringLiteral node.
static GLECMA_BNode read_string(Parser * p) {
  if (p->tok.kind != GLECMA_TOK_STRING) {
    parser_unexpected(p);
  }
  GLECMA_BText value;
  parser_text(p, &p->tok, GLECMA_VALUE_COOKED, false, &value);
  GLECMA_BNode node = parser_make_text(p, GLECMA_NODE_STRING_LITERAL, 0,
      p->tok.start.offset, p->tok.end.offset, &value, NULL);
  parser_next(p);
  return node;
}

// `from` and `as` are words of this grammar only: an escaped spelling is not
// them.
static void expect_word(Parser * p, GLECMA_Keyword keyword) {
  if (!parser_is_kw(p, keyword)) {
    parser_unexpected(p);
  }
  parser_next(p);
}

/* ---- Exported names ----------------------------------------------------------------- */

static void add_export(Parser * p, const uint16_t * name,
    const GLECMA_Location * at) {
  if (!ptrset_add(p, &p->export_names, name)) {
    parser_fail(p, GLECMA_RULE_MODULE_DUPLICATE_EXPORT, at);
  }
}

// A local name an `export { a }` refers to, to be found at the end. `rule` is
// what makes it unfit to be one (a string, a reserved word), or none: whether
// it matters depends on a `from` clause that comes after.
static void add_export_ref(Parser * p, const GLECMA_BText * name, size_t offset,
    uint32_t rule) {
  if (p->export_ref_len == p->export_ref_cap) {
    size_t capacity = p->export_ref_cap == 0 ? 16 : p->export_ref_cap * 2;
    if (capacity > SIZE_MAX / sizeof(ModuleRef)) {
      parser_fail_result(p, GLECMA_ERR_OOM);
    }
    ModuleRef * grown = (ModuleRef *)p->allocator->realloc_fn(p->allocator->ctx,
        p->export_refs, capacity * sizeof(ModuleRef));
    if (grown == NULL) {
      parser_fail_result(p, GLECMA_ERR_OOM);
    }
    p->export_refs = grown;
    p->export_ref_cap = capacity;
  }
  p->export_refs[p->export_ref_len].name = name->units;
  p->export_refs[p->export_ref_len].offset = offset;
  p->export_refs[p->export_ref_len].rule = rule;
  p->export_ref_len++;
}

// The names a declaration just read bound in the module's scope are exported
// (`export var a, b`, `export function f() {}`): they are the scope records
// added since `first`.
static void export_declared(Parser * p, size_t first) {
  for (size_t i = first; i < p->rec_len; i++) {
    const ScopeRec * rec = &p->recs[i];
    if (rec->kind == SR_VAR || rec->kind == SR_LEX || rec->kind == SR_FUNC ||
        rec->kind == SR_FUNC_OTHER) {
      GLECMA_Location at = location_of(p, rec->offset);
      add_export(p, rec->name, &at);
    }
  }
}

void module_finish(Parser * p) {
  if (p->export_ref_len == 0) {
    return;
  }
  // Every name the module scope declares: var, let, const, class, function
  // and import names (clause 16.2.1.1).
  // The set lives in the parser's slot for attribute keys, which is empty
  // between declarations, so that it is released with the parser whichever
  // way the parse ends.
  for (size_t i = p->frames[0].first; i < p->rec_len; i++) {
    (void)ptrset_add(p, &p->attribute_keys, p->recs[i].name);
  }
  size_t missing = SIZE_MAX;
  const PtrSet * names = &p->attribute_keys;
  for (size_t i = 0; i < p->export_ref_len && missing == SIZE_MAX; i++) {
    const uint16_t * name = p->export_refs[i].name;
    bool found = false;
    if (names->keys != NULL) {
      size_t mask = names->capacity - 1;
      size_t slot = ptr_slot(name, mask);
      while (names->keys[slot] != NULL) {
        if (names->keys[slot] == name) {
          found = true;
          break;
        }
        slot = (slot + 1) & mask;
      }
    }
    if (!found) {
      missing = i;
    }
  }
  ptrset_release(p, &p->attribute_keys);
  if (missing != SIZE_MAX) {
    GLECMA_Location at = location_of(p, p->export_refs[missing].offset);
    parser_fail(p, GLECMA_RULE_MODULE_EXPORT_UNDECLARED, &at);
  }
}

/* ---- Import attributes (clause 16.2.2) ------------------------------------------------ */

static GLECMA_BNode import_attributes(Parser * p) {
  const size_t start = p->tok.start.offset;
  const size_t mark = p->kids_len;
  parser_next(p); // `with`
  parser_expect(p, GLECMA_TOK_LBRACE);
  while (p->tok.kind != GLECMA_TOK_RBRACE) {
    const size_t entry = p->kids_len;
    ModuleName key = read_name(p, false);
    if (!ptrset_add(p, &p->attribute_keys, key.text.units)) {
      parser_fail(p, GLECMA_RULE_IMPORT_ATTRIBUTE_DUPLICATE, &key.token.start);
    }
    parser_expect(p, GLECMA_TOK_COLON);
    parser_kid(p, key.node);
    GLECMA_BNode value = read_string(p);
    parser_kid(p, value);
    parser_kid(p, parser_make(p, GLECMA_NODE_IMPORT_ATTRIBUTE, 0, 0,
                       key.token.start.offset, p->prev_end, entry));
    if (p->tok.kind != GLECMA_TOK_RBRACE) {
      parser_expect(p, GLECMA_TOK_COMMA);
    }
  }
  const size_t end = p->tok.end.offset;
  parser_next(p);
  ptrset_clear(p, &p->attribute_keys);
  return parser_make(p, GLECMA_NODE_IMPORT_ATTRIBUTES, 0, 0, start, end, mark);
}

/* ---- Import declarations (clause 16.2.2) ---------------------------------------------- */

// An IdentifierName node for a name already read, for the second slot of a
// specifier that names one thing twice.
static GLECMA_BNode name_again(Parser * p, const GLECMA_Token * token,
    const GLECMA_BText * text) {
  return parser_make_text(p, GLECMA_NODE_IDENTIFIER, 0, token->start.offset,
      token->end.offset, text, NULL);
}

static void import_named(Parser * p) {
  parser_next(p); // `{`
  while (p->tok.kind != GLECMA_TOK_RBRACE) {
    const size_t mark = p->kids_len;
    const size_t start = p->tok.start.offset;
    GLECMA_Token after;
    if (p->tok.kind == GLECMA_TOK_NAME && parser_peek(p, &after) &&
        !token_is_kw(&after, GLECMA_KW_AS)) {
      // `a`: the name is the binding, and so must be a valid one.
      const GLECMA_Token token = p->tok;
      GLECMA_BText text;
      parser_text(p, &p->tok, GLECMA_VALUE_COOKED, true, &text);
      PExpr local = parse_binding_identifier(p, GLECMA_BDECL_IMPORT);
      parser_kid(p, name_again(p, &token, &text));
      parser_kid(p, local.node);
    } else {
      ModuleName imported = read_name(p, true);
      expect_word(p, GLECMA_KW_AS);
      PExpr local = parse_binding_identifier(p, GLECMA_BDECL_IMPORT);
      parser_kid(p, imported.node);
      parser_kid(p, local.node);
    }
    GLECMA_BNode node = parser_make(p, GLECMA_NODE_IMPORT_SPECIFIER, 0, 0,
        start, p->prev_end, mark);
    parser_kid(p, node);
    if (p->tok.kind != GLECMA_TOK_RBRACE) {
      parser_expect(p, GLECMA_TOK_COMMA);
    }
  }
  parser_next(p); // `}`
}

static void import_namespace(Parser * p) {
  const size_t start = p->tok.start.offset;
  const size_t mark = p->kids_len;
  parser_next(p); // `*`
  expect_word(p, GLECMA_KW_AS);
  PExpr local = parse_binding_identifier(p, GLECMA_BDECL_IMPORT);
  parser_kid(p, local.node);
  GLECMA_BNode node = parser_make(p, GLECMA_NODE_IMPORT_NAMESPACE_SPECIFIER, 0,
      0, start, p->prev_end, mark);
  parser_kid(p, node);
}

// Moves the last `count` slots in front of the `specifiers` slots before them:
// the clause comes first in the text and the source after it, and the node
// lists the source first.
static void move_to_front(Parser * p, size_t mark, size_t count) {
  GLECMA_BNode moved[3];
  const size_t total = p->kids_len - mark;
  for (size_t i = 0; i < count; i++) {
    moved[i] = p->kids[p->kids_len - count + i];
  }
  memmove(p->kids + mark + count, p->kids + mark,
      (total - count) * sizeof(GLECMA_BNode));
  for (size_t i = 0; i < count; i++) {
    p->kids[mark + i] = moved[i];
  }
}

static GLECMA_BNode parse_import_declaration(Parser * p) {
  const size_t start = p->tok.start.offset;
  const size_t mark = p->kids_len;
  parser_next(p); // `import`
  if (p->tok.kind != GLECMA_TOK_STRING) {
    // ImportClause FromClause.
    if (p->tok.kind == GLECMA_TOK_NAME) {
      const size_t specifier_start = p->tok.start.offset;
      const size_t inner = p->kids_len;
      PExpr local = parse_binding_identifier(p, GLECMA_BDECL_IMPORT);
      parser_kid(p, local.node);
      parser_kid(p, parser_make(p, GLECMA_NODE_IMPORT_DEFAULT_SPECIFIER, 0, 0,
                         specifier_start, p->prev_end, inner));
      p->kids[inner] = p->kids[p->kids_len - 1];
      p->kids_len = inner + 1;
      if (p->tok.kind == GLECMA_TOK_COMMA) {
        parser_next(p);
        if (p->tok.kind == GLECMA_TOK_STAR) {
          import_namespace(p);
        } else if (p->tok.kind == GLECMA_TOK_LBRACE) {
          import_named(p);
        } else {
          parser_unexpected(p);
        }
      }
    } else if (p->tok.kind == GLECMA_TOK_STAR) {
      import_namespace(p);
    } else if (p->tok.kind == GLECMA_TOK_LBRACE) {
      import_named(p);
    } else {
      parser_unexpected(p);
    }
    expect_word(p, GLECMA_KW_FROM);
  }
  GLECMA_BNode source = read_string(p);
  GLECMA_BNode attributes = NULL;
  if (parser_is_kw(p, GLECMA_KW_WITH)) {
    attributes = import_attributes(p);
  }
  parser_semicolon(p);
  parser_kid(p, source);
  parser_kid(p, attributes);
  move_to_front(p, mark, 2);
  return parser_make(p, GLECMA_NODE_IMPORT_DECLARATION, 0, 0, start,
      p->prev_end, mark);
}

/* ---- Export declarations (clause 16.2.3) ------------------------------------------------ */

// The second slot of a specifier that names one thing twice (`export { a }`
// is `a as a`): a node of its own for the same token, not the first used again.
static GLECMA_BNode same_name_node(Parser * p, const ModuleName * name) {
  return parser_make_text(p,
      name->is_string ? GLECMA_NODE_STRING_LITERAL : GLECMA_NODE_IDENTIFIER, 0,
      name->token.start.offset, name->token.end.offset, &name->text, NULL);
}

static GLECMA_BNode parse_export_named(Parser * p, size_t start) {
  const size_t mark = p->kids_len;
  // The front of the node, a `from` clause and its attributes, is filled in
  // once the list is read.
  parser_kid(p, NULL);
  parser_kid(p, NULL);
  parser_next(p); // `{`
  const size_t first_ref = p->export_ref_len;
  while (p->tok.kind != GLECMA_TOK_RBRACE) {
    const size_t entry_start = p->tok.start.offset;
    const size_t inner = p->kids_len;
    ModuleName local = read_name(p, true);
    ModuleName exported = local;
    parser_kid(p, local.node);
    if (parser_is_kw(p, GLECMA_KW_AS)) {
      parser_next(p);
      exported = read_name(p, true);
      parser_kid(p, exported.node);
    } else {
      parser_kid(p, same_name_node(p, &local));
    }
    GLECMA_BNode node = parser_make(p, GLECMA_NODE_EXPORT_SPECIFIER, 0, 0,
        entry_start, p->prev_end, inner);
    parser_kid(p, node);
    // The name is the module's whether or not a `from` follows.
    add_export(p, exported.text.units, &exported.token.start);
    // The local name is a binding of this module unless a `from` follows: it
    // is then an IdentifierReference (clause 16.2.3.1).
    uint32_t rule = GLECMA_RULE_NONE;
    if (local.is_string) {
      rule = GLECMA_RULE_MODULE_EXPORT_STRING_LOCAL;
    } else {
      rule = glecma_token_identifier_check(&local.token, parser_idf(p));
    }
    add_export_ref(p, &local.text, local.token.start.offset, rule);
    if (p->tok.kind != GLECMA_TOK_RBRACE) {
      parser_expect(p, GLECMA_TOK_COMMA);
    }
  }
  parser_next(p); // `}`
  GLECMA_BNode source = NULL;
  GLECMA_BNode attributes = NULL;
  if (parser_is_kw(p, GLECMA_KW_FROM)) {
    parser_next(p);
    source = read_string(p);
    if (parser_is_kw(p, GLECMA_KW_WITH)) {
      attributes = import_attributes(p);
    }
    // The names of an export with a `from` clause are another module's.
    p->export_ref_len = first_ref;
  } else {
    for (size_t i = first_ref; i < p->export_ref_len; i++) {
      if (p->export_refs[i].rule != GLECMA_RULE_NONE) {
        GLECMA_Location at = location_of(p, p->export_refs[i].offset);
        parser_fail(p, p->export_refs[i].rule, &at);
      }
    }
  }
  parser_semicolon(p);
  p->kids[mark] = source;
  p->kids[mark + 1] = attributes;
  return parser_make(p, GLECMA_NODE_EXPORT_NAMED, 0, 0, start, p->prev_end,
      mark);
}

static GLECMA_BNode parse_export_all(Parser * p, size_t start) {
  const size_t mark = p->kids_len;
  parser_next(p); // `*`
  GLECMA_BNode exported = NULL;
  if (parser_is_kw(p, GLECMA_KW_AS)) {
    parser_next(p);
    ModuleName name = read_name(p, true);
    exported = name.node;
    add_export(p, name.text.units, &name.token.start);
  }
  expect_word(p, GLECMA_KW_FROM);
  GLECMA_BNode source = read_string(p);
  GLECMA_BNode attributes = NULL;
  if (parser_is_kw(p, GLECMA_KW_WITH)) {
    attributes = import_attributes(p);
  }
  parser_semicolon(p);
  parser_kid(p, source);
  parser_kid(p, attributes);
  parser_kid(p, exported);
  return parser_make(p, GLECMA_NODE_EXPORT_ALL, 0, 0, start, p->prev_end, mark);
}

static GLECMA_BNode parse_export_default(Parser * p, size_t start) {
  const size_t mark = p->kids_len;
  const GLECMA_Location at = p->tok.start;
  parser_next(p); // `default`
  const size_t first = p->tok.start.offset;
  GLECMA_BNode value;
  GLECMA_Token after;
  if (parser_is_kw(p, GLECMA_KW_FUNCTION)) {
    value = parse_function_declaration(p, first, false, true, true).node;
  } else if (parser_is_kw(p, GLECMA_KW_ASYNC) && parser_peek(p, &after) &&
      token_is_kw(&after, GLECMA_KW_FUNCTION) &&
      (after.flags & GLECMA_TOKF_NEWLINE_BEFORE) == 0) {
    parser_next(p);
    value = parse_function_declaration(p, first, true, true, true).node;
  } else if (parser_is_kw(p, GLECMA_KW_CLASS)) {
    value = parse_class_declaration(p, true).node;
  } else {
    value = parse_assignment(p, 0).node;
    parser_semicolon(p);
  }
  // Whatever the declaration is called, the module exports it as `default`.
  GLECMA_BText name;
  static const uint16_t default_name[] = {'d', 'e', 'f', 'a', 'u', 'l', 't'};
  p->builder->ops->text(p->builder, default_name, 7, true, &name);
  add_export(p, name.units, &at);
  parser_kid(p, value);
  return parser_make(p, GLECMA_NODE_EXPORT_DEFAULT, 0, 0, start, p->prev_end,
      mark);
}

static GLECMA_BNode parse_export_declaration(Parser * p) {
  const size_t start = p->tok.start.offset;
  parser_next(p); // `export`
  if (p->tok.kind == GLECMA_TOK_STAR) {
    return parse_export_all(p, start);
  }
  if (p->tok.kind == GLECMA_TOK_LBRACE) {
    return parse_export_named(p, start);
  }
  if (parser_is_kw(p, GLECMA_KW_DEFAULT)) {
    return parse_export_default(p, start);
  }
  // A VariableStatement or a Declaration: what it binds in the module's scope
  // is exported.
  const size_t mark = p->kids_len;
  const size_t first_record = p->rec_len;
  const size_t declaration_start = p->tok.start.offset;
  GLECMA_BNode declaration;
  GLECMA_Token after;
  if (parser_is_kw(p, GLECMA_KW_VAR)) {
    parser_next(p);
    declaration = parse_variable_declaration(p, GLECMA_BDECL_VAR, false,
        declaration_start, NULL);
  } else if (parser_is_kw(p, GLECMA_KW_CONST)) {
    parser_next(p);
    declaration = parse_variable_declaration(p, GLECMA_BDECL_CONST, false,
        declaration_start, NULL);
  } else if (parser_is_kw(p, GLECMA_KW_LET)) {
    parser_next(p);
    declaration = parse_variable_declaration(p, GLECMA_BDECL_LET, false,
        declaration_start, NULL);
  } else if (parser_is_kw(p, GLECMA_KW_FUNCTION)) {
    declaration = parse_function_declaration(p, declaration_start, false, true,
        false).node;
  } else if (parser_is_kw(p, GLECMA_KW_ASYNC) && parser_peek(p, &after) &&
      token_is_kw(&after, GLECMA_KW_FUNCTION) &&
      (after.flags & GLECMA_TOKF_NEWLINE_BEFORE) == 0) {
    parser_next(p);
    declaration = parse_function_declaration(p, declaration_start, true, true,
        false).node;
  } else if (parser_is_kw(p, GLECMA_KW_CLASS)) {
    declaration = parse_class_declaration(p, false).node;
  } else {
    parser_unexpected(p);
  }
  export_declared(p, first_record);
  parser_kid(p, declaration);
  return parser_make(p, GLECMA_NODE_EXPORT_DECLARATION, 0, 0, start,
      p->prev_end, mark);
}

/* ---- The items --------------------------------------------------------------------------- */

static GLECMA_BNode parse_module_item(Parser * p) {
  if (parser_is_kw(p, GLECMA_KW_IMPORT)) {
    GLECMA_Token after;
    // `import(` and `import.meta` begin an expression statement.
    if (parser_peek(p, &after) && after.kind != GLECMA_TOK_LPAREN &&
        after.kind != GLECMA_TOK_DOT) {
      return parse_import_declaration(p);
    }
  } else if (parser_is_kw(p, GLECMA_KW_EXPORT)) {
    return parse_export_declaration(p);
  }
  return parse_statement_list_item(p);
}

void parse_module_items(Parser * p) {
  while (p->tok.kind != GLECMA_TOK_EOF) {
    p->prologue_item = false;
    parser_kid(p, parse_module_item(p));
  }
}

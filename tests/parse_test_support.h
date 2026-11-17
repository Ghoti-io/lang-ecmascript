//
// SPDX-License-Identifier: LGPL-3.0-only
//
// Copyright (C) 2026 Corey Pennycuff
//
// This file is part of Ghoti.io Lang-ecmascript.
//
// Ghoti.io Lang-ecmascript is free software: you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public License version 3
// as published by the Free Software Foundation.
//
// Ghoti.io Lang-ecmascript is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser
// General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

// What the parser's tests share: a source and a parse in one call, and a
// rendering of a tree made only through the public walker, so that a test
// states the tree it expects as a string and asserts nothing about internals.

#ifndef GHOTI_IO_GLECMA_TESTS_PARSE_TEST_SUPPORT_H
#define GHOTI_IO_GLECMA_TESTS_PARSE_TEST_SUPPORT_H

#include <ghoti.io/lang-ecmascript/lang-ecmascript.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "parse/parse_internal.h"

namespace glecma_test {

// A UTF-16 text as UTF-8, for printing.
inline std::string Narrow(const uint16_t * units, size_t length) {
  std::string out;
  for (size_t i = 0; i < length; i++) {
    uint32_t u = units[i];
    if (u >= 0xD800 && u <= 0xDBFF && i + 1 < length &&
        units[i + 1] >= 0xDC00 && units[i + 1] <= 0xDFFF) {
      u = 0x10000 + ((u - 0xD800) << 10) + (units[i + 1] - 0xDC00);
      i++;
    }
    if (u == '"' || u == '\\') {
      out += '\\';
      out += static_cast<char>(u);
    } else if (u >= 0x20 && u < 0x7F) {
      out += static_cast<char>(u);
    } else {
      char buffer[16];
      std::snprintf(buffer, sizeof buffer, "\\u{%X}", u);
      out += buffer;
    }
  }
  return out;
}

inline std::string ShortKind(uint32_t kind) {
  std::string name = glecma_node_kind_name(kind);
  for (const char * suffix : {"Expression", "Statement"}) {
    size_t n = std::strlen(suffix);
    if (name.size() > n && name.compare(name.size() - n, n, suffix) == 0) {
      return name.substr(0, name.size() - n);
    }
  }
  return name;
}

inline std::string FlagNames(uint32_t flags) {
  static const struct { uint32_t flag; const char * name; } names[] = {
    {GLECMA_NODEF_COMPUTED, "computed"}, {GLECMA_NODEF_SHORTHAND, "shorthand"},
    {GLECMA_NODEF_STATIC, "static"}, {GLECMA_NODEF_ASYNC, "async"},
    {GLECMA_NODEF_GENERATOR, "gen"}, {GLECMA_NODEF_PREFIX, "prefix"},
    {GLECMA_NODEF_OPTIONAL, "opt"}, {GLECMA_NODEF_DELEGATE, "delegate"},
    {GLECMA_NODEF_AWAIT, "await"}, {GLECMA_NODEF_DIRECTIVE, "directive"},
    {GLECMA_NODEF_EXPRESSION_BODY, "exprbody"}, {GLECMA_NODEF_STRICT, "strict"},
    {GLECMA_NODEF_GETTER, "get"}, {GLECMA_NODEF_SETTER, "set"},
    {GLECMA_NODEF_CONSTRUCTOR, "ctor"}, {GLECMA_NODEF_DEFAULT, "default"},
    {GLECMA_NODEF_VAR, "var"}, {GLECMA_NODEF_LET, "let"},
    {GLECMA_NODEF_CONST, "const"}, {GLECMA_NODEF_TRUE, "true"},
    {GLECMA_NODEF_LEGACY_OCTAL, "octal"},
    {GLECMA_NODEF_NON_OCTAL_DECIMAL, "nonoctal"},
    {GLECMA_NODEF_COOKED_UNDEFINED, "nocooked"},
    {GLECMA_NODEF_METHOD, "method"}, {GLECMA_NODEF_DERIVED, "derived"},
  };
  std::string out;
  for (const auto & n : names) {
    if ((flags & n.flag) != 0) {
      out += "/";
      out += n.name;
    }
  }
  return out;
}

// The tree below a node as `(Kind/flags op "text" child ...)`; an empty slot
// is `_`. With `ranges`, each node also carries `[start,end)`.
inline void Render(const GLECMA_Node * node, bool ranges, std::string * out) {
  if (node == nullptr) {
    *out += "_";
    return;
  }
  *out += "(";
  *out += ShortKind(glecma_node_kind(node));
  *out += FlagNames(glecma_node_flags(node));
  uint32_t op = glecma_node_operator(node);
  if (op != GLECMA_OP_NONE ||
      glecma_node_kind(node) == GLECMA_NODE_ASSIGNMENT_EXPRESSION) {
    *out += " ";
    *out += glecma_operator_text(op);
    if (glecma_node_kind(node) == GLECMA_NODE_ASSIGNMENT_EXPRESSION) {
      *out += "=";
    }
  }
  for (size_t i = 0; i < 2; i++) {
    const uint16_t * units = nullptr;
    size_t length = 0;
    if (glecma_node_text(node, i, &units, &length)) {
      uint32_t kind = glecma_node_kind(node);
      bool bare = kind == GLECMA_NODE_IDENTIFIER ||
          kind == GLECMA_NODE_PRIVATE_NAME || kind == GLECMA_NODE_INTRINSIC_NAME;
      *out += bare ? " " : " \"";
      *out += Narrow(units, length);
      if (!bare) {
        *out += "\"";
      }
    }
  }
  if (ranges) {
    *out += " [" + std::to_string(glecma_node_start(node)) + "," +
        std::to_string(glecma_node_end(node)) + ")";
  }
  for (size_t i = 0; i < glecma_node_child_count(node); i++) {
    *out += " ";
    Render(glecma_node_child(node, i), ranges, out);
  }
  *out += ")";
}

struct Parsed {
  GLECMA_Source * source = nullptr;
  GLECMA_Tree * tree = nullptr;
  GLECMA_Result result = GLECMA_OK;
  GLECMA_Failure failure = GLECMA_FAILURE_INIT;

  Parsed() = default;
  Parsed(const Parsed &) = delete;
  Parsed & operator=(const Parsed &) = delete;
  Parsed(Parsed && other) noexcept
      : source(other.source), tree(other.tree), result(other.result),
        failure(other.failure) {
    other.source = nullptr;
    other.tree = nullptr;
  }
  ~Parsed() {
    glecma_tree_destroy(tree);
    glecma_source_release(source);
  }

  // The statements of the Script, one per top-level child, joined.
  std::string Body(bool ranges = false) const {
    std::string out;
    const GLECMA_Node * root = glecma_tree_root(tree);
    for (size_t i = 0; root != nullptr && i < glecma_node_child_count(root); i++) {
      if (i > 0) {
        out += " ";
      }
      Render(glecma_node_child(root, i), ranges, &out);
    }
    return out;
  }
  std::string Whole(bool ranges = false) const {
    std::string out;
    Render(glecma_tree_root(tree), ranges, &out);
    return out;
  }
};

// Parses `text` (UTF-8) as a Script, strict when asked, through the public
// entry or, with `internal`, the internal entry.
inline Parsed Parse(const std::string & text, bool strict = false,
    uint32_t internal = 0, const GLECMA_Limits * limits = nullptr) {
  Parsed p;
  GLECMA_Result made = glecma_source_create_utf8(text.data(), text.size(),
      nullptr, nullptr, &p.failure, &p.source);
  if (made != GLECMA_OK) {
    p.result = made;
    return p;
  }
  GLECMA_ParseOptions options = GLECMA_PARSE_OPTIONS_INIT;
  options.limits = limits;
  options.context = strict ? GLECMA_CONTEXT_STRICT : 0u;
  p.result = glecma_parse_internal(p.source, &options, nullptr, internal,
      &p.failure, &p.tree);
  return p;
}

}  // namespace glecma_test

#endif  // GHOTI_IO_GLECMA_TESTS_PARSE_TEST_SUPPORT_H

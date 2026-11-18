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

// The dynamic-function goal (clause 20.2.1.1.1): the parameters read alone, the
// body read alone, and the text they make read as the kind's function
// expression. Each refusal is paired with the control that is accepted.

#include <gtest/gtest.h>

#include "../parse_test_support.h"
#include "../test_helpers.h"

#include <string>
#include <vector>

namespace {

using glecma_test::Parsed;

GLECMA_Source * Utf8(const std::string & text) {
  GLECMA_Source * source = nullptr;
  EXPECT_EQ(glecma_source_create_utf8(text.data(), text.size(), nullptr, nullptr,
                nullptr, &source), GLECMA_OK);
  return source;
}

GLECMA_Source * Utf16(const std::u16string & text) {
  GLECMA_Source * source = nullptr;
  EXPECT_EQ(glecma_source_create_utf16(reinterpret_cast<const uint16_t *>(text.data()),
                text.size(), nullptr, nullptr, nullptr, &source), GLECMA_OK);
  return source;
}

// What a dynamic function parse returns, with the two sources it was given.
struct Dynamic {
  GLECMA_Tree * tree = nullptr;
  GLECMA_Result result = GLECMA_OK;
  GLECMA_Failure failure = GLECMA_FAILURE_INIT;
  Dynamic() = default;
  Dynamic(const Dynamic &) = delete;
  Dynamic & operator=(const Dynamic &) = delete;
  ~Dynamic() { glecma_tree_destroy(tree); }
  std::string Whole(bool ranges = false) const {
    std::string out;
    glecma_test::Render(glecma_tree_root(tree), ranges, &out);
    return out;
  }
};

std::unique_ptr<Dynamic> Make(uint32_t kind, const std::string & parameters,
    const std::string & body, const GLECMA_ParseOptions * options = nullptr,
    const GLECMA_Allocator * allocator = nullptr) {
  auto out = std::make_unique<Dynamic>();
  GLECMA_Source * p = Utf8(parameters);
  GLECMA_Source * b = Utf8(body);
  GLECMA_DynamicFunction function = GLECMA_DYNAMIC_FUNCTION_INIT;
  function.kind = kind;
  function.parameters = p;
  function.body = b;
  out->result = glecma_parse_dynamic_function(&function, options, allocator,
      &out->failure, &out->tree);
  glecma_source_release(p);
  glecma_source_release(b);
  return out;
}

const uint32_t kKinds[] = {GLECMA_DYNFN_FUNCTION, GLECMA_DYNFN_GENERATOR,
    GLECMA_DYNFN_ASYNC, GLECMA_DYNFN_ASYNC_GENERATOR};

bool Accepts(uint32_t kind, const std::string & parameters, const std::string & body) {
  return Make(kind, parameters, body)->result == GLECMA_OK;
}

}  // namespace

TEST(DynamicFunction, AFailureIsPlacedInThePieceTheCallerGaveInItsOwnUnits) {
  // An error at the end of the text is at the end of the piece, not past it.
  auto end = Make(GLECMA_DYNFN_FUNCTION, "", "x +");
  ASSERT_EQ(end->result, GLECMA_ERR_SYNTAX);
  EXPECT_EQ(end->failure.location.offset, 3u);
  EXPECT_EQ(end->failure.location.line, 1u);
  EXPECT_EQ(end->failure.location.column, 3u);
  auto empty_end = Make(GLECMA_DYNFN_FUNCTION, "a,,", "");
  ASSERT_EQ(empty_end->result, GLECMA_ERR_SYNTAX);
  EXPECT_LE(empty_end->failure.location.offset, 3u);
  auto lines = Make(GLECMA_DYNFN_FUNCTION, "", "x;\ny +");
  ASSERT_EQ(lines->result, GLECMA_ERR_SYNTAX);
  EXPECT_EQ(lines->failure.location.offset, 6u);
  EXPECT_EQ(lines->failure.location.line, 2u);
  // A UTF-8 piece's offsets are its own bytes, and its columns UTF-16 units: an astral character
  // is four bytes and two units.
  const std::string body = "'\xF0\x9F\x98\x80' x";  // '😀' x
  auto astral = Make(GLECMA_DYNFN_FUNCTION, "", body);
  ASSERT_EQ(astral->result, GLECMA_ERR_SYNTAX);
  EXPECT_EQ(astral->failure.location.offset, 7u);
  EXPECT_EQ(astral->failure.location.column, 5u);
  auto parameters = Make(GLECMA_DYNFN_FUNCTION, "\xF0\x9D\x92\x9C \xF0\x9D\x92\x9C", "");  // two names of U+1D49C
  ASSERT_EQ(parameters->result, GLECMA_ERR_SYNTAX);
  EXPECT_EQ(parameters->failure.location.offset, 5u);
  EXPECT_EQ(parameters->failure.location.column, 3u);
  // A UTF-16 piece is its own text.
  GLECMA_Source * wide = Utf16(u"'\xD83D\xDE00' x");
  GLECMA_DynamicFunction function = GLECMA_DYNAMIC_FUNCTION_INIT;
  function.body = wide;
  GLECMA_Tree * tree = nullptr;
  GLECMA_Failure failure = GLECMA_FAILURE_INIT;
  ASSERT_EQ(glecma_parse_dynamic_function(&function, nullptr, nullptr, &failure, &tree), GLECMA_ERR_SYNTAX);
  EXPECT_EQ(failure.location.offset, 5u);
  glecma_source_release(wide);
}

TEST(DynamicFunction, AnAllocatorWithoutAFunctionIsRefusedNotCalled) {
  GLECMA_Allocator broken = *glecma_allocator_default();
  GLECMA_Source * body = Utf8("return 1;");
  GLECMA_DynamicFunction function = GLECMA_DYNAMIC_FUNCTION_INIT;
  function.body = body;
  GLECMA_Tree * tree = nullptr;
  GLECMA_Failure failure = GLECMA_FAILURE_INIT;
  for (int hole = 0; hole < 4; hole++) {
    GLECMA_Allocator a = broken;
    switch (hole) {
      case 0: a.malloc_fn = nullptr; break;
      case 1: a.calloc_fn = nullptr; break;
      case 2: a.realloc_fn = nullptr; break;
      default: a.free_fn = nullptr; break;
    }
    EXPECT_EQ(glecma_parse_dynamic_function(&function, nullptr, &a, &failure, &tree), GLECMA_ERR_INVALID) << hole;
    EXPECT_EQ(tree, nullptr);
  }
  glecma_source_release(body);
}

TEST(DynamicFunction, TheTreeIsTheFunctionExpressionOfTheAssembledText) {
  // "function anonymous(a,b\n) {\nx\n}": the text CreateDynamicFunction assembles.
  auto made = Make(GLECMA_DYNFN_FUNCTION, "a,b", "x");
  ASSERT_EQ(made->result, GLECMA_OK) << made->failure.message;
  EXPECT_EQ(glecma_tree_goal(made->tree), GLECMA_PARSE_DYNAMIC_FUNCTION);
  EXPECT_FALSE(glecma_tree_strict(made->tree));
  EXPECT_EQ(glecma_source_units(glecma_tree_source(made->tree)), 30u);
  const GLECMA_Node * root = glecma_tree_root(made->tree);
  EXPECT_EQ(glecma_node_kind(root), GLECMA_NODE_FUNCTION_EXPRESSION);
  EXPECT_EQ(glecma_node_start(root), 0u);
  EXPECT_EQ(glecma_node_end(root), 30u);
  // The name the text gives it, the parameters between the parentheses, and the body's braces.
  const GLECMA_Node * id = glecma_node_child(root, 0);
  ASSERT_NE(id, nullptr);
  EXPECT_EQ(glecma_node_start(id), 9u);
  EXPECT_EQ(glecma_node_end(id), 18u);
  const GLECMA_Node * params = glecma_node_child(root, 1);
  EXPECT_EQ(glecma_node_start(params), 18u);
  EXPECT_EQ(glecma_node_end(params), 24u);
  EXPECT_EQ(glecma_node_child_count(params), 2u);
  const GLECMA_Node * body = glecma_node_child(root, 2);
  EXPECT_EQ(glecma_node_kind(body), GLECMA_NODE_FUNCTION_BODY);
  EXPECT_EQ(glecma_node_start(body), 25u);
  EXPECT_EQ(glecma_node_end(body), 30u);
}

TEST(DynamicFunction, EachKindIsTheFunctionOfItsPrefix) {
  struct Row { uint32_t kind; uint32_t flags; size_t prefix; };
  const Row rows[] = {
    {GLECMA_DYNFN_FUNCTION, 0, 8},
    {GLECMA_DYNFN_GENERATOR, GLECMA_NODEF_GENERATOR, 9},
    {GLECMA_DYNFN_ASYNC, GLECMA_NODEF_ASYNC, 14},
    {GLECMA_DYNFN_ASYNC_GENERATOR, GLECMA_NODEF_ASYNC | GLECMA_NODEF_GENERATOR, 15},
  };
  for (const Row & row : rows) {
    auto made = Make(row.kind, "", "");
    ASSERT_EQ(made->result, GLECMA_OK) << row.kind;
    const GLECMA_Node * root = glecma_tree_root(made->tree);
    EXPECT_EQ(glecma_node_kind(root), GLECMA_NODE_FUNCTION_EXPRESSION);
    EXPECT_EQ(glecma_node_flags(root) & (GLECMA_NODEF_ASYNC | GLECMA_NODEF_GENERATOR), row.flags);
    // prefix, " anonymous(", "\n) {", "\n\n", "}"
    EXPECT_EQ(glecma_node_end(root), row.prefix + 11 + 4 + 2 + 1) << row.kind;
    EXPECT_EQ(glecma_node_start(glecma_node_child(root, 0)), row.prefix + 1);
  }
  // No text at all is the empty function.
  GLECMA_DynamicFunction nothing = GLECMA_DYNAMIC_FUNCTION_INIT;
  GLECMA_Tree * tree = nullptr;
  ASSERT_EQ(glecma_parse_dynamic_function(&nothing, nullptr, nullptr, nullptr, &tree), GLECMA_OK);
  EXPECT_EQ(glecma_node_end(glecma_tree_root(tree)), 8u + 11u + 4u + 2u + 1u);
  glecma_tree_destroy(tree);
}

TEST(DynamicFunction, EachPieceIsValidAloneAsTheAlgorithmRequires) {
  for (uint32_t kind : kKinds) {
    // The assembled text of each of these is a function expression; no one of them is a function.
    EXPECT_FALSE(Accepts(kind, "/*", "*/ ) {"));        // new Function("/*", "*/ ) {")
    EXPECT_FALSE(Accepts(kind, "a /*", "*/ b"));
    EXPECT_FALSE(Accepts(kind, "a", ") {"));
    EXPECT_FALSE(Accepts(kind, "a) { return 1; } function g(", ""));
    EXPECT_FALSE(Accepts(kind, "", "} function g() {"));
    EXPECT_FALSE(Accepts(kind, "a", "} {"));
    EXPECT_FALSE(Accepts(kind, "", "/*"));
    EXPECT_FALSE(Accepts(kind, "", "`"));
    EXPECT_FALSE(Accepts(kind, "a = '", "'"));
    // The controls: what is valid alone and joined.
    EXPECT_TRUE(Accepts(kind, "a //", "return a"));   // a line comment ends at the line feed the text adds
    EXPECT_TRUE(Accepts(kind, "a /* c */, b", "/* c */ return a; // d"));
    EXPECT_TRUE(Accepts(kind, "a, b,", ""));          // a trailing comma
    EXPECT_TRUE(Accepts(kind, "{a, b: [c]} = {}, ...d", "{ return [a, b, c, d]; }"));
    // Each piece is read as itself: the failure is the piece's, and says which.
    auto parameters = Make(kind, "a b", "");
    ASSERT_EQ(parameters->result, GLECMA_ERR_SYNTAX);
    EXPECT_EQ(parameters->failure.location.offset, 2u);
    auto body = Make(kind, "a", "x y");
    ASSERT_EQ(body->result, GLECMA_ERR_SYNTAX);
    EXPECT_EQ(body->failure.location.offset, 2u);  // in the body, not counting the line feed the text puts first
    EXPECT_EQ(body->failure.location.line, 1u);
    auto later = Make(kind, "a", "x\n\ny z");
    ASSERT_EQ(later->result, GLECMA_ERR_SYNTAX);
    EXPECT_EQ(later->failure.location.offset, 5u);
    EXPECT_EQ(later->failure.location.line, 3u);
    EXPECT_EQ(later->failure.location.column, 2u);
  }
}

TEST(DynamicFunction, TheWholeHasEveryEarlyErrorOfBoth) {
  for (uint32_t kind : kKinds) {
    // Duplicate parameters: strict code and a non-simple list forbid them; sloppy simple code does not.
    EXPECT_TRUE(Accepts(kind, "a, a", ""));
    auto strict = Make(kind, "a, a", "'use strict';");
    ASSERT_EQ(strict->result, GLECMA_ERR_SYNTAX);
    EXPECT_EQ(strict->failure.rule, static_cast<uint32_t>(GLECMA_RULE_DUPLICATE_PARAMETER));
    auto non_simple = Make(kind, "a, a = 1", "");
    ASSERT_EQ(non_simple->result, GLECMA_ERR_SYNTAX);
    EXPECT_EQ(non_simple->failure.rule, static_cast<uint32_t>(GLECMA_RULE_DUPLICATE_PARAMETER));
    // A directive over parameters that are not a simple list.
    auto directive = Make(kind, "a = 1", "'use strict';");
    ASSERT_EQ(directive->result, GLECMA_ERR_SYNTAX);
    EXPECT_EQ(directive->failure.rule, static_cast<uint32_t>(GLECMA_RULE_USE_STRICT_NON_SIMPLE_PARAMETERS));
    EXPECT_TRUE(Accepts(kind, "a", "'use strict';"));
    // A body's lexical name against a parameter; a strict name in the parameters.
    auto lexical = Make(kind, "a", "let a;");
    ASSERT_EQ(lexical->result, GLECMA_ERR_SYNTAX);
    EXPECT_EQ(lexical->failure.rule, static_cast<uint32_t>(GLECMA_RULE_FUNCTION_REDECLARATION));
    EXPECT_TRUE(Accepts(kind, "a", "var a;"));
    auto retroactive = Make(kind, "eval", "'use strict';");
    ASSERT_EQ(retroactive->result, GLECMA_ERR_SYNTAX);
    EXPECT_EQ(retroactive->failure.rule, static_cast<uint32_t>(GLECMA_RULE_BINDING_EVAL_ARGUMENTS));
    EXPECT_TRUE(Accepts(kind, "eval", ""));
    // The strictness the body gives is the tree's.
    auto made = Make(kind, "a", "'use strict';");
    ASSERT_EQ(made->result, GLECMA_OK);
    EXPECT_TRUE(glecma_tree_strict(made->tree));
    EXPECT_TRUE(glecma_node_flags(glecma_node_child(glecma_tree_root(made->tree), 2)) & GLECMA_NODEF_STRICT);
  }
}

TEST(DynamicFunction, EachKindReadsItsOwnYieldAndAwait) {
  // `yield` is an operator in a generator's body and a name elsewhere; `await` the same for async.
  for (uint32_t kind : kKinds) {
    const bool generator = kind == GLECMA_DYNFN_GENERATOR || kind == GLECMA_DYNFN_ASYNC_GENERATOR;
    const bool async = kind == GLECMA_DYNFN_ASYNC || kind == GLECMA_DYNFN_ASYNC_GENERATOR;
    EXPECT_EQ(Accepts(kind, "", "yield 1;"), generator) << kind;
    EXPECT_EQ(Accepts(kind, "", "await 1;"), async) << kind;
    EXPECT_EQ(Accepts(kind, "", "var yield; yield: 1;"), !generator) << kind;
    EXPECT_EQ(Accepts(kind, "", "var await; await: 1;"), !async) << kind;
    // In the parameters neither is an operator, and a name only where the kind allows.
    EXPECT_NE(Make(kind, generator ? "a = yield" : "a = await 1", "")->result, GLECMA_OK) << kind;
    EXPECT_EQ(Accepts(kind, "yield", ""), !generator) << kind;
    EXPECT_EQ(Accepts(kind, "await", ""), !async) << kind;
  }
  auto yield_param = Make(GLECMA_DYNFN_GENERATOR, "a = yield", "");
  EXPECT_EQ(yield_param->failure.rule, static_cast<uint32_t>(GLECMA_RULE_YIELD_IN_PARAMETERS));
  auto await_param = Make(GLECMA_DYNFN_ASYNC, "a = await 1", "");
  EXPECT_EQ(await_param->failure.rule, static_cast<uint32_t>(GLECMA_RULE_AWAIT_IN_PARAMETERS));
  EXPECT_TRUE(Accepts(GLECMA_DYNFN_FUNCTION, "a = yield, b = await", ""));
}

TEST(DynamicFunction, ItIsAFunctionAndNothingMore) {
  for (uint32_t kind : kKinds) {
    EXPECT_TRUE(Accepts(kind, "a = new.target", "return new.target;"));
    EXPECT_TRUE(Accepts(kind, "", "return; arguments; this; import('m');"));
    for (const char * body : {"super.x;", "super();", "import.meta;", "import a from 'm';",
             "export var a;", "break;", "continue;", "x: { break y; }", "a: a: ;", "#a in b;",
             "class C { m() { this.#b; } }"}) {
      EXPECT_FALSE(Accepts(kind, "", body)) << kind << ": " << body;
    }
    EXPECT_FALSE(Accepts(kind, "a = super.x", ""));
    EXPECT_FALSE(Accepts(kind, "a = #b in c", ""));
    EXPECT_TRUE(Accepts(kind, "", "x: { break x; } with (a) b;"));
  }
}

TEST(DynamicFunction, TextWithAnyCodeUnitCanBeGiven) {
  // A lone surrogate in a string, from UTF-16, and astral characters from UTF-8; offsets are code units.
  GLECMA_Source * body = Utf16(u"return '\xD800' + \"\xDC00\";");
  GLECMA_Source * params = Utf16(u"a = '\xD800'");
  GLECMA_DynamicFunction function = GLECMA_DYNAMIC_FUNCTION_INIT;
  function.parameters = params;
  function.body = body;
  GLECMA_Tree * tree = nullptr;
  GLECMA_Failure failure = GLECMA_FAILURE_INIT;
  ASSERT_EQ(glecma_parse_dynamic_function(&function, nullptr, nullptr, &failure, &tree), GLECMA_OK)
      << failure.message;
  glecma_tree_destroy(tree);
  glecma_source_release(body);
  glecma_source_release(params);
  // An astral identifier part in UTF-8 text is two units in the tree's source.
  auto astral = Make(GLECMA_DYNFN_FUNCTION, "\xF0\x9D\x92\x9C", "");  // U+1D49C is an ID_Start
  ASSERT_EQ(astral->result, GLECMA_OK);
  const GLECMA_Node * list = glecma_node_child(glecma_tree_root(astral->tree), 1);
  EXPECT_EQ(glecma_node_end(glecma_node_child(list, 0)) - glecma_node_start(glecma_node_child(list, 0)), 2u);
  // A text that is not well-formed UTF-8 never becomes a source, which is the caller's to refuse.
  GLECMA_Source * bad = nullptr;
  EXPECT_EQ(glecma_source_create_utf8("\xC0\x80", 2, nullptr, nullptr, nullptr, &bad), GLECMA_ERR_CORRUPT);
}

TEST(DynamicFunction, WhatTheAssembledTextAcceptsAndTheAlgorithmRefuses) {
  // Over a set of pieces: whatever the algorithm accepts, the plain function expression of the
  // assembled text accepts too, and the converse does not hold (the control above).
  const std::vector<std::pair<std::string, std::string>> pieces = {
    {"", ""}, {"a", "return a"}, {"a, b", "return a + b"}, {"...r", "return r"},
    {"{a} = {}", ""}, {"a = 1", "'use strict'"}, {"a, a", ""}, {"a, a", "'use strict'"},
    {"", "let a; let a;"}, {"a", "let a"}, {"/*", "*/"}, {"a", "}"}, {"a //", ""},
    {"", "yield 1"}, {"", "await 1"}, {"", "return"}, {"", "break"},
  };
  for (const auto & [parameters, body] : pieces) {
    const std::string text = "(function anonymous(" + parameters + "\n) {\n" + body + "\n})";
    const bool expression = glecma_test::Parse(text).result == GLECMA_OK;
    if (Accepts(GLECMA_DYNFN_FUNCTION, parameters, body)) {
      EXPECT_TRUE(expression) << parameters << " | " << body;
    }
  }
}

TEST(DynamicFunction, TheOptionsAndTheInputAreCheckedBeforeAnyWork) {
  glecma_test::CountingAllocator counting;
  GLECMA_Source * p = Utf8("a");
  GLECMA_Source * b = Utf8("return a;");
  GLECMA_DynamicFunction function = GLECMA_DYNAMIC_FUNCTION_INIT;
  function.parameters = p;
  function.body = b;
  GLECMA_Tree * tree = nullptr;
  GLECMA_Failure failure = GLECMA_FAILURE_INIT;
  auto refused = [&](const GLECMA_DynamicFunction * f, const GLECMA_ParseOptions * options) {
    counting.reset();
    GLECMA_Result r = glecma_parse_dynamic_function(f, options, counting.get(), &failure, &tree);
    EXPECT_EQ(counting.calls(), 0u);
    EXPECT_EQ(tree, nullptr);
    return r;
  };
  // A feature this build lacks: refused, named, and nothing was allocated.
  GLECMA_FeatureSet features = GLECMA_FEATURE_SET_INIT;
  ASSERT_EQ(glecma_feature_set_add(&features, "decorators"), GLECMA_OK);
  GLECMA_ParseOptions options = GLECMA_PARSE_OPTIONS_INIT;
  options.features = &features;
  EXPECT_EQ(refused(&function, &options), GLECMA_ERR_UNSUPPORTED);
  EXPECT_STREQ(failure.feature, "decorators");
  // A goal, a strictness or a context the entry has no use for, and a malformed struct.
  options = GLECMA_PARSE_OPTIONS_INIT;
  options.goal = GLECMA_PARSE_MODULE;
  EXPECT_EQ(refused(&function, &options), GLECMA_ERR_INVALID);
  options.goal = GLECMA_PARSE_SCRIPT;
  options.context = GLECMA_CONTEXT_STRICT;
  EXPECT_EQ(refused(&function, &options), GLECMA_ERR_INVALID);
  options.context = 0;
  GLECMA_EvalContext context = GLECMA_EVAL_CONTEXT_INIT;
  options.eval_context = &context;
  EXPECT_EQ(refused(&function, &options), GLECMA_ERR_INVALID);
  options = GLECMA_PARSE_OPTIONS_INIT;
  options.size = 4;
  EXPECT_EQ(refused(&function, &options), GLECMA_ERR_INVALID);
  GLECMA_DynamicFunction bad = function;
  bad.kind = 4;
  EXPECT_EQ(refused(&bad, nullptr), GLECMA_ERR_INVALID);
  bad = function;
  bad.size = 4;
  EXPECT_EQ(refused(&bad, nullptr), GLECMA_ERR_INVALID);
  EXPECT_EQ(refused(nullptr, nullptr), GLECMA_ERR_INVALID);
  counting.reset();
  EXPECT_EQ(glecma_parse_dynamic_function(&function, nullptr, counting.get(), &failure, nullptr), GLECMA_ERR_INVALID);
  // The entry's own goal, and the default that stands for it, are accepted.
  options = GLECMA_PARSE_OPTIONS_INIT;
  options.goal = GLECMA_PARSE_DYNAMIC_FUNCTION;
  EXPECT_EQ(glecma_parse_dynamic_function(&function, &options, nullptr, &failure, &tree), GLECMA_OK);
  glecma_tree_destroy(tree);
  // A struct that declares the header alone is the empty function.
  struct { uint32_t size; uint32_t kind; } header = {8, GLECMA_DYNFN_GENERATOR};
  tree = nullptr;
  ASSERT_EQ(glecma_parse_dynamic_function(reinterpret_cast<const GLECMA_DynamicFunction *>(&header),
                nullptr, nullptr, &failure, &tree), GLECMA_OK);
  EXPECT_EQ(glecma_node_flags(glecma_tree_root(tree)) & GLECMA_NODEF_GENERATOR, GLECMA_NODEF_GENERATOR);
  glecma_tree_destroy(tree);
  glecma_source_release(p);
  glecma_source_release(b);
}

TEST(DynamicFunction, TheCapsApplyToTheTextsAndToTheWholeOfThem) {
  GLECMA_Limits limits = GLECMA_LIMITS_INIT;
  glecma_limits_default(&limits);
  GLECMA_ParseOptions options = GLECMA_PARSE_OPTIONS_INIT;
  options.limits = &limits;
  // The pieces fit under the cap, and the text they are assembled into does not.
  limits.source_units = 60;
  auto fits = Make(GLECMA_DYNFN_FUNCTION, "a", "return a;", &options);
  EXPECT_EQ(fits->result, GLECMA_OK);
  const std::string filler(40, 'x');
  auto over = Make(GLECMA_DYNFN_FUNCTION, "a", "var " + filler + "; var y;", &options);
  ASSERT_EQ(over->result, GLECMA_ERR_LIMIT);
  EXPECT_STREQ(over->failure.limit, "source_units");
  // Nesting is the parse's: a deep body is refused by name.
  glecma_limits_default(&limits);
  limits.nesting = 8;
  std::string deep;
  for (int i = 0; i < 20; i++) {
    deep += "(";
  }
  deep += "1";
  for (int i = 0; i < 20; i++) {
    deep += ")";
  }
  auto nested = Make(GLECMA_DYNFN_FUNCTION, "", deep, &options);
  ASSERT_EQ(nested->result, GLECMA_ERR_LIMIT);
  EXPECT_STREQ(nested->failure.limit, "nesting");
}

TEST(DynamicFunction, AFailedParseLeaksNothingAtAnyPoint) {
  for (uint32_t kind : {GLECMA_DYNFN_FUNCTION, GLECMA_DYNFN_ASYNC_GENERATOR}) {
    size_t total = 0;
    {
      glecma_test::CountingAllocator counting;
      auto made = Make(kind, "a, {b} = {}, ...c", "let d = [a, b, c]; return d;", nullptr, counting.get());
      ASSERT_EQ(made->result, GLECMA_OK);
      total = counting.calls();
      made.reset();
      EXPECT_EQ(counting.live(), 0u);
    }
    ASSERT_GT(total, 10u);
    for (size_t i = 0; i < total; i++) {
      glecma_test::CountingAllocator counting;
      counting.fail_at(i);
      auto made = Make(kind, "a, {b} = {}, ...c", "let d = [a, b, c]; return d;", nullptr, counting.get());
      EXPECT_EQ(made->result, GLECMA_ERR_OOM) << "allocation " << i << " of " << total;
      EXPECT_EQ(made->tree, nullptr);
      made.reset();
      EXPECT_EQ(counting.live(), 0u) << "allocation " << i;
    }
  }
}

int main(int argc, char * * argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

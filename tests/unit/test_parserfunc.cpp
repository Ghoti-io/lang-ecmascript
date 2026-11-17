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

// Functions and classes: the tree each form parses to, the source range of
// each (exactly the text Function.prototype.toString returns), and the
// grammar's refusals.

#include <gtest/gtest.h>

#include "../parse_test_support.h"

using glecma_test::Parse;

namespace {

struct Case {
  const char * source;
  const char * tree;
};

void ExpectAll(std::initializer_list<Case> cases) {
  for (const Case & c : cases) {
    auto parsed = Parse(c.source);
    ASSERT_EQ(parsed.result, GLECMA_OK)
        << c.source << ": " << (parsed.failure.message ? parsed.failure.message : "")
        << " at " << parsed.failure.location.offset;
    if (c.tree != nullptr) {
      EXPECT_EQ(parsed.Body(), c.tree) << c.source;
    }
  }
}

// The text of every function and class node, in source order of their start.
std::vector<std::string> FunctionTexts(const std::string & source) {
  auto parsed = Parse(source);
  EXPECT_EQ(parsed.result, GLECMA_OK) << source;
  std::vector<std::string> out;
  std::vector<const GLECMA_Node *> stack = {glecma_tree_root(parsed.tree)};
  while (!stack.empty()) {
    const GLECMA_Node * node = stack.back();
    stack.pop_back();
    if (node == nullptr) {
      continue;
    }
    switch (glecma_node_kind(node)) {
      case GLECMA_NODE_FUNCTION_DECLARATION: case GLECMA_NODE_FUNCTION_EXPRESSION:
      case GLECMA_NODE_ARROW_FUNCTION: case GLECMA_NODE_CLASS_DECLARATION:
      case GLECMA_NODE_CLASS_EXPRESSION:
        out.push_back(source.substr(glecma_node_start(node),
            glecma_node_end(node) - glecma_node_start(node)));
        break;
      default:
        break;
    }
    for (size_t i = glecma_node_child_count(node); i > 0; i--) {
      stack.push_back(glecma_node_child(node, i - 1));
    }
  }
  return out;
}

}  // namespace

TEST(ParseFunctions, DeclarationsAndExpressionsInTheirKinds) {
  ExpectAll({
    {"function f() {}", "(FunctionDeclaration (Identifier f) (ParameterList) (FunctionBody))"},
    {"function f(a, b = 1, ...c) { return a; }",
        "(FunctionDeclaration (Identifier f) (ParameterList (Identifier a) (AssignmentPattern (Identifier b) (NumberLiteral)) "
        "(RestElement (Identifier c))) (FunctionBody (Return (Identifier a))))"},
    {"function f([a], {b}) {}",
        "(FunctionDeclaration (Identifier f) (ParameterList (ArrayPattern (Identifier a)) "
        "(ObjectPattern (PatternProperty/shorthand (Identifier b) (Identifier b)))) (FunctionBody))"},
    {"function* g() {}", "(FunctionDeclaration/gen (Identifier g) (ParameterList) (FunctionBody))"},
    {"async function f() {}", "(FunctionDeclaration/async (Identifier f) (ParameterList) (FunctionBody))"},
    {"async function* g() {}", "(FunctionDeclaration/async/gen (Identifier g) (ParameterList) (FunctionBody))"},
    {"x = function () {};", "(Expression (Assignment = (Identifier x) (Function _ (ParameterList) (FunctionBody))))"},
    {"x = function f() {};", "(Expression (Assignment = (Identifier x) (Function (Identifier f) (ParameterList) (FunctionBody))))"},
    {"x = function* () {};", "(Expression (Assignment = (Identifier x) (Function/gen _ (ParameterList) (FunctionBody))))"},
    {"x = async function () {};", "(Expression (Assignment = (Identifier x) (Function/async _ (ParameterList) (FunctionBody))))"},
    {"x = async function* f() {};", "(Expression (Assignment = (Identifier x) (Function/async/gen (Identifier f) (ParameterList) (FunctionBody))))"},
    {"(function () {})();", "(Expression (Call (Function _ (ParameterList) (FunctionBody))))"},
    {"function yield() {}", "(FunctionDeclaration (Identifier yield) (ParameterList) (FunctionBody))"},
    {"x = function* yield2() {};", nullptr},
    {"var async; async\nfunction f() {}", nullptr},
    {"async function f() { x = function await() {}; }", nullptr},
  });
}

TEST(ParseFunctions, AFunctionsRangeIsItsFunctionPrototypeToStringText) {
  struct Range {
    const char * source;
    std::vector<std::string> texts;
  };
  const Range cases[] = {
    {"function f(a) { return a; }", {"function f(a) { return a; }"}},
    {"x = function () {} ;", {"function () {}"}},
    {"x = async function* g() {}", {"async function* g() {}"}},
    {"x = (a, b) => a + b;", {"(a, b) => a + b"}},
    {"x = a => { return a; };", {"a => { return a; }"}},
    {"x = async (a) => a;", {"async (a) => a"}},
    {"x = async a => a;", {"async a => a"}},
    {"x = { m() {} };", {"m() {}"}},
    {"x = { get a() {} };", {"get a() {}"}},
    {"x = { async *[k]() {} };", {"async *[k]() {}"}},
    {"x = { a: function () {} };", {"function () {}"}},
    {"x = class C { static m() {} };", {"class C { static m() {} }", "m() {}"}},
    {"x = class { constructor() {} get a() {} static set a(v) {} async b() {} *c() {} };",
        {"class { constructor() {} get a() {} static set a(v) {} async b() {} *c() {} }", "constructor() {}",
            "get a() {}", "set a(v) {}", "async b() {}", "*c() {}"}},
    {"class A { 'x'() {} 1() {} [y]() {} #z() {} }",
        {"class A { 'x'() {} 1() {} [y]() {} #z() {} }", "'x'() {}", "1() {}", "[y]() {}", "#z() {}"}},
    {"function f() { function g() {} }", {"function f() { function g() {} }", "function g() {}"}},
    {"x = (a = function () {}) => 1;", {"(a = function () {}) => 1", "function () {}"}},
    {"/* c */ function f() {} // tail", {"function f() {}"}},
  };
  for (const Range & c : cases) {
    EXPECT_EQ(FunctionTexts(c.source), c.texts) << c.source;
  }
}

// A node's range includes the parentheses of a parenthesized expression at
// its edge: the expression's own node excludes them (tree.h), and whatever
// holds it takes them in.
TEST(ParseFunctions, ARangeTakesInAParenthesizedOperandAtItsEdge) {
  struct Case {
    const char * source;
    const char * text;  // the text of the first statement's first child
  };
  const Case cases[] = {
    {"x = (a, b) => (c);", "x = (a, b) => (c)"},
    {"a * (b);", "a * (b)"},
    {"(1 + 2) * 3;", "(1 + 2) * 3"},
    {"(a) = 1;", "(a) = 1"},
    {"x = (a) ? (b) : (c);", "x = (a) ? (b) : (c)"},
    {"f((a), (b));", "f((a), (b))"},
    {"(a).b((c));", "(a).b((c))"},
    {"new (a)((b));", "new (a)((b))"},
    {"[...(a)];", "[...(a)]"},
    {"({a: (1)});", "{a: (1)}"},
    {"-(a);", "-(a)"},
    {"(a)++;", "(a)++"},
    {"typeof (a);", "typeof (a)"},
    {"`${(a)}`;", "`${(a)}`"},
  };
  for (const Case & c : cases) {
    auto parsed = Parse(c.source);
    ASSERT_EQ(parsed.result, GLECMA_OK) << c.source;
    const GLECMA_Node * statement = glecma_node_child(glecma_tree_root(parsed.tree), 0);
    const GLECMA_Node * node = glecma_node_child(statement, 0);
    std::string source = c.source;
    EXPECT_EQ(source.substr(glecma_node_start(node),
                  glecma_node_end(node) - glecma_node_start(node)),
        c.text)
        << c.source;
    // The statement ends where its last token does.
    EXPECT_EQ(glecma_node_end(statement), source.size()) << c.source;
  }
  // The parenthesized expression's own node is the inner one.
  auto inner = Parse("(a);");
  const GLECMA_Node * identifier = glecma_node_child(glecma_node_child(glecma_tree_root(inner.tree), 0), 0);
  EXPECT_EQ(glecma_node_start(identifier), 1u);
  EXPECT_EQ(glecma_node_end(identifier), 2u);
  // Declarations and members that hold one.
  EXPECT_EQ(FunctionTexts("x = (a) => (b)")[0], "(a) => (b)");
  EXPECT_EQ(FunctionTexts("var f = function () {}, g = (function () {})")[1], "function () {}");
  auto declaration = Parse("var x = (1);");
  const GLECMA_Node * var = glecma_node_child(glecma_tree_root(declaration.tree), 0);
  const GLECMA_Node * declarator = glecma_node_child(var, 0);
  EXPECT_EQ(glecma_node_end(declarator), 11u);
  EXPECT_EQ(glecma_node_end(var), 12u);
  auto field = Parse("class A { x = (1) }");
  const GLECMA_Node * body = glecma_node_child(glecma_node_child(glecma_tree_root(field.tree), 0), 2);
  EXPECT_EQ(glecma_node_end(glecma_node_child(body, 0)), 17u);
}

TEST(ParseFunctions, ClassesInAllTheirParts) {
  ExpectAll({
    {"class A {}", "(ClassDeclaration (Identifier A) _ (ClassBody))"},
    {"x = class {};", "(Expression (Assignment = (Identifier x) (Class _ _ (ClassBody))))"},
    {"class A extends B {}", "(ClassDeclaration/derived (Identifier A) (Identifier B) (ClassBody))"},
    {"class A extends B.c(d) {}",
        "(ClassDeclaration/derived (Identifier A) (Call (Member (Identifier B) (Identifier c)) (Identifier d)) (ClassBody))"},
    {"class A { m() {} }",
        "(ClassDeclaration (Identifier A) _ (ClassBody (MethodDefinition/method (Identifier m) "
        "(Function _ (ParameterList) (FunctionBody/strict)))))"},
    {"class A { constructor(a) {} }",
        "(ClassDeclaration (Identifier A) _ (ClassBody (MethodDefinition/ctor/method (Identifier constructor) "
        "(Function _ (ParameterList (Identifier a)) (FunctionBody/strict)))))"},
    {"class A { 'constructor'() {} }",
        "(ClassDeclaration (Identifier A) _ (ClassBody (MethodDefinition/ctor/method (StringLiteral \"constructor\") "
        "(Function _ (ParameterList) (FunctionBody/strict)))))"},
    {"class A { static constructor() {} get constructor() {} }", nullptr},
    {"class A { get a() {} set a(v) {} }",
        "(ClassDeclaration (Identifier A) _ (ClassBody (MethodDefinition/get (Identifier a) (Function _ (ParameterList) (FunctionBody/strict))) "
        "(MethodDefinition/set (Identifier a) (Function _ (ParameterList (Identifier v)) (FunctionBody/strict)))))"},
    {"class A { static async *m() {} }",
        "(ClassDeclaration (Identifier A) _ (ClassBody (MethodDefinition/static/method (Identifier m) "
        "(Function/async/gen _ (ParameterList) (FunctionBody/strict)))))"},
    {"class A { [a]() {} static [b] = 1; }",
        "(ClassDeclaration (Identifier A) _ (ClassBody (MethodDefinition/computed/method (Identifier a) "
        "(Function _ (ParameterList) (FunctionBody/strict))) (FieldDefinition/computed/static (Identifier b) (NumberLiteral))))"},
    {"class A { a; b = 1; static c; static d = 2; #e = 3; static #f; }",
        "(ClassDeclaration (Identifier A) _ (ClassBody (FieldDefinition (Identifier a) _) (FieldDefinition (Identifier b) (NumberLiteral)) "
        "(FieldDefinition/static (Identifier c) _) (FieldDefinition/static (Identifier d) (NumberLiteral)) "
        "(FieldDefinition (PrivateName e) (NumberLiteral)) (FieldDefinition/static (PrivateName f) _)))"},
    {"class A { static { a; } static {} }",
        "(ClassDeclaration (Identifier A) _ (ClassBody (StaticBlock (Expression (Identifier a))) (StaticBlock)))"},
    {"class A { ;; a(){} ; }", nullptr},
    {"class A { a\nb\nc() {} }",
        "(ClassDeclaration (Identifier A) _ (ClassBody (FieldDefinition (Identifier a) _) (FieldDefinition (Identifier b) _) "
        "(MethodDefinition/method (Identifier c) (Function _ (ParameterList) (FunctionBody/strict)))))"},
    {"class A { x = this.#y; #y; m() { return #y in this; } }", nullptr},
  });
}

TEST(ParseFunctions, ContextualWordsAreNamesWhereTheyAreNotModifiers) {
  ExpectAll({
    {"class A { get; set; static; async; }",
        "(ClassDeclaration (Identifier A) _ (ClassBody (FieldDefinition (Identifier get) _) (FieldDefinition (Identifier set) _) "
        "(FieldDefinition (Identifier static) _) (FieldDefinition (Identifier async) _)))"},
    {"class A { get() {} set() {} static() {} async() {} }",
        "(ClassDeclaration (Identifier A) _ (ClassBody "
        "(MethodDefinition/method (Identifier get) (Function _ (ParameterList) (FunctionBody/strict))) "
        "(MethodDefinition/method (Identifier set) (Function _ (ParameterList) (FunctionBody/strict))) "
        "(MethodDefinition/method (Identifier static) (Function _ (ParameterList) (FunctionBody/strict))) "
        "(MethodDefinition/method (Identifier async) (Function _ (ParameterList) (FunctionBody/strict)))))"},
    {"class A { get = 1; static = 2; async = 3; static static; }", nullptr},
    {"class A { static get() {} static set() {} static async() {} }", nullptr},
    {"class A { get\n*a() {} }",
        "(ClassDeclaration (Identifier A) _ (ClassBody (FieldDefinition (Identifier get) _) "
        "(MethodDefinition/method (Identifier a) (Function/gen _ (ParameterList) (FunctionBody/strict)))))"},
    {"class A { async\nm() {} }",
        "(ClassDeclaration (Identifier A) _ (ClassBody (FieldDefinition (Identifier async) _) "
        "(MethodDefinition/method (Identifier m) (Function _ (ParameterList) (FunctionBody/strict)))))"},
    {"x = { get, set, async, static };", nullptr},
    {"x = { get() {}, set() {}, async() {}, get a() {}, set a(v) {}, async b() {}, async *c() {}, *d() {}, async: 1, get: 2 };", nullptr},
  });
  EXPECT_EQ(Parse("x = { async\nb() {} };").result, GLECMA_ERR_SYNTAX);
}

TEST(ParseFunctions, RefusalsOfTheFunctionAndClassGrammar) {
  struct Refusal {
    const char * source;
    uint32_t rule;
    size_t offset;
  };
  const Refusal refusals[] = {
    {"class {}", GLECMA_RULE_UNEXPECTED_TOKEN, 6},
    {"class A { a b }", GLECMA_RULE_MISSING_SEMICOLON, 12},
    {"class A { get a(b) {} }", GLECMA_RULE_UNEXPECTED_TOKEN, 16},
    {"class A { set a() {} }", GLECMA_RULE_UNEXPECTED_TOKEN, 16},
    {"class A { set a(...b) {} }", GLECMA_RULE_UNEXPECTED_TOKEN, 16},
    {"class A { set a(b, c) {} }", GLECMA_RULE_UNEXPECTED_TOKEN, 17},
    {"class A { async get a() {} }", GLECMA_RULE_UNEXPECTED_TOKEN, 20},
    {"class A { * a }", GLECMA_RULE_UNEXPECTED_TOKEN, 14},
    {"class A { a: 1 }", GLECMA_RULE_MISSING_SEMICOLON, 11},
    {"class A extends B, C {}", GLECMA_RULE_UNEXPECTED_TOKEN, 17},
    {"x = { get a(b) {} };", GLECMA_RULE_UNEXPECTED_TOKEN, 12},
    {"x = { set a() {} };", GLECMA_RULE_UNEXPECTED_TOKEN, 12},
    {"x = { async *a };", GLECMA_RULE_UNEXPECTED_TOKEN, 15},
    {"x = { a b };", GLECMA_RULE_UNEXPECTED_TOKEN, 8},
    {"x = { if };", GLECMA_RULE_RESERVED_WORD, 6},
    {"x = { 'a' };", GLECMA_RULE_UNEXPECTED_TOKEN, 10},
    {"x = { [a] };", GLECMA_RULE_UNEXPECTED_TOKEN, 10},
    {"x = { 1 };", GLECMA_RULE_UNEXPECTED_TOKEN, 8},
    {"function* g() { yield\n* a; }", GLECMA_RULE_UNEXPECTED_TOKEN, 22},
  };
  for (const Refusal & r : refusals) {
    auto parsed = Parse(r.source);
    ASSERT_EQ(parsed.result, GLECMA_ERR_SYNTAX) << r.source;
    EXPECT_EQ(parsed.failure.rule, r.rule)
        << r.source << ": " << glecma_rule_name(parsed.failure.rule);
    EXPECT_EQ(parsed.failure.location.offset, r.offset) << r.source;
  }
}

int main(int argc, char * * argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

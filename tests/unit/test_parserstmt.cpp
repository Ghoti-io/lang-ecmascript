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

// Statements and declarations: the tree each form parses to, automatic
// semicolon insertion, the directive prologue and the grammar's refusals.

#include <gtest/gtest.h>

#include "../parse_test_support.h"

using glecma_test::Parse;

namespace {

struct Case {
  const char * source;
  const char * tree;
};

void ExpectAll(std::initializer_list<Case> cases, bool strict = false) {
  for (const Case & c : cases) {
    auto parsed = Parse(c.source, strict);
    ASSERT_EQ(parsed.result, GLECMA_OK)
        << c.source << ": " << (parsed.failure.message ? parsed.failure.message : "")
        << " at " << parsed.failure.location.offset;
    if (c.tree != nullptr) {
      EXPECT_EQ(parsed.Body(), c.tree) << c.source;
    }
  }
}

struct Refusal {
  const char * source;
  uint32_t rule;
  size_t offset;
};

void RefuseAll(std::initializer_list<Refusal> refusals, bool strict = false) {
  for (const Refusal & r : refusals) {
    auto parsed = Parse(r.source, strict);
    ASSERT_EQ(parsed.result, GLECMA_ERR_SYNTAX) << r.source;
    EXPECT_EQ(parsed.failure.rule, r.rule)
        << r.source << ": " << glecma_rule_name(parsed.failure.rule);
    EXPECT_EQ(parsed.failure.location.offset, r.offset) << r.source;
  }
}

}  // namespace

TEST(ParseStatements, SimpleStatements) {
  ExpectAll({
    {";", "(Empty)"},
    {"{}", "(Block)"},
    {"{ a; b }", "(Block (Expression (Identifier a)) (Expression (Identifier b)))"},
    {"debugger;", "(Debugger)"},
    {"throw a;", "(Throw (Identifier a))"},
    {"return;", "(Return _)"},
    {"return a, b;", "(Return (Sequence (Identifier a) (Identifier b)))"},
    {"break; continue;", "(Break _) (Continue _)"},
    {"a: for (;;) { break a; continue a; }",
        "(Labelled (Identifier a) (For _ _ _ (Block (Break (Identifier a)) (Continue (Identifier a)))))"},
    {"a: b: c;", "(Labelled (Identifier a) (Labelled (Identifier b) (Expression (Identifier c))))"},
    {"with (a) b;", "(With (Identifier a) (Expression (Identifier b)))"},
    {"while (a) b;", "(While (Identifier a) (Expression (Identifier b)))"},
    {"do a; while (b)", "(DoWhile (Expression (Identifier a)) (Identifier b))"},
    {"do a; while (b) c;", "(DoWhile (Expression (Identifier a)) (Identifier b)) (Expression (Identifier c))"},
    {"if (a) b; else c;", "(If (Identifier a) (Expression (Identifier b)) (Expression (Identifier c)))"},
    {"if (a) b;", "(If (Identifier a) (Expression (Identifier b)) _)"},
    {"if (a) if (b) c; else d;",
        "(If (Identifier a) (If (Identifier b) (Expression (Identifier c)) (Expression (Identifier d))) _)"},
  });
}

TEST(ParseStatements, VariableDeclarations) {
  ExpectAll({
    {"var a = 1, b;",
        "(VariableDeclaration/var (VariableDeclarator (Identifier a) (NumberLiteral)) (VariableDeclarator (Identifier b) _))"},
    {"let a;", "(VariableDeclaration/let (VariableDeclarator (Identifier a) _))"},
    {"const a = 1;", "(VariableDeclaration/const (VariableDeclarator (Identifier a) (NumberLiteral)))"},
    {"let [a, b] = c;",
        "(VariableDeclaration/let (VariableDeclarator (ArrayPattern (Identifier a) (Identifier b)) (Identifier c)))"},
    {"const {a, b: c = 1, ...d} = e;",
        "(VariableDeclaration/const (VariableDeclarator (ObjectPattern (PatternProperty/shorthand (Identifier a) (Identifier a)) "
        "(PatternProperty (Identifier b) (AssignmentPattern (Identifier c) (NumberLiteral))) (RestElement (Identifier d))) (Identifier e)))"},
    {"var [a = 1, , [b], ...c] = d;",
        "(VariableDeclaration/var (VariableDeclarator (ArrayPattern (AssignmentPattern (Identifier a) (NumberLiteral)) _ "
        "(ArrayPattern (Identifier b)) (RestElement (Identifier c))) (Identifier d)))"},
    {"var {if: a, 'b': c, 1: d, [e]: f} = g;",
        "(VariableDeclaration/var (VariableDeclarator (ObjectPattern (PatternProperty (Identifier if) (Identifier a)) "
        "(PatternProperty (StringLiteral \"b\") (Identifier c)) (PatternProperty (NumberLiteral) (Identifier d)) "
        "(PatternProperty/computed (Identifier e) (Identifier f))) (Identifier g)))"},
  });
}

TEST(ParseStatements, LetIsAnIdentifierOnlyWhereThereIsNoBinding) {
  ExpectAll({
    {"let\nx = 1;", "(VariableDeclaration/let (VariableDeclarator (Identifier x) (NumberLiteral)))"},
    {"let = 1;", "(Expression (Assignment = (Identifier let) (NumberLiteral)))"},
    {"let;", "(Expression (Identifier let))"},
    {"let.a;", "(Expression (Member (Identifier let) (Identifier a)))"},
    {"let in a;", "(Expression (Binary in (Identifier let) (Identifier a)))"},
    {"if (a) let\nb", "(If (Identifier a) (Expression (Identifier let)) _) (Expression (Identifier b))"},
    {"var let;", "(VariableDeclaration/var (VariableDeclarator (Identifier let) _))"},
    {"for (let in a);", "(ForIn (Identifier let) (Identifier a) (Empty))"},
    {"for (let of of b);", "(ForOf (VariableDeclaration/let (VariableDeclarator (Identifier of) _)) (Identifier b) (Empty))"},
  });
  RefuseAll({
    {"if (a) let [b] = c;", GLECMA_RULE_DECLARATION_POSITION, 7},
    {"let [a];", GLECMA_RULE_UNEXPECTED_TOKEN, 7},
    {"for (let of x);", GLECMA_RULE_UNEXPECTED_TOKEN, 12},
  });
  RefuseAll({
    {"let = 1;", GLECMA_RULE_UNEXPECTED_TOKEN, 4},
  }, true);
}

TEST(ParseStatements, ForStatementsInAllTheirForms) {
  ExpectAll({
    {"for (;;);", "(For _ _ _ (Empty))"},
    {"for (var i = 0; i < n; i++) x;",
        "(For (VariableDeclaration/var (VariableDeclarator (Identifier i) (NumberLiteral))) (Binary < (Identifier i) (Identifier n)) "
        "(Update ++ (Identifier i)) (Expression (Identifier x)))"},
    {"for (a, b; ; c);", "(For (Sequence (Identifier a) (Identifier b)) _ (Identifier c) (Empty))"},
    {"for (let i = 0, j; ;);",
        "(For (VariableDeclaration/let (VariableDeclarator (Identifier i) (NumberLiteral)) (VariableDeclarator (Identifier j) _)) _ _ (Empty))"},
    {"for (x in y) z;", "(ForIn (Identifier x) (Identifier y) (Expression (Identifier z)))"},
    {"for (var x in y, z);", "(ForIn (VariableDeclaration/var (VariableDeclarator (Identifier x) _)) (Sequence (Identifier y) (Identifier z)) (Empty))"},
    {"for (var x = 1 in y);",
        "(ForIn (VariableDeclaration/var (VariableDeclarator (Identifier x) (NumberLiteral))) (Identifier y) (Empty))"},
    {"for (x of y) z;", "(ForOf (Identifier x) (Identifier y) (Expression (Identifier z)))"},
    {"for (const [a] of b);", "(ForOf (VariableDeclaration/const (VariableDeclarator (ArrayPattern (Identifier a)) _)) (Identifier b) (Empty))"},
    {"for (a.b of c);", "(ForOf (Member (Identifier a) (Identifier b)) (Identifier c) (Empty))"},
    {"for (var a = 1 in b ? 1 : 2);", nullptr},
    {"for ((a in b);;);", "(For (Binary in (Identifier a) (Identifier b)) _ _ (Empty))"},
    {"for (var a = (b in c);;);", "(For (VariableDeclaration/var (VariableDeclarator (Identifier a) (Binary in (Identifier b) (Identifier c)))) _ _ (Empty))"},
    {"for ([a in b];;);", "(For (ArrayLiteral (Binary in (Identifier a) (Identifier b))) _ _ (Empty))"},
    {"for (async of => {};;);", nullptr},
  });
  ExpectAll({
    {"async function f() { for await (x of y); for await (const z of w); }",
        "(FunctionDeclaration/async (Identifier f) (ParameterList) (FunctionBody (ForOf/await (Identifier x) (Identifier y) (Empty)) "
        "(ForOf/await (VariableDeclaration/const (VariableDeclarator (Identifier z) _)) (Identifier w) (Empty))))"},
    {"async function f() { for await (async of x); }", nullptr},
  });
  RefuseAll({
    {"for (x of a, b);", GLECMA_RULE_UNEXPECTED_TOKEN, 11},
    {"for (async of x);", GLECMA_RULE_FOR_HEAD_RESTRICTED, 5},
    {"for (let of x);", GLECMA_RULE_UNEXPECTED_TOKEN, 12},
    {"for (1 of a);", GLECMA_RULE_INVALID_FOR_TARGET, 5},
    {"for (a, b in c);", GLECMA_RULE_UNEXPECTED_TOKEN, 10},
    {"for (a = b in c);", GLECMA_RULE_INVALID_FOR_TARGET, 5},
    {"for (var a, b of c);", GLECMA_RULE_FOR_HEAD_RESTRICTED, 14},
    {"for (var a = 1 of b);", GLECMA_RULE_FOR_HEAD_RESTRICTED, 15},
    {"for (let a = 1 in b);", GLECMA_RULE_FOR_HEAD_RESTRICTED, 15},
    {"for (var [a] = 1 in b);", GLECMA_RULE_FOR_HEAD_RESTRICTED, 17},
    {"for (var [a];;);", GLECMA_RULE_UNEXPECTED_TOKEN, 12},
    {"for await (x of y);", GLECMA_RULE_FOR_AWAIT_POSITION, 4},
    {"async function f() { for await (x in y); }", GLECMA_RULE_UNEXPECTED_TOKEN, 37},
    {"for (;);", GLECMA_RULE_UNEXPECTED_TOKEN, 6},
  });
  RefuseAll({
    {"for (var a = 1 in b);", GLECMA_RULE_FOR_HEAD_RESTRICTED, 15},
  }, true);
}

TEST(ParseStatements, SwitchAndTry) {
  ExpectAll({
    {"switch (a) { case 1: b; break; default: c }",
        "(Switch (Identifier a) (SwitchCase (NumberLiteral) (Expression (Identifier b)) (Break _)) "
        "(SwitchCase/default _ (Expression (Identifier c))))"},
    {"switch (a) {}", "(Switch (Identifier a))"},
    {"switch (a) { case b: case c: }", "(Switch (Identifier a) (SwitchCase (Identifier b)) (SwitchCase (Identifier c)))"},
    {"switch (a) { default: let b; function c() {} }",
        "(Switch (Identifier a) (SwitchCase/default _ (VariableDeclaration/let (VariableDeclarator (Identifier b) _)) "
        "(FunctionDeclaration (Identifier c) (ParameterList) (FunctionBody))))"},
    {"try { a } catch (e) { b } finally { c }",
        "(Try (Block (Expression (Identifier a))) (CatchClause (Identifier e) (Block (Expression (Identifier b)))) "
        "(Block (Expression (Identifier c))))"},
    {"try {} catch {}", "(Try (Block) (CatchClause _ (Block)) _)"},
    {"try {} finally {}", "(Try (Block) _ (Block))"},
    {"try {} catch ({a}) {}",
        "(Try (Block) (CatchClause (ObjectPattern (PatternProperty/shorthand (Identifier a) (Identifier a))) (Block)) _)"},
  });
  RefuseAll({
    {"try {}", GLECMA_RULE_UNEXPECTED_END, 6},
    {"try {} catch () {}", GLECMA_RULE_UNEXPECTED_TOKEN, 14},
    {"switch (a) { b }", GLECMA_RULE_UNEXPECTED_TOKEN, 13},
    {"switch (a) { case 1 b }", GLECMA_RULE_UNEXPECTED_TOKEN, 20},
    {"else a;", GLECMA_RULE_RESERVED_WORD, 0},
  });
}

TEST(ParseStatements, AnnexBFunctionDeclarationsInStatementPosition) {
  ExpectAll({
    {"if (a) function f() {}",
        "(If (Identifier a) (Block (FunctionDeclaration (Identifier f) (ParameterList) (FunctionBody))) _)"},
    {"if (a) b; else function f() {}",
        "(If (Identifier a) (Expression (Identifier b)) (Block (FunctionDeclaration (Identifier f) (ParameterList) (FunctionBody))))"},
    {"a: function f() {}", "(Labelled (Identifier a) (FunctionDeclaration (Identifier f) (ParameterList) (FunctionBody)))"},
    {"a: b: function f() {}",
        "(Labelled (Identifier a) (Labelled (Identifier b) (FunctionDeclaration (Identifier f) (ParameterList) (FunctionBody))))"},
  });
  RefuseAll({
    {"if (a) function* g() {}", GLECMA_RULE_UNEXPECTED_TOKEN, 15},
    {"if (a) async function f() {}", GLECMA_RULE_DECLARATION_POSITION, 7},
    {"while (a) function f() {}", GLECMA_RULE_DECLARATION_POSITION, 10},
    {"if (a) class C {}", GLECMA_RULE_DECLARATION_POSITION, 7},
    {"if (a) const b = 1;", GLECMA_RULE_DECLARATION_POSITION, 7},
    {"a: class C {}", GLECMA_RULE_DECLARATION_POSITION, 3},
  });
  RefuseAll({
    {"if (a) function f() {}", GLECMA_RULE_DECLARATION_POSITION, 7},
    {"a: function f() {}", GLECMA_RULE_DECLARATION_POSITION, 3},
  }, true);
}

TEST(ParseStatements, AutomaticSemicolonInsertion) {
  ExpectAll({
    {"a\nb", "(Expression (Identifier a)) (Expression (Identifier b))"},
    {"a = b\n(c)", "(Expression (Assignment = (Identifier a) (Call (Identifier b) (Identifier c))))"},
    {"a\n++b", "(Expression (Identifier a)) (Expression (Update/prefix ++ (Identifier b)))"},
    {"a\n/b/g", "(Expression (Binary / (Binary / (Identifier a) (Identifier b)) (Identifier g)))"},
    {"{ a }", "(Block (Expression (Identifier a)))"},
    {"{ a\nb }", "(Block (Expression (Identifier a)) (Expression (Identifier b)))"},
    {"function f() { return\na }",
        "(FunctionDeclaration (Identifier f) (ParameterList) (FunctionBody (Return _) (Expression (Identifier a))))"},
    {"a: for (;;) { break\na }",
        "(Labelled (Identifier a) (For _ _ _ (Block (Break _) (Expression (Identifier a)))))"},
    {"var a = 1\nvar b = 2", "(VariableDeclaration/var (VariableDeclarator (Identifier a) (NumberLiteral))) (VariableDeclaration/var (VariableDeclarator (Identifier b) (NumberLiteral)))"},
    {"a\n`b`", "(Expression (TaggedTemplate (Identifier a) (TemplateLiteral (TemplateElement \"b\" \"b\"))))"},
    {"if (a) b\nelse c", "(If (Identifier a) (Expression (Identifier b)) (Expression (Identifier c)))"},
    {"for (a\n;;) {}", "(For (Identifier a) _ _ (Block))"},
    {"do ; while (a) b", "(DoWhile (Empty) (Identifier a)) (Expression (Identifier b))"},
    {"x = async\nfunction f() {}",
        "(Expression (Assignment = (Identifier x) (Identifier async))) (FunctionDeclaration (Identifier f) (ParameterList) (FunctionBody))"},
  });
  RefuseAll({
    {"a b", GLECMA_RULE_MISSING_SEMICOLON, 2},
    {"var a = 1 var b", GLECMA_RULE_MISSING_SEMICOLON, 10},
    {"throw\na", GLECMA_RULE_THROW_LINE_TERMINATOR, 6},
    {"if (a) b else c", GLECMA_RULE_MISSING_SEMICOLON, 9},
  });
}

TEST(ParseStatements, TheDirectivePrologueAndStrictness) {
  auto strict = Parse("'use strict'; a;");
  ASSERT_EQ(strict.result, GLECMA_OK);
  EXPECT_TRUE(glecma_tree_strict(strict.tree));
  EXPECT_EQ(strict.Whole(),
      "(Script/strict (Expression/directive (StringLiteral \"use strict\")) (Expression (Identifier a)))");
  auto sloppy = Parse("a; 'use strict';");
  ASSERT_EQ(sloppy.result, GLECMA_OK);
  EXPECT_FALSE(glecma_tree_strict(sloppy.tree));
  EXPECT_EQ(sloppy.Whole(), "(Script (Expression (Identifier a)) (Expression (StringLiteral \"use strict\")))");
  // An escape, or an operator after the string, is not the directive.
  for (const char * source : {"'use\\x20strict'; with (a) b;", "('use strict'); with (a) b;",
           "'use strict' + 1; with (a) b;"}) {
    auto parsed = Parse(source);
    ASSERT_EQ(parsed.result, GLECMA_OK) << source;
    EXPECT_FALSE(glecma_tree_strict(parsed.tree)) << source;
  }
  // Several directives, and strictness starting at the one that asks.
  auto several = Parse("'a'; 'use strict'; 'b'; 010;");
  EXPECT_EQ(several.result, GLECMA_ERR_SYNTAX);
  EXPECT_EQ(several.failure.rule, static_cast<uint32_t>(GLECMA_RULE_NUMERIC_LEGACY_OCTAL_STRICT));
  // A function's own directive makes the function strict and nothing after it.
  auto function = Parse("function f() { 'use strict'; } 010; function g() { 011 }");
  ASSERT_EQ(function.result, GLECMA_OK);
  EXPECT_FALSE(glecma_tree_strict(function.tree));
  const GLECMA_Node * root = glecma_tree_root(function.tree);
  const GLECMA_Node * f = glecma_node_child(root, 0);
  const GLECMA_Node * body = glecma_node_child(f, 2);
  EXPECT_NE(glecma_node_flags(body) & GLECMA_NODEF_STRICT, 0u);
  const GLECMA_Node * g = glecma_node_child(root, 2);
  EXPECT_EQ(glecma_node_flags(glecma_node_child(g, 2)) & GLECMA_NODEF_STRICT, 0u);
  // What was read before the directive is checked when it turns strictness on.
  RefuseAll({
    {"'use strict'\n010", GLECMA_RULE_NUMERIC_LEGACY_OCTAL_STRICT, 13},
    {"'use strict'; 089;", GLECMA_RULE_NUMERIC_NON_OCTAL_DECIMAL_STRICT, 14},
    {"'\\07'; 'use strict';", GLECMA_RULE_ESCAPE_LEGACY_OCTAL_STRICT, 0},
    {"'\\8'; 'use strict';", GLECMA_RULE_ESCAPE_NON_OCTAL_DECIMAL_STRICT, 0},
    {"'use strict'; '\\07';", GLECMA_RULE_ESCAPE_LEGACY_OCTAL_STRICT, 14},
    {"function f() { 'use strict'; 010 }", GLECMA_RULE_NUMERIC_LEGACY_OCTAL_STRICT, 29},
    {"function f() { '\\07'; 'use strict'; }", GLECMA_RULE_ESCAPE_LEGACY_OCTAL_STRICT, 15},
    {"'use strict'; var let;", GLECMA_RULE_RESERVED_WORD_STRICT, 18},
    {"'use strict'; var yield;", GLECMA_RULE_RESERVED_WORD_STRICT, 18},
    {"function f() { 'use strict'; var static; }", GLECMA_RULE_RESERVED_WORD_STRICT, 33},
  });
}

TEST(ParseStatements, AFunctionOrClassIsStrictAndTheCodeAroundItIsNot) {
  // Lexing resumes in the strictness of the code around a strict function or
  // class, so an octal literal after it is read as the sloppy code it is in.
  EXPECT_EQ(Parse("function f() { 'use strict'; } 010;").result, GLECMA_OK);
  EXPECT_EQ(Parse("class A {} 010;").result, GLECMA_OK);
  EXPECT_EQ(Parse("(function() { 'use strict'; })\n010;").result, GLECMA_OK);
  EXPECT_EQ(Parse("class A { m() { 010 } }").result, GLECMA_ERR_SYNTAX);
  EXPECT_EQ(Parse("class A extends 010 {}").result, GLECMA_ERR_SYNTAX);
  EXPECT_EQ(Parse("class let {}").result, GLECMA_ERR_SYNTAX);
}

TEST(ParseStatements, RefusalsOfTheStatementGrammar) {
  RefuseAll({
    {"if", GLECMA_RULE_UNEXPECTED_END, 2},
    {"if (a", GLECMA_RULE_UNEXPECTED_END, 5},
    {"if a", GLECMA_RULE_UNEXPECTED_TOKEN, 3},
    {"while", GLECMA_RULE_UNEXPECTED_END, 5},
    {"do a", GLECMA_RULE_UNEXPECTED_END, 4},
    {"{", GLECMA_RULE_UNEXPECTED_END, 1},
    {"}", GLECMA_RULE_UNEXPECTED_TOKEN, 0},
    {"var;", GLECMA_RULE_UNEXPECTED_TOKEN, 3},
    {"var 1;", GLECMA_RULE_UNEXPECTED_TOKEN, 4},
    {"var a, ;", GLECMA_RULE_UNEXPECTED_TOKEN, 7},
    {"var [a];", GLECMA_RULE_UNEXPECTED_TOKEN, 7},
    {"var {a};", GLECMA_RULE_UNEXPECTED_TOKEN, 7},
    {"const [a];", GLECMA_RULE_UNEXPECTED_TOKEN, 9},
    {"export a;", GLECMA_RULE_UNEXPECTED_TOKEN, 0},
    {"a: ;;;; b: c d", GLECMA_RULE_MISSING_SEMICOLON, 13},
    {"yield: 1; function* g() { yield: 1 }", GLECMA_RULE_MISSING_SEMICOLON, 31},
    {"function", GLECMA_RULE_UNEXPECTED_END, 8},
    {"function () {}", GLECMA_RULE_UNEXPECTED_TOKEN, 9},
    {"function f( {}", GLECMA_RULE_UNEXPECTED_END, 14},
    {"function f(a b) {}", GLECMA_RULE_UNEXPECTED_TOKEN, 13},
    {"function f(...a, b) {}", GLECMA_RULE_REST_ELEMENT_POSITION, 15},
    {"function f(...a = 1) {}", GLECMA_RULE_REST_ELEMENT_POSITION, 16},
    {"function f(a) {", GLECMA_RULE_UNEXPECTED_END, 15},
  });
}

int main(int argc, char * * argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

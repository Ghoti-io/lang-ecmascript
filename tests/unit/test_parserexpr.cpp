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

// Expressions: the tree each form parses to, through the public walker, and
// the refusals of the grammar.

#include <gtest/gtest.h>

#include "../parse_test_support.h"

using glecma_test::Parse;

namespace {

struct Case {
  const char * source;
  const char * tree;  // the statements of the program, rendered
};

void Expect(const Case & c, bool strict = false) {
  auto parsed = Parse(c.source, strict);
  ASSERT_EQ(parsed.result, GLECMA_OK)
      << c.source << ": " << (parsed.failure.message ? parsed.failure.message : "");
  EXPECT_EQ(parsed.Body(), c.tree) << c.source;
}

struct Refusal {
  const char * source;
  uint32_t rule;
  size_t offset;  // where the failure is
};

void ExpectRefused(const Refusal & r, bool strict = false) {
  auto parsed = Parse(r.source, strict);
  ASSERT_EQ(parsed.result, GLECMA_ERR_SYNTAX) << r.source;
  EXPECT_EQ(parsed.failure.rule, r.rule)
      << r.source << ": " << glecma_rule_name(parsed.failure.rule);
  EXPECT_EQ(parsed.failure.location.offset, r.offset) << r.source;
  EXPECT_EQ(parsed.tree, nullptr);
}

}  // namespace

TEST(ParseExpressions, PrimaryExpressions) {
  const Case cases[] = {
    {"a;", "(Expression (Identifier a))"},
    {"this;", "(Expression (This))"},
    {"null;", "(Expression (NullLiteral))"},
    {"true; false;", "(Expression (BooleanLiteral/true)) (Expression (BooleanLiteral))"},
    {"1; 1n; 0x1F;", "(Expression (NumberLiteral)) (Expression (BigIntLiteral)) (Expression (NumberLiteral))"},
    {"1; 'a\\n';", "(Expression (NumberLiteral)) (Expression (StringLiteral \"a\\u{A}\"))"},
    {"(a);", "(Expression (Identifier a))"},
    {"(a, b);", "(Expression (Sequence (Identifier a) (Identifier b)))"},
    {"/a+/gi;", "(Expression (RegExpLiteral \"a+\" \"gi\"))"},
    {"a / b / c;", "(Expression (Binary / (Binary / (Identifier a) (Identifier b)) (Identifier c)))"},
    {"`a${b}c`;", "(Expression (TemplateLiteral (TemplateElement \"a\" \"a\") (Identifier b) (TemplateElement \"c\" \"c\")))"},
    {"`${a}${b}`;", "(Expression (TemplateLiteral (TemplateElement \"\" \"\") (Identifier a) (TemplateElement \"\" \"\") (Identifier b) (TemplateElement \"\" \"\")))"},
    {"[];", "(Expression (ArrayLiteral))"},
    {"[1, , 2, ...a,];", "(Expression (ArrayLiteral (NumberLiteral) _ (NumberLiteral) (SpreadElement (Identifier a))))"},
    {"({});", "(Expression (ObjectLiteral))"},
    {"({a, b: 1, [c]: 2, 'd': 3, 4: 5, ...e});",
        "(Expression (ObjectLiteral (Property/shorthand (Identifier a) (Identifier a)) "
        "(Property (Identifier b) (NumberLiteral)) (Property/computed (Identifier c) (NumberLiteral)) "
        "(Property (StringLiteral \"d\") (NumberLiteral)) (Property (NumberLiteral) (NumberLiteral)) "
        "(SpreadElement (Identifier e))))"},
  };
  for (const Case & c : cases) {
    Expect(c);
  }
}

TEST(ParseExpressions, DivisionAndRegularExpressionsAreToldApartByTheGrammar) {
  const Case cases[] = {
    {"a = b / c / d;", "(Expression (Assignment = (Identifier a) (Binary / (Binary / (Identifier b) (Identifier c)) (Identifier d))))"},
    {"a = /b/ / c;", "(Expression (Assignment = (Identifier a) (Binary / (RegExpLiteral \"b\" \"\") (Identifier c))))"},
    {"(a) / 2;", "(Expression (Binary / (Identifier a) (NumberLiteral)))"},
    {"x = {} / 2;", "(Expression (Assignment = (Identifier x) (Binary / (ObjectLiteral) (NumberLiteral))))"},
    {"if (a) /b/.test(c);",
        "(If (Identifier a) (Expression (Call (Member (RegExpLiteral \"b\" \"\") (Identifier test)) (Identifier c))) _)"},
    {"a++ / 2;", "(Expression (Binary / (Update ++ (Identifier a)) (NumberLiteral)))"},
    {"/=/;", "(Expression (RegExpLiteral \"=\" \"\"))"},
    {"a /= 2;", "(Expression (Assignment /= (Identifier a) (NumberLiteral)))"},
    {"`${a / b}`;", "(Expression (TemplateLiteral (TemplateElement \"\" \"\") (Binary / (Identifier a) (Identifier b)) (TemplateElement \"\" \"\")))"},
    {"`${`${a}`}`;", "(Expression (TemplateLiteral (TemplateElement \"\" \"\") (TemplateLiteral (TemplateElement \"\" \"\") (Identifier a) (TemplateElement \"\" \"\")) (TemplateElement \"\" \"\")))"},
  };
  for (const Case & c : cases) {
    Expect(c);
  }
}

TEST(ParseExpressions, OperatorsAndTheirPrecedence) {
  const Case cases[] = {
    {"a + b * c;", "(Expression (Binary + (Identifier a) (Binary * (Identifier b) (Identifier c))))"},
    {"a * b + c;", "(Expression (Binary + (Binary * (Identifier a) (Identifier b)) (Identifier c)))"},
    {"a - b - c;", "(Expression (Binary - (Binary - (Identifier a) (Identifier b)) (Identifier c)))"},
    {"a ** b ** c;", "(Expression (Binary ** (Identifier a) (Binary ** (Identifier b) (Identifier c))))"},
    {"(-a) ** b;", "(Expression (Binary ** (Unary - (Identifier a)) (Identifier b)))"},
    {"a ?? b;", "(Expression (Logical ?? (Identifier a) (Identifier b)))"},
    {"a || b && c;", "(Expression (Logical || (Identifier a) (Logical && (Identifier b) (Identifier c))))"},
    {"(a || b) ?? c;", "(Expression (Logical ?? (Logical || (Identifier a) (Identifier b)) (Identifier c)))"},
    {"a ?? b | c;", "(Expression (Logical ?? (Identifier a) (Binary | (Identifier b) (Identifier c))))"},
    {"a < b == c > d;", "(Expression (Binary == (Binary < (Identifier a) (Identifier b)) (Binary > (Identifier c) (Identifier d))))"},
    {"a << 1 >>> 2 >> 3;", "(Expression (Binary >> (Binary >>> (Binary << (Identifier a) (NumberLiteral)) (NumberLiteral)) (NumberLiteral)))"},
    {"a in b instanceof c;", "(Expression (Binary instanceof (Binary in (Identifier a) (Identifier b)) (Identifier c)))"},
    {"a & b ^ c | d;", "(Expression (Binary | (Binary ^ (Binary & (Identifier a) (Identifier b)) (Identifier c)) (Identifier d)))"},
    {"!a; ~b; typeof c; void d; delete e.f; +g; -h;",
        "(Expression (Unary ! (Identifier a))) (Expression (Unary ~ (Identifier b))) "
        "(Expression (Unary typeof (Identifier c))) (Expression (Unary void (Identifier d))) "
        "(Expression (Unary delete (Member (Identifier e) (Identifier f)))) "
        "(Expression (Unary + (Identifier g))) (Expression (Unary - (Identifier h)))"},
    {"++a; --a; a++; a--;",
        "(Expression (Update/prefix ++ (Identifier a))) (Expression (Update/prefix -- (Identifier a))) "
        "(Expression (Update ++ (Identifier a))) (Expression (Update -- (Identifier a)))"},
    {"a ? b : c ? d : e;",
        "(Expression (Conditional (Identifier a) (Identifier b) (Conditional (Identifier c) (Identifier d) (Identifier e))))"},
    {"a = b = c;", "(Expression (Assignment = (Identifier a) (Assignment = (Identifier b) (Identifier c))))"},
    {"a += 1; a **= 2; a >>>= 3; a &&= 4; a ||= 5; a ?\?= 6;",
        "(Expression (Assignment += (Identifier a) (NumberLiteral))) (Expression (Assignment **= (Identifier a) (NumberLiteral))) "
        "(Expression (Assignment >>>= (Identifier a) (NumberLiteral))) (Expression (Assignment &&= (Identifier a) (NumberLiteral))) "
        "(Expression (Assignment ||= (Identifier a) (NumberLiteral))) (Expression (Assignment ?\?= (Identifier a) (NumberLiteral)))"},
    {"a, b, c;", "(Expression (Sequence (Identifier a) (Identifier b) (Identifier c)))"},
    {"#x in o;", nullptr},  // refused outside a class by a later check; the grammar accepts it
  };
  for (const Case & c : cases) {
    if (c.tree == nullptr) {
      continue;
    }
    Expect(c);
  }
}

TEST(ParseExpressions, MembersCallsNewAndOptionalChains) {
  const Case cases[] = {
    {"a.b.c;", "(Expression (Member (Member (Identifier a) (Identifier b)) (Identifier c)))"},
    {"a[b][0];", "(Expression (Member/computed (Member/computed (Identifier a) (Identifier b)) (NumberLiteral)))"},
    {"a.if.class;", "(Expression (Member (Member (Identifier a) (Identifier if)) (Identifier class)))"},
    {"a(b)(c, ...d);", "(Expression (Call (Call (Identifier a) (Identifier b)) (Identifier c) (SpreadElement (Identifier d))))"},
    {"a(b,);", "(Expression (Call (Identifier a) (Identifier b)))"},
    {"new A;", "(Expression (New (Identifier A)))"},
    {"new A();", "(Expression (New (Identifier A)))"},
    {"new A(b).c;", "(Expression (Member (New (Identifier A) (Identifier b)) (Identifier c)))"},
    {"new A.b(c);", "(Expression (New (Member (Identifier A) (Identifier b)) (Identifier c)))"},
    {"new new A()();", "(Expression (New (New (Identifier A))))"},
    {"new (a())();", "(Expression (New (Call (Identifier a))))"},
    {"new a`x`;", "(Expression (New (TaggedTemplate (Identifier a) (TemplateLiteral (TemplateElement \"x\" \"x\")))))"},
    {"a?.b;", "(Expression (OptionalChain (Member/opt (Identifier a) (Identifier b))))"},
    {"a?.[b];", "(Expression (OptionalChain (Member/computed/opt (Identifier a) (Identifier b))))"},
    {"a?.(b);", "(Expression (OptionalChain (Call/opt (Identifier a) (Identifier b))))"},
    {"a?.b.c(d);",
        "(Expression (OptionalChain (Call (Member (Member/opt (Identifier a) (Identifier b)) (Identifier c)) (Identifier d))))"},
    {"(a?.b).c;", "(Expression (Member (OptionalChain (Member/opt (Identifier a) (Identifier b))) (Identifier c)))"},
    {"a?.b?.c;", "(Expression (OptionalChain (Member/opt (Member/opt (Identifier a) (Identifier b)) (Identifier c))))"},
    {"a ?.5 : 1;", nullptr},
    {"a?.5:1;", "(Expression (Conditional (Identifier a) (NumberLiteral) (NumberLiteral)))"},
    {"a`x${b}`;", "(Expression (TaggedTemplate (Identifier a) (TemplateLiteral (TemplateElement \"x\" \"x\") (Identifier b) (TemplateElement \"\" \"\"))))"},
    {"a.b`c`;", "(Expression (TaggedTemplate (Member (Identifier a) (Identifier b)) (TemplateLiteral (TemplateElement \"c\" \"c\"))))"},
    {"import('a');", "(Expression (ImportCall (StringLiteral \"a\") _))"},
    {"import('a', b);", "(Expression (ImportCall (StringLiteral \"a\") (Identifier b)))"},
    {"import('a', b,);", "(Expression (ImportCall (StringLiteral \"a\") (Identifier b)))"},
    {"import('a').then(b);", "(Expression (Call (Member (ImportCall (StringLiteral \"a\") _) (Identifier then)) (Identifier b)))"},
    {"new.target;", "(Expression (MetaProperty new.target))"},
    {"async\n(a);", "(Expression (Call (Identifier async) (Identifier a)))"},
  };
  for (const Case & c : cases) {
    if (c.tree == nullptr) {
      continue;
    }
    Expect(c);
  }
}

TEST(ParseExpressions, ArrowFunctionsAndTheirCoverGrammar) {
  const Case cases[] = {
    {"a => a;", "(Expression (ArrowFunction/exprbody _ (ParameterList (Identifier a)) (Identifier a)))"},
    {"() => 1;", "(Expression (ArrowFunction/exprbody _ (ParameterList) (NumberLiteral)))"},
    {"(a, b) => a;", "(Expression (ArrowFunction/exprbody _ (ParameterList (Identifier a) (Identifier b)) (Identifier a)))"},
    {"(a, b,) => a;", "(Expression (ArrowFunction/exprbody _ (ParameterList (Identifier a) (Identifier b)) (Identifier a)))"},
    {"(a = 1, ...b) => a;",
        "(Expression (ArrowFunction/exprbody _ (ParameterList (AssignmentPattern (Identifier a) (NumberLiteral)) "
        "(RestElement (Identifier b))) (Identifier a)))"},
    {"({a, b: [c]}) => c;",
        "(Expression (ArrowFunction/exprbody _ (ParameterList (ObjectPattern (PatternProperty/shorthand (Identifier a) (Identifier a)) "
        "(PatternProperty (Identifier b) (ArrayPattern (Identifier c))))) (Identifier c)))"},
    {"({a = 1}) => a;",
        "(Expression (ArrowFunction/exprbody _ (ParameterList (ObjectPattern (PatternProperty/shorthand (Identifier a) "
        "(AssignmentPattern (Identifier a) (NumberLiteral))))) (Identifier a)))"},
    {"x => y => x;",
        "(Expression (ArrowFunction/exprbody _ (ParameterList (Identifier x)) (ArrowFunction/exprbody _ (ParameterList (Identifier y)) (Identifier x))))"},
    {"a => { return a; };", "(Expression (ArrowFunction _ (ParameterList (Identifier a)) (FunctionBody (Return (Identifier a)))))"},
    {"x = a => a;", "(Expression (Assignment = (Identifier x) (ArrowFunction/exprbody _ (ParameterList (Identifier a)) (Identifier a))))"},
    {"a ? b => 1 : c => 2;",
        "(Expression (Conditional (Identifier a) (ArrowFunction/exprbody _ (ParameterList (Identifier b)) (NumberLiteral)) "
        "(ArrowFunction/exprbody _ (ParameterList (Identifier c)) (NumberLiteral))))"},
    {"async a => a;", "(Expression (ArrowFunction/async/exprbody _ (ParameterList (Identifier a)) (Identifier a)))"},
    {"async (a, b) => a;", "(Expression (ArrowFunction/async/exprbody _ (ParameterList (Identifier a) (Identifier b)) (Identifier a)))"},
    {"async () => await a;", nullptr},
    {"async(a, b);", "(Expression (Call (Identifier async) (Identifier a) (Identifier b)))"},
    {"(x => 1)(2);", "(Expression (Call (ArrowFunction/exprbody _ (ParameterList (Identifier x)) (NumberLiteral)) (NumberLiteral)))"},
    {"(a = (b = (c) => 1) => 2) => 3;", nullptr},
    {"((a)) ;", "(Expression (Identifier a))"},
  };
  for (const Case & c : cases) {
    if (c.tree == nullptr) {
      continue;
    }
    Expect(c);
  }
  // An arrow function inside the parameters of another is read once for the
  // outer group and the decision is reused.
  auto nested = Parse("(a = (b = (c) => 1) => 2) => 3;");
  ASSERT_EQ(nested.result, GLECMA_OK);
  EXPECT_EQ(nested.Body(),
      "(Expression (ArrowFunction/exprbody _ (ParameterList (AssignmentPattern (Identifier a) "
      "(ArrowFunction/exprbody _ (ParameterList (AssignmentPattern (Identifier b) "
      "(ArrowFunction/exprbody _ (ParameterList (Identifier c)) (NumberLiteral)))) (NumberLiteral)))) (NumberLiteral)))");
}

TEST(ParseExpressions, DestructuringAssignmentIsResolvedFromTheCoverGrammar) {
  const Case cases[] = {
    {"[a, b] = c;",
        "(Expression (Assignment = (ArrayPattern (Identifier a) (Identifier b)) (Identifier c)))"},
    {"[a = 1, , ...b] = c;",
        "(Expression (Assignment = (ArrayPattern (AssignmentPattern (Identifier a) (NumberLiteral)) _ (RestElement (Identifier b))) (Identifier c)))"},
    {"[a.b, c[0]] = d;",
        "(Expression (Assignment = (ArrayPattern (Member (Identifier a) (Identifier b)) (Member/computed (Identifier c) (NumberLiteral))) (Identifier d)))"},
    {"({a, b: c, d = 1, [e]: f, ...g} = h);",
        "(Expression (Assignment = (ObjectPattern (PatternProperty/shorthand (Identifier a) (Identifier a)) "
        "(PatternProperty (Identifier b) (Identifier c)) (PatternProperty/shorthand (Identifier d) (AssignmentPattern (Identifier d) (NumberLiteral))) "
        "(PatternProperty/computed (Identifier e) (Identifier f)) (RestElement (Identifier g))) (Identifier h)))"},
    {"[[a], {b}] = c;",
        "(Expression (Assignment = (ArrayPattern (ArrayPattern (Identifier a)) (ObjectPattern (PatternProperty/shorthand (Identifier b) (Identifier b)))) (Identifier c)))"},
    {"[{}[a]] = b;",
        "(Expression (Assignment = (ArrayPattern (Member/computed (ObjectLiteral) (Identifier a))) (Identifier b)))"},
    {"[(a), (b.c)] = d;",
        "(Expression (Assignment = (ArrayPattern (Identifier a) (Member (Identifier b) (Identifier c))) (Identifier d)))"},
    {"[a = {b = 1}] = c;", nullptr},
    {"[{a = 1}] = b;",
        "(Expression (Assignment = (ArrayPattern (ObjectPattern (PatternProperty/shorthand (Identifier a) "
        "(AssignmentPattern (Identifier a) (NumberLiteral))))) (Identifier b)))"},
    {"x = [a] = b;", "(Expression (Assignment = (Identifier x) (Assignment = (ArrayPattern (Identifier a)) (Identifier b))))"},
    {"for ([a, b] of c);",
        "(ForOf (ArrayPattern (Identifier a) (Identifier b)) (Identifier c) (Empty))"},
    {"for ({a = 1} of c);",
        "(ForOf (ObjectPattern (PatternProperty/shorthand (Identifier a) (AssignmentPattern (Identifier a) (NumberLiteral)))) (Identifier c) (Empty))"},
    {"for ({}[a] of c);", "(ForOf (Member/computed (ObjectLiteral) (Identifier a)) (Identifier c) (Empty))"},
  };
  for (const Case & c : cases) {
    if (c.tree == nullptr) {
      continue;
    }
    Expect(c);
  }
}

TEST(ParseExpressions, WebCompatibilityCallTargetsAreSloppyOnly) {
  // Annex B.3.9: a call is an assignment target in sloppy code (the
  // assignment throws when it runs), and an error in strict code and in the
  // logical assignments and the destructuring targets.
  for (const char * source : {"f() = 1;", "f() += 1;", "f()++;", "--f();", "for (f() in a);", "for (f() of a);"}) {
    EXPECT_EQ(Parse(source).result, GLECMA_OK) << source;
    EXPECT_EQ(Parse(source, true).result, GLECMA_ERR_SYNTAX) << source;
  }
  for (const char * source : {"f() &&= 1;", "f() ||= 1;", "f() ?\?= 1;", "[f()] = a;", "({a: f()} = b);"}) {
    EXPECT_EQ(Parse(source).result, GLECMA_ERR_SYNTAX) << source;
  }
}

TEST(ParseExpressions, YieldAwaitAndTheirContexts) {
  const Case cases[] = {
    {"function* g() { yield; yield 1; yield* a; x = yield; (yield); [yield]; }", nullptr},
    {"var yield = 1; yield;", "(VariableDeclaration/var (VariableDeclarator (Identifier yield) (NumberLiteral))) (Expression (Identifier yield))"},
    {"var await = 1; await;", "(VariableDeclaration/var (VariableDeclarator (Identifier await) (NumberLiteral))) (Expression (Identifier await))"},
    {"async function f() { await a; await await b; }", nullptr},
  };
  for (const Case & c : cases) {
    if (c.tree == nullptr) {
      EXPECT_EQ(Parse(c.source).result, GLECMA_OK) << c.source;
      continue;
    }
    Expect(c);
  }
  EXPECT_EQ(Parse("function* g() { yield\n* 2; }").result, GLECMA_ERR_SYNTAX);
  EXPECT_EQ(Parse("function* g() { var yield; }").result, GLECMA_ERR_SYNTAX);
  EXPECT_EQ(Parse("async function f() { var await; }").result, GLECMA_ERR_SYNTAX);
  EXPECT_EQ(Parse("function f() { await a; }").result, GLECMA_ERR_SYNTAX);
  EXPECT_EQ(Parse("function g() { yield 1; }").result, GLECMA_ERR_SYNTAX);
  auto y = Parse("function* g() { yield* a; yield b; yield; }");
  ASSERT_EQ(y.result, GLECMA_OK);
  EXPECT_EQ(y.Body(),
      "(FunctionDeclaration/gen (Identifier g) (ParameterList) (FunctionBody (Expression (Yield/delegate (Identifier a))) "
      "(Expression (Yield (Identifier b))) (Expression (Yield _))))");
}

TEST(ParseExpressions, GrammarRefusalsNameTheirRuleAndPlace) {
  const Refusal refusals[] = {
    {"a +;", GLECMA_RULE_UNEXPECTED_TOKEN, 3},
    {"a +", GLECMA_RULE_UNEXPECTED_END, 3},
    {"(a", GLECMA_RULE_UNEXPECTED_END, 2},
    {"()", GLECMA_RULE_UNEXPECTED_TOKEN, 0},
    {"(a,)", GLECMA_RULE_UNEXPECTED_TOKEN, 3},
    {"(...a)", GLECMA_RULE_UNEXPECTED_TOKEN, 1},
    {"a = 1 = 2", GLECMA_RULE_INVALID_ASSIGNMENT_TARGET, 6},
    {"1 = 2", GLECMA_RULE_INVALID_ASSIGNMENT_TARGET, 2},
    {"a?.b = 1", GLECMA_RULE_INVALID_ASSIGNMENT_TARGET, 5},
    {"({a}) = 1", GLECMA_RULE_INVALID_ASSIGNMENT_TARGET, 6},
    {"[a] += 1", GLECMA_RULE_INVALID_ASSIGNMENT_TARGET, 4},
    {"++1", GLECMA_RULE_INVALID_UPDATE_TARGET, 0},
    {"1++", GLECMA_RULE_INVALID_UPDATE_TARGET, 1},
    {"({a = 1});", GLECMA_RULE_COVER_INITIALIZED_NAME, 4},
    {"f({a = 1});", GLECMA_RULE_COVER_INITIALIZED_NAME, 5},
    {"[{a = 1}.b];", GLECMA_RULE_COVER_INITIALIZED_NAME, 4},
    {"[...a, b] = c;", GLECMA_RULE_REST_ELEMENT_POSITION, 5},
    {"[...a,] = c;", GLECMA_RULE_REST_ELEMENT_POSITION, 5},
    {"({...a, b} = c);", GLECMA_RULE_REST_ELEMENT_POSITION, 6},
    {"({...{a}} = c);", GLECMA_RULE_INVALID_ASSIGNMENT_TARGET, 5},
    {"-a ** b;", GLECMA_RULE_EXPONENT_UNARY_OPERAND, 3},
    {"typeof a ** b;", GLECMA_RULE_EXPONENT_UNARY_OPERAND, 9},
    {"a ?? b || c;", GLECMA_RULE_NULLISH_MIXED, 2},
    {"a || b ?? c;", GLECMA_RULE_NULLISH_MIXED, 7},
    {"a && b ?? c;", GLECMA_RULE_NULLISH_MIXED, 7},
    {"a?.b`c`;", GLECMA_RULE_OPTIONAL_CHAIN_TEMPLATE, 4},
    {"a?.`c`;", GLECMA_RULE_OPTIONAL_CHAIN_TEMPLATE, 3},
    {"new a?.b();", GLECMA_RULE_OPTIONAL_CHAIN_NEW, 5},
    {"super;", GLECMA_RULE_INVALID_SUPER, 5},
    {"new super();", GLECMA_RULE_INVALID_SUPER, 9},
    {"super?.a;", GLECMA_RULE_INVALID_SUPER, 5},
    {"import.meta;", GLECMA_RULE_IMPORT_META_OUTSIDE_MODULE, 6},
    {"import();", GLECMA_RULE_UNEXPECTED_TOKEN, 7},
    {"import(a, b, c);", GLECMA_RULE_IMPORT_CALL_ARGUMENTS, 13},
    {"import a;", GLECMA_RULE_UNEXPECTED_TOKEN, 0},
    {"a\n=> 1;", GLECMA_RULE_ARROW_LINE_TERMINATOR, 2},
    {"(a)\n=> 1;", GLECMA_RULE_ARROW_LINE_TERMINATOR, 4},
    {"async (a)\n=> 1;", GLECMA_RULE_ARROW_LINE_TERMINATOR, 10},
    {"a + b => 1;", GLECMA_RULE_MISSING_SEMICOLON, 6},
    {"a ? b => 1;", GLECMA_RULE_UNEXPECTED_TOKEN, 10},
    {"x => {} + 1;", GLECMA_RULE_MISSING_SEMICOLON, 8},
    {"(a, b) => {}()", GLECMA_RULE_MISSING_SEMICOLON, 12},
    {"super.#b;", GLECMA_RULE_UNEXPECTED_TOKEN, 6},
    {"({#a: 1});", GLECMA_RULE_UNEXPECTED_TOKEN, 2},
    {"`${a`;", GLECMA_RULE_UNTERMINATED_TEMPLATE, 4},
    {"`\\01`;", GLECMA_RULE_TEMPLATE_ESCAPE_OCTAL, 1},
    {"x = if;", GLECMA_RULE_RESERVED_WORD, 4},
    {"x = class;", GLECMA_RULE_UNEXPECTED_TOKEN, 9},
  };
  for (const Refusal & r : refusals) {
    ExpectRefused(r);
  }
}

TEST(ParseExpressions, ATaggedTemplateKeepsAnInvalidEscapeAndAnUntaggedOneRefusesIt) {
  auto tagged = Parse("a`\\u{`;");
  ASSERT_EQ(tagged.result, GLECMA_OK);
  EXPECT_EQ(tagged.Body(),
      "(Expression (TaggedTemplate (Identifier a) (TemplateLiteral (TemplateElement/nocooked \"\" \"\\\\u{\"))))");
  auto plain = Parse("`\\u{`;");
  ASSERT_EQ(plain.result, GLECMA_ERR_SYNTAX);
  EXPECT_EQ(plain.failure.rule, static_cast<uint32_t>(GLECMA_RULE_ESCAPE_CODE_POINT));
}

TEST(ParseExpressions, ExpressionsInStrictCode) {
  EXPECT_EQ(Parse("a = 1;", true).result, GLECMA_OK);
  EXPECT_EQ(Parse("var let;", true).result, GLECMA_ERR_SYNTAX);
  EXPECT_EQ(Parse("yield;", true).result, GLECMA_ERR_SYNTAX);
  EXPECT_EQ(Parse("010;", true).result, GLECMA_ERR_SYNTAX);
  EXPECT_EQ(Parse("'\\07';", true).result, GLECMA_ERR_SYNTAX);
  EXPECT_EQ(Parse("010;", false).result, GLECMA_OK);
}

int main(int argc, char * * argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

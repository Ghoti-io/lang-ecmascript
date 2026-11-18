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

// Early errors (clause 5.1.4 and the "Static Semantics: Early Errors" clauses):
// the errors the grammar does not itself decide. Every case is parsed with the
// tree builder and with the checking builder, which builds no function body,
// and must be accepted or refused for the same rule at the same place by both.

#include <gtest/gtest.h>

#include "../parse_test_support.h"

using glecma_test::Parse;

namespace {

enum Mode { kBoth, kSloppy, kStrict };

struct Case {
  const char * source;
  uint32_t rule;  // 0 for accepted
  Mode mode;
};

// One parse with each builder; the two must agree.
void Check(const char * source, uint32_t rule, bool strict) {
  auto tree = Parse(source, strict);
  auto checked = Parse(source, strict, GLECMA_PARSE_INTERNAL_CHECKING);
  const char * where = strict ? " [strict] " : " [sloppy] ";
  if (rule == 0) {
    EXPECT_EQ(tree.result, GLECMA_OK) << source << where << glecma_rule_name(tree.failure.rule);
    EXPECT_EQ(checked.result, GLECMA_OK) << source << where << glecma_rule_name(checked.failure.rule);
    return;
  }
  EXPECT_EQ(tree.result, GLECMA_ERR_SYNTAX) << source << where;
  EXPECT_EQ(tree.failure.rule, rule)
      << source << where << glecma_rule_name(tree.failure.rule);
  EXPECT_EQ(checked.result, tree.result) << source << where;
  EXPECT_EQ(checked.failure.rule, tree.failure.rule) << source << where;
  EXPECT_EQ(checked.failure.location.offset, tree.failure.location.offset)
      << source << where;
}

void RunCases(std::initializer_list<Case> cases) {
  for (const Case & c : cases) {
    if (c.mode != kStrict) {
      Check(c.source, c.rule, false);
    }
    if (c.mode != kSloppy) {
      Check(c.source, c.rule, true);
    }
  }
}

}  // namespace

// The rules this file's cases exercise, each with the clause its code names.
TEST(EarlyErrors, EveryRuleNamesItsClause) {
  EXPECT_STREQ(glecma_rule_clause(GLECMA_RULE_BINDING_EVAL_ARGUMENTS), "13.1.1");
  EXPECT_STREQ(glecma_rule_clause(GLECMA_RULE_RETURN_POSITION), "14.10");
  EXPECT_STREQ(glecma_rule_clause(GLECMA_RULE_SUPER_PROPERTY_POSITION), "16.1.1");
  EXPECT_STREQ(glecma_rule_clause(GLECMA_RULE_SUPER_CALL_POSITION), "16.1.1");
  EXPECT_STREQ(glecma_rule_clause(GLECMA_RULE_NEW_TARGET_POSITION), "16.1.1");
  EXPECT_STREQ(glecma_rule_clause(GLECMA_RULE_ARGUMENTS_IN_INITIALIZER), "15.7.1");
  EXPECT_STREQ(glecma_rule_clause(GLECMA_RULE_YIELD_IN_PARAMETERS), "15.5.1");
  EXPECT_STREQ(glecma_rule_clause(GLECMA_RULE_AWAIT_IN_PARAMETERS), "15.8.1");
  EXPECT_STREQ(glecma_rule_clause(GLECMA_RULE_DELETE_IDENTIFIER_STRICT), "13.5.1.1");
  EXPECT_STREQ(glecma_rule_clause(GLECMA_RULE_DELETE_PRIVATE_REFERENCE), "13.5.1.1");
  EXPECT_STREQ(glecma_rule_clause(GLECMA_RULE_WITH_STRICT), "14.11.1");
  EXPECT_STREQ(glecma_rule_clause(GLECMA_RULE_DUPLICATE_PROTO), "13.2.5.1");
  EXPECT_STREQ(glecma_rule_clause(GLECMA_RULE_CONST_WITHOUT_INITIALIZER), "14.3.1.1");
  EXPECT_STREQ(glecma_rule_clause(GLECMA_RULE_LET_BOUND_NAME), "14.3.1.1");
  EXPECT_STREQ(glecma_rule_clause(GLECMA_RULE_USE_STRICT_NON_SIMPLE_PARAMETERS), "15.2.1");
  EXPECT_STREQ(glecma_rule_clause(GLECMA_RULE_LABEL_REDECLARED), "14.13.1");
  EXPECT_STREQ(glecma_rule_clause(GLECMA_RULE_BREAK_TARGET), "14.9.1");
  EXPECT_STREQ(glecma_rule_clause(GLECMA_RULE_CONTINUE_TARGET), "14.8.1");
  EXPECT_STREQ(glecma_rule_clause(GLECMA_RULE_LABELLED_FUNCTION_BODY), "14.13.1");
  EXPECT_STREQ(glecma_rule_clause(GLECMA_RULE_CLASS_CONSTRUCTOR_DUPLICATE), "15.7.1");
  EXPECT_STREQ(glecma_rule_clause(GLECMA_RULE_CLASS_CONSTRUCTOR_KIND), "15.7.1");
  EXPECT_STREQ(glecma_rule_clause(GLECMA_RULE_CLASS_STATIC_PROTOTYPE), "15.7.1");
  EXPECT_STREQ(glecma_rule_clause(GLECMA_RULE_PRIVATE_NAME_CONSTRUCTOR), "15.7.1");
  EXPECT_STREQ(glecma_rule_clause(GLECMA_RULE_PRIVATE_NAME_DUPLICATE), "15.7.1");
  EXPECT_STREQ(glecma_rule_clause(GLECMA_RULE_PRIVATE_NAME_UNDECLARED), "15.7.1");
  EXPECT_STREQ(glecma_rule_clause(GLECMA_RULE_BLOCK_REDECLARATION), "14.2.1");
  EXPECT_STREQ(glecma_rule_clause(GLECMA_RULE_SWITCH_REDECLARATION), "14.12.1");
  EXPECT_STREQ(glecma_rule_clause(GLECMA_RULE_SCRIPT_REDECLARATION), "16.1.1");
  EXPECT_STREQ(glecma_rule_clause(GLECMA_RULE_FUNCTION_REDECLARATION), "15.2.1");
  EXPECT_STREQ(glecma_rule_clause(GLECMA_RULE_CATCH_REDECLARATION), "14.15.1");
  EXPECT_STREQ(glecma_rule_clause(GLECMA_RULE_FOR_REDECLARATION), "14.7.5.1");
  EXPECT_STREQ(glecma_rule_clause(GLECMA_RULE_STATIC_BLOCK_REDECLARATION), "15.7.1");
  EXPECT_STREQ(glecma_rule_clause(GLECMA_RULE_DUPLICATE_PARAMETER), "15.1.1");
}

// Clause 13.1.1: `eval` and `arguments` are not bound, and not assigned, in strict code.
TEST(EarlyErrors, EvalAndArgumentsInStrictCode) {
  const uint32_t bind = GLECMA_RULE_BINDING_EVAL_ARGUMENTS;
  const uint32_t assign = GLECMA_RULE_INVALID_ASSIGNMENT_TARGET;
  RunCases({
    {"var eval;", bind, kStrict},
    {"var eval;", 0, kSloppy},
    {"let arguments;", bind, kStrict},
    {"function eval() {}", bind, kStrict},
    {"function eval() {}", 0, kSloppy},
    {"function f(arguments) {}", bind, kStrict},
    {"function f(a, {b: eval}) {}", bind, kStrict},
    {"(eval) => 1", bind, kStrict},
    {"eval => 1", bind, kStrict},
    {"async arguments => 1", bind, kStrict},
    {"try {} catch (eval) {}", bind, kStrict},
    {"class eval {}", bind, kBoth},
    {"var {eval} = {};", bind, kStrict},
    // A directive turns the name and the parameters read before it into errors.
    {"function f(eval) { 'use strict'; }", bind, kSloppy},
    {"function eval() { 'use strict'; }", bind, kSloppy},
    {"(function (a, arguments) { 'use strict'; })", bind, kSloppy},
    // The name is not a binding when it is a property.
    {"({eval: 1, arguments: 2}); a.eval; a.arguments;", 0, kBoth},
    // Assignment targets.
    {"eval = 1;", assign, kStrict},
    {"eval = 1;", 0, kSloppy},
    {"arguments += 1;", assign, kStrict},
    {"(eval) = 1;", assign, kStrict},
    {"eval ||= 1;", assign, kStrict},
    {"[eval] = [];", assign, kStrict},
    {"({eval} = {});", assign, kStrict},
    {"({a: arguments} = {});", assign, kStrict},
    {"eval++;", GLECMA_RULE_INVALID_UPDATE_TARGET, kStrict},
    {"--arguments;", GLECMA_RULE_INVALID_UPDATE_TARGET, kStrict},
    {"for (eval in {}) ;", GLECMA_RULE_INVALID_FOR_TARGET, kStrict},
    {"for (arguments of []) ;", GLECMA_RULE_INVALID_FOR_TARGET, kStrict},
    {"eval++; for (eval in {}) ; [eval] = [];", 0, kSloppy},
    {"a.eval = 1; eval.x = 1; eval[0]++;", 0, kBoth},
  });
}

// Clause 14.10: a `return` belongs to a function body.
TEST(EarlyErrors, ReturnOutsideAFunction) {
  const uint32_t r = GLECMA_RULE_RETURN_POSITION;
  RunCases({
    {"return;", r, kBoth},
    {"{ return 1; }", r, kBoth},
    {"if (a) return;", r, kBoth},
    {"class C { static { return; } }", r, kBoth},
    {"class C { static { (function () { return; }); } }", 0, kBoth},
    {"class C { x = () => { return 1; }; }", 0, kBoth},
    {"function f() { return; }", 0, kBoth},
    {"() => { return 1; };", 0, kBoth},
    {"({ m() { return; } });", 0, kBoth},
    {"async function f() { return await 1; }", 0, kBoth},
  });
}

// Clauses 16.1.1, 15.2.1, 15.4.1, 15.7.1: where `super` and `new.target` may appear.
TEST(EarlyErrors, SuperAndNewTarget) {
  const uint32_t prop = GLECMA_RULE_SUPER_PROPERTY_POSITION;
  const uint32_t call = GLECMA_RULE_SUPER_CALL_POSITION;
  const uint32_t target = GLECMA_RULE_NEW_TARGET_POSITION;
  RunCases({
    {"super.x;", prop, kBoth},
    {"super[0];", prop, kBoth},
    {"function f() { super.x; }", prop, kBoth},
    {"(function () { super.x; });", prop, kBoth},
    {"() => super.x;", prop, kBoth},
    {"({ m() { super.x; } });", 0, kBoth},
    {"({ get a() { return super.x; }, set a(v) { super[v]; } });", 0, kBoth},
    {"({ m() { () => super.x; } });", 0, kBoth},
    {"({ m() { function f() { super.x; } } });", prop, kBoth},
    {"({ m: function () { super.x; } });", prop, kBoth},
    {"class C { m() { super.x; } }", 0, kBoth},
    {"class C { static m() { super.x; } }", 0, kBoth},
    {"class C { x = super.y; }", 0, kBoth},
    {"class C { static x = () => super.y; }", 0, kBoth},
    {"class C { static { super.x; } }", 0, kBoth},
    {"class C extends (super.x) {}", prop, kBoth},
    {"class C { [super.x]() {} }", prop, kBoth},
    {"({ m() { class C extends super.x {} } });", 0, kBoth},
    // super()
    {"super();", call, kBoth},
    {"function f() { super(); }", call, kBoth},
    {"({ m() { super(); } });", call, kBoth},
    {"class C { constructor() { super(); } }", call, kBoth},
    {"class C extends B { constructor() { super(); } }", 0, kBoth},
    {"class C extends B { constructor() { () => super(); } }", 0, kBoth},
    {"class C extends B { constructor() { function f() { super(); } } }", call, kBoth},
    {"class C extends B { m() { super(); } }", call, kBoth},
    {"class C extends B { static constructor() { super(); } }", call, kBoth},
    {"class C extends B { x = super(); }", call, kBoth},
    {"class C extends B { static { super(); } }", call, kBoth},
    {"class C extends B { constructor(a = super()) {} }", 0, kBoth},
    // new.target
    {"new.target;", target, kBoth},
    {"() => new.target;", target, kBoth},
    {"function f() { new.target; }", 0, kBoth},
    {"function f() { () => new.target; }", 0, kBoth},
    {"({ m() { new.target; } });", 0, kBoth},
    {"class C { x = new.target; }", 0, kBoth},
    {"class C { static { new.target; } }", 0, kBoth},
    {"class C { [new.target]() {} }", target, kBoth},
    {"new new.target;", target, kBoth},
    {"function f() { new new.target; new.target.x; }", 0, kBoth},
  });
}

// Clause 15.7.1: `arguments` is not a name in a class initializer.
TEST(EarlyErrors, ArgumentsInAClassInitializer) {
  const uint32_t r = GLECMA_RULE_ARGUMENTS_IN_INITIALIZER;
  RunCases({
    {"class C { x = arguments; }", r, kBoth},
    {"class C { static x = arguments; }", r, kBoth},
    {"class C { x = () => arguments; }", r, kBoth},
    {"class C { x = { arguments }; }", r, kBoth},
    {"class C { x = typeof arguments; }", r, kBoth},
    {"class C { static { arguments; } }", r, kBoth},
    {"class C { static { () => arguments; } }", r, kBoth},
    {"class C { x = function () { arguments; }; }", 0, kBoth},
    {"class C { x = function () { return () => arguments; }; }", 0, kBoth},
    {"class C { static { function f() { arguments; } } }", 0, kBoth},
    {"class C { m() { arguments; } }", 0, kBoth},
    {"class C { x = a.arguments; y = { arguments: 1 }; }", 0, kBoth},
    {"class C { [arguments]() {} }", 0, kBoth},
    {"function f() { class C { x = 1; [arguments]() {} } arguments; }", 0, kBoth},
  });
}

// Clauses 15.5.1, 15.8.1, 15.3.1, 15.9.1: no `yield` or `await` expression among parameters.
TEST(EarlyErrors, YieldAndAwaitInParameters) {
  const uint32_t y = GLECMA_RULE_YIELD_IN_PARAMETERS;
  const uint32_t a = GLECMA_RULE_AWAIT_IN_PARAMETERS;
  RunCases({
    {"function* g(x = yield) {}", y, kBoth},
    {"(function* (x = yield 1) {});", y, kBoth},
    {"({ *m(x = yield) {} });", y, kBoth},
    {"function* g() { (x = yield) => 1; }", y, kBoth},
    {"function* g() { function f(x = yield) {} }", 0, kSloppy},
    {"function* g(x = function* () { yield; }) {}", 0, kBoth},
    {"function* g() { yield; function h(a = 1) { return a; } }", 0, kBoth},
    {"async function f(x = await 1) {}", a, kBoth},
    {"(async function (x = await 1) {});", a, kBoth},
    {"async function f() { (x = await 1) => 1; }", a, kBoth},
    {"async function f() { async (x = await 1) => 1; }", a, kBoth},
    {"({ async m(x = await 1) {} });", a, kBoth},
    {"async function f() { async function g(x = 1) { await x; } }", 0, kBoth},
    {"async function f(x = async () => await 1) {}", 0, kBoth},
    {"async function f() { (x = async () => await 1) => 1; }", 0, kBoth},
    {"async function* g(x = yield) {}", y, kBoth},
    {"function* g() { async (a = yield) => 1; }", y, kBoth},
    {"function* g() { async ([a = yield]) => 1; }", y, kBoth},
    {"function* g() { async (a = yield) ; }", 0, kBoth},
    {"async function* g(x = await 1) {}", a, kBoth},
  });
}

// Clauses 13.5.1.1 and 14.11.1.
TEST(EarlyErrors, DeleteAndWith) {
  const uint32_t del = GLECMA_RULE_DELETE_IDENTIFIER_STRICT;
  const uint32_t priv = GLECMA_RULE_DELETE_PRIVATE_REFERENCE;
  RunCases({
    {"delete x;", del, kStrict},
    {"delete x;", 0, kSloppy},
    {"delete (x);", del, kStrict},
    {"delete ((x));", del, kStrict},
    {"delete x.y; delete x[0]; delete x(); delete 1;", 0, kBoth},
    {"delete (x, y);", 0, kBoth},
    {"class C { #p; m() { delete this.#p; } }", priv, kBoth},
    {"class C { #p; m() { delete (this.#p); } }", priv, kBoth},
    {"class C { #p; m() { delete this?.#p; } }", priv, kBoth},
    {"class C { #p; m() { delete this?.x.#p; } }", priv, kBoth},
    {"class C { #p; m() { delete this.#p.x; delete this?.#p.x; } }", 0, kBoth},
    {"class C { #p; m() { delete this.x?.#p.y; delete this.#p(); } }", 0, kBoth},
    {"with (a) b;", GLECMA_RULE_WITH_STRICT, kStrict},
    {"with (a) b;", 0, kSloppy},
    {"function f() { 'use strict'; with (a) b; }", GLECMA_RULE_WITH_STRICT, kBoth},
    {"class C { m() { with (a) b; } }", GLECMA_RULE_WITH_STRICT, kBoth},
  });
}

// Clause 13.2.5.1: two `__proto__: value` properties, unless the literal is a pattern.
TEST(EarlyErrors, DuplicateProto) {
  const uint32_t r = GLECMA_RULE_DUPLICATE_PROTO;
  RunCases({
    {"({__proto__: a, __proto__: b});", r, kBoth},
    {"({__proto__: a, '__proto__': b});", r, kBoth},
    {"({'__proto__': a, \"__proto__\": b});", r, kBoth},
    {"({__proto__: a, \"__pro\\u0074o__\": b});", r, kBoth},
    {"x = {__proto__: a, __proto__: b};", r, kBoth},
    {"f({__proto__: a, __proto__: b});", r, kBoth},
    {"({a: {__proto__: a, __proto__: b}});", r, kBoth},
    {"[{__proto__: a, __proto__: b}];", r, kBoth},
    {"({__proto__: a, __proto__: b} = {});", 0, kBoth},
    {"[{__proto__: a, __proto__: b}] = [];", 0, kBoth},
    {"({x: {__proto__: a, __proto__: b}} = {});", 0, kBoth},
    {"for ({__proto__: a, __proto__: b} of []) ;", 0, kBoth},
    {"({__proto__: a, __proto__: b}) => 1;", 0, kBoth},
    {"([{__proto__: a, __proto__: b}]) => 1;", 0, kBoth},
    {"({__proto__: a, __proto__: b}).x;", r, kBoth},
    {"({__proto__: a, ['__proto__']: b, __proto__() {}, get __proto__() {}});", 0, kBoth},
    {"var __proto__ = 1; ({__proto__, __proto__: a, __proto__});", 0, kBoth},
    {"({__proto__: a}); ({__proto__: b});", 0, kBoth},
    // A statement inside a method of the literal does not make it forget the error.
    {"({__proto__: a, __proto__: b, m() { for (c;;) break; }});", r, kBoth},
    {"({a = 1, m() { for (b in x) ; }});", GLECMA_RULE_COVER_INITIALIZED_NAME, kBoth},
  });
}

// Clause 14.3.1.1.
// Found by a review: a directive-prologue flag that a nested function body left set, and a
// class heritage that never ruled on a pending cover-grammar error.
TEST(EarlyErrors, ADirectiveInANestedBodyIsNotTheOuterListsAndAHeritageRulesOnItsLiteral) {
  RunCases({
    // Not directives: the first statement is an expression, and the prologue ended.
    {"'use strict' + function () { 'x' }; with (a) {}", 0, kSloppy},
    {"function f() { 'x' } 'use strict'; with (a) {}", 0, kSloppy},
    {"var f = function () { 'use strict' }; 'use strict'; with (a) {}", 0, kSloppy},
    {"function f() { 'x' } 'use strict'; 010;", 0, kSloppy},
    // Directives.
    {"'x'; 'use strict'; with (a) {}", GLECMA_RULE_WITH_STRICT, kSloppy},
    {"function f() { 'x' } function g() { 'use strict'; with (a) {} }", GLECMA_RULE_WITH_STRICT, kSloppy},
    {"class A extends { a = 1 } {}", GLECMA_RULE_COVER_INITIALIZED_NAME, kBoth},
    {"class A extends ({ a = 1 }) {}", GLECMA_RULE_COVER_INITIALIZED_NAME, kBoth},
    {"class A extends { __proto__: 1, __proto__: 2 } {}", GLECMA_RULE_DUPLICATE_PROTO, kBoth},
    {"x = class extends { a = 1 } {};", GLECMA_RULE_COVER_INITIALIZED_NAME, kBoth},
    {"class A extends { a: 1 } {}", 0, kBoth},
  });
}

TEST(EarlyErrors, ConstAndLetDeclarations) {
  RunCases({
    {"const x;", GLECMA_RULE_CONST_WITHOUT_INITIALIZER, kBoth},
    {"const x = 1, y;", GLECMA_RULE_CONST_WITHOUT_INITIALIZER, kBoth},
    {"for (const x;;) ;", GLECMA_RULE_CONST_WITHOUT_INITIALIZER, kBoth},
    {"for (const x of []) ; for (const y in {}) ;", 0, kBoth},
    {"const x = 1; for (const y = 1;;) break;", 0, kBoth},
    {"let let = 1;", GLECMA_RULE_LET_BOUND_NAME, kSloppy},
    {"const let = 1;", GLECMA_RULE_LET_BOUND_NAME, kSloppy},
    {"let [let] = [];", GLECMA_RULE_LET_BOUND_NAME, kSloppy},
    {"let {let} = {};", GLECMA_RULE_LET_BOUND_NAME, kSloppy},
    {"let {a: let} = {};", GLECMA_RULE_LET_BOUND_NAME, kSloppy},
    {"let l\\u0065t;", GLECMA_RULE_LET_BOUND_NAME, kSloppy},
    {"for (let let of []) ;", GLECMA_RULE_LET_BOUND_NAME, kSloppy},
    {"var let = 1; let: 1; function let() {}", 0, kSloppy},
  });
}

// Clause 15.2.1: a directive cannot give strictness to a function whose parameters are not simple.
TEST(EarlyErrors, UseStrictWithNonSimpleParameters) {
  const uint32_t r = GLECMA_RULE_USE_STRICT_NON_SIMPLE_PARAMETERS;
  RunCases({
    {"function f(a = 1) { 'use strict'; }", r, kBoth},
    {"function f(...a) { 'use strict'; }", r, kBoth},
    {"function f({a}) { 'use strict'; }", r, kBoth},
    {"function f([a]) { 'use strict'; }", r, kBoth},
    {"(function (a = 1) { \"use strict\"; });", r, kBoth},
    {"({ m(a = 1) { 'use strict'; } });", r, kBoth},
    {"({ set a({b}) { 'use strict'; } });", r, kBoth},
    {"(a = 1) => { 'use strict'; };", r, kBoth},
    {"({a}) => { 'use strict'; };", r, kBoth},
    {"async function f(a = 1) { 'use strict'; }", r, kBoth},
    {"function* g(...a) { 'use strict'; }", r, kBoth},
    {"class C { m(a = 1) { 'use strict'; } }", r, kBoth},
    {"function f(a = 1) { 'a'; 'use strict'; }", r, kBoth},
    {"function f(a, b) { 'use strict'; }", 0, kBoth},
    {"function f(a = 1) { 'use  strict'; 'use\\x20strict'; }", 0, kBoth},
    {"function f(a = 1) { ('use strict'); }", 0, kBoth},
    {"function f(a = 1) { x; 'use strict'; }", 0, kBoth},
    {"a => { 'use strict'; };", 0, kBoth},
    {"function f(a = 1) { function g() { 'use strict'; } }", 0, kBoth},
    {"function f() { 'use strict'; function g(a = 1) { } }", 0, kBoth},
    {"function f(a = function (b) { 'use strict'; }) {}", 0, kBoth},
    {"function f(a = (b = 1) => { 'use strict'; }) {}", r, kBoth},
  });
}

// Clauses 14.13.1, 14.9.1, 14.8.1: labels, `break` and `continue`.
TEST(EarlyErrors, LabelsBreakAndContinue) {
  const uint32_t dup = GLECMA_RULE_LABEL_REDECLARED;
  const uint32_t brk = GLECMA_RULE_BREAK_TARGET;
  const uint32_t cont = GLECMA_RULE_CONTINUE_TARGET;
  const uint32_t fnb = GLECMA_RULE_LABELLED_FUNCTION_BODY;
  RunCases({
    {"a: a: ;", dup, kBoth},
    {"a: { a: ; }", dup, kBoth},
    {"a: b: a: ;", dup, kBoth},
    {"a: { } a: { }", 0, kBoth},
    {"a: b: ;", 0, kBoth},
    {"a: { function f() { a: ; } }", 0, kBoth},
    {"a: (function () { a: ; });", 0, kBoth},
    {"a: () => { a: ; };", 0, kBoth},
    {"a: { class C { static { a: ; } } }", 0, kBoth},
    {"break;", brk, kBoth},
    {"continue;", cont, kBoth},
    {"if (a) break;", brk, kBoth},
    {"{ break; }", brk, kBoth},
    {"while (a) break; do break; while (a); for (;;) break; for (a in b) break; for (a of b) break;", 0, kBoth},
    {"switch (a) { case 1: break; default: break; }", 0, kBoth},
    {"switch (a) { case 1: continue; }", cont, kBoth},
    {"while (a) { switch (b) { case 1: continue; } }", 0, kBoth},
    {"break a;", brk, kBoth},
    {"a: break a;", 0, kBoth},
    {"a: { break a; }", 0, kBoth},
    {"a: { b: break a; }", 0, kBoth},
    {"a: ; break a;", brk, kBoth},
    {"a: while (b) { break c; }", brk, kBoth},
    {"a: while (b) { function f() { break a; } }", brk, kBoth},
    {"a: while (b) { (function () { break a; }); }", brk, kBoth},
    {"a: while (b) { () => { break a; }; }", brk, kBoth},
    {"while (a) { function f() { break; } }", brk, kBoth},
    {"while (a) { () => { continue; }; }", cont, kBoth},
    {"a: while (b) { continue a; }", 0, kBoth},
    {"a: b: while (c) { continue a; continue b; }", 0, kBoth},
    {"a: { while (b) { continue a; } }", cont, kBoth},
    {"a: { continue a; }", cont, kBoth},
    {"a: if (b) { while (c) { continue a; } }", cont, kBoth},
    {"a: for (;;) { continue a; }", 0, kBoth},
    {"a: for (x in y) continue a;", 0, kBoth},
    {"a: for (x of y) continue a;", 0, kBoth},
    {"a: do { continue a; } while (b);", 0, kBoth},
    {"a: switch (b) { case 1: continue a; }", cont, kBoth},
    {"a: while (b) { c: { continue c; } }", cont, kBoth},
    {"a: while (b) { c: while (d) { continue a; continue c; } }", 0, kBoth},
    // A function declaration that a label gives a body to is not a loop's, an `if`'s or a `with`'s.
    {"a: function f() {}", 0, kSloppy},
    {"while (a) b: function f() {}", fnb, kSloppy},
    {"do a: function f() {} while (b);", fnb, kSloppy},
    {"for (;;) a: function f() {}", fnb, kSloppy},
    {"for (a in b) c: function f() {}", fnb, kSloppy},
    {"for (a of b) c: function f() {}", fnb, kSloppy},
    {"if (a) b: function f() {}", fnb, kSloppy},
    {"if (a) ; else b: function f() {}", fnb, kSloppy},
    {"with (a) b: function f() {}", fnb, kSloppy},
    {"while (a) b: c: function f() {}", fnb, kSloppy},
    {"a: b: function f() {}", 0, kSloppy},
    {"while (a) b: ;", 0, kBoth},
    {"while (a) { b: function f() {} }", 0, kSloppy},
  });
}

// Clause 15.7.1: constructors and static members.
TEST(EarlyErrors, ClassElements) {
  const uint32_t dup = GLECMA_RULE_CLASS_CONSTRUCTOR_DUPLICATE;
  const uint32_t kind = GLECMA_RULE_CLASS_CONSTRUCTOR_KIND;
  const uint32_t proto = GLECMA_RULE_CLASS_STATIC_PROTOTYPE;
  RunCases({
    {"class C { constructor() {} constructor() {} }", dup, kBoth},
    {"class C { constructor() {} 'constructor'() {} }", dup, kBoth},
    {"class C { 'constructor'() {} \"constructor\"() {} }", dup, kBoth},
    {"class C { constructor() {} ['constructor']() {} static constructor() {} }", 0, kBoth},
    {"class C { constructor() {} get constructor() {} }", kind, kBoth},
    {"class C { get constructor() {} }", kind, kBoth},
    {"class C { set constructor(v) {} }", kind, kBoth},
    {"class C { *constructor() {} }", kind, kBoth},
    {"class C { async constructor() {} }", kind, kBoth},
    {"class C { async *constructor() {} }", kind, kBoth},
    {"class C { constructor = 1; }", kind, kBoth},
    {"class C { static constructor = 1; }", kind, kBoth},
    {"class C { 'constructor' = 1; }", kind, kBoth},
    {"class C { constructor; }", kind, kBoth},
    {"class C { c\\u006fnstructor() {} get c\\u006fnstructor() {} }", kind, kBoth},
    {"class C { static get constructor() {} static *constructor() {} }", 0, kBoth},
    {"class C { ['constructor'] = 1; static ['constructor'] = 2; constructor() {} }", 0, kBoth},
    {"class C { static prototype() {} }", proto, kBoth},
    {"class C { static get prototype() {} }", proto, kBoth},
    {"class C { static set prototype(v) {} }", proto, kBoth},
    {"class C { static *prototype() {} }", proto, kBoth},
    {"class C { static async prototype() {} }", proto, kBoth},
    {"class C { static 'prototype'() {} }", proto, kBoth},
    {"class C { static prototype = 1; }", proto, kBoth},
    {"class C { static 'prototype' = 1; }", proto, kBoth},
    {"class C { prototype() {} prototype = 1; static ['prototype']() {} }", 0, kBoth},
    {"class C { static #prototype() {} static #constructor2; }", 0, kBoth},
  });
}

// Clause 15.7.1: private names, declared once, never `#constructor`, declared where used.
TEST(EarlyErrors, PrivateNames) {
  const uint32_t ctor = GLECMA_RULE_PRIVATE_NAME_CONSTRUCTOR;
  const uint32_t dup = GLECMA_RULE_PRIVATE_NAME_DUPLICATE;
  const uint32_t undeclared = GLECMA_RULE_PRIVATE_NAME_UNDECLARED;
  RunCases({
    {"class C { #constructor; }", ctor, kBoth},
    {"class C { #constructor() {} }", ctor, kBoth},
    {"class C { static get #constructor() {} }", ctor, kBoth},
    {"class C { #a; #a; }", dup, kBoth},
    {"class C { #a; #a() {} }", dup, kBoth},
    {"class C { #a() {} #a() {} }", dup, kBoth},
    {"class C { get #a() {} get #a() {} }", dup, kBoth},
    {"class C { set #a(v) {} set #a(v) {} }", dup, kBoth},
    {"class C { get #a() {} #a; }", dup, kBoth},
    {"class C { static get #a() {} set #a(v) {} }", dup, kBoth},
    {"class C { get #a() {} static set #a(v) {} }", dup, kBoth},
    {"class C { get #a() {} set #a(v) {} get #a() {} }", dup, kBoth},
    {"class C { get #a() {} set #a(v) {} #a; }", dup, kBoth},
    {"class C { get #a() {} set #a(v) {} }", 0, kBoth},
    {"class C { set #a(v) {} get #a() {} }", 0, kBoth},
    {"class C { static get #a() {} static set #a(v) {} }", 0, kBoth},
    {"class C { #a; static #b; #c() {} static #d() {} }", 0, kBoth},
    {"class C { #a; class_() { class D { #a; m() { this.#a; } } } }", 0, kBoth},
    {"class C { #a; m() { this.#a; this.#a = 1; #a in this; } }", 0, kBoth},
    {"class C { m() { this.#a; } #a; }", 0, kBoth},
    {"class C { [this.#a]() {} #a; }", 0, kBoth},
    {"class C { m() { this.#a; } }", undeclared, kBoth},
    {"class C { #a; m() { this.#b; } }", undeclared, kBoth},
    {"class C { #a; m() { class D { n() { this.#a; this.#b; } } } }", undeclared, kBoth},
    {"class C { #a; m() { class D { n() { this.#a; } } } }", 0, kBoth},
    {"class C { m() { #a in this; } }", undeclared, kBoth},
    {"this.#a;", undeclared, kBoth},
    {"function f() { this.#a; }", undeclared, kBoth},
    {"class C { #a; } this.#a;", undeclared, kBoth},
    {"class C { #a; } new C().#a;", undeclared, kBoth},
    {"class C { #a; } function f() { this.#a; }", undeclared, kBoth},
    {"#a in this;", undeclared, kBoth},
    {"#a;", GLECMA_RULE_UNEXPECTED_TOKEN, kBoth},
    // A heritage is outside the class it belongs to.
    {"class C extends (this.#a) { #a; }", undeclared, kBoth},
    {"class C extends function () { this.#a; } { #a; }", undeclared, kBoth},
    {"class A { #a; m() { class C extends (this.#a) {} } }", 0, kBoth},
    // A class that is read twice (a group that is arrow parameters) is one class.
    {"(a = class { #p; m() { this.#p; } }) => 1;", 0, kBoth},
    {"(a = class { m() { this.#p; } }) => 1;", undeclared, kBoth},
    {"({a = class { #p; m() { this.#p; } }} = {});", 0, kBoth},
    {"async (a = class { #p; m() { this.#p; } }) => 1;", 0, kBoth},
  });
}

// Clauses 14.2.1, 14.3.1.1, 14.12.1, 15.2.1, 16.1.1: a scope's lexically declared
// names are unique and are not also var-declared names, and Annex B lets sloppy code
// declare one plain function twice in a block or a case block.
TEST(EarlyErrors, RedeclarationInBlocksAndScripts) {
  const uint32_t blk = GLECMA_RULE_BLOCK_REDECLARATION;
  const uint32_t scr = GLECMA_RULE_SCRIPT_REDECLARATION;
  const uint32_t fun = GLECMA_RULE_FUNCTION_REDECLARATION;
  const uint32_t sw = GLECMA_RULE_SWITCH_REDECLARATION;
  RunCases({
    {"let a; let a;", scr, kBoth},
    {"let a; const a = 1;", scr, kBoth},
    {"let a, a;", scr, kBoth},
    {"let [a, a] = [];", scr, kBoth},
    {"let {a, b: a} = {};", scr, kBoth},
    {"let a; var a;", scr, kBoth},
    {"var a; let a;", scr, kBoth},
    {"let a; { var a; }", scr, kBoth},
    {"{ var a; } let a;", scr, kBoth},
    {"let a; function a() {}", scr, kBoth},
    {"function a() {} let a;", scr, kBoth},
    {"class a {} var a;", scr, kBoth},
    {"let a; class a {}", scr, kBoth},
    {"class a {} class a {}", scr, kBoth},
    {"let a; for (var a;;) break;", scr, kBoth},
    {"let a; if (b) { for (var a of c) ; }", scr, kBoth},
    {"var a; var a; function a() {} function a() {}", 0, kBoth},
    {"var a; function a() {} var a;", 0, kBoth},
    {"let a; { let a; var b; } var b;", 0, kBoth},
    {"let a; { let a; { let a; } }", 0, kBoth},
    {"var a; { let a; }", 0, kBoth},
    {"{ let a; } { let a; } var a;", 0, kBoth},
    {"let a; function f() { var a; let a2; }", 0, kBoth},
    {"let a; (function (a) { var a; });", 0, kBoth},
    // A function's body is its own scope.
    {"function f() { let a; let a; }", fun, kBoth},
    {"function f() { let a; var a; }", fun, kBoth},
    {"function f() { { var a; } let a; }", fun, kBoth},
    {"function f() { let a; function a() {} }", fun, kBoth},
    {"function f(a) { let a; }", fun, kBoth},
    {"function f(a = 1) { let a; }", fun, kBoth},
    {"function f({a}) { let a; }", fun, kBoth},
    {"function f(a) { var a; function a() {} }", 0, kBoth},
    {"(function f() { let f; });", 0, kBoth},
    {"(function* f() { let f; var g; });", 0, kBoth},
    {"x = class C { m() { let C; } };", 0, kBoth},
    {"(a) => { let a; };", fun, kBoth},
    {"a => { let a; };", fun, kBoth},
    {"async (a) => { let a; };", fun, kBoth},
    {"(a, b = 1) => { let b; };", fun, kBoth},
    {"({ m(a) { let a; } });", fun, kBoth},
    {"({ set s(a) { let a; } });", fun, kBoth},
    {"class C { m(a) { let a; } }", fun, kBoth},
    {"class C { constructor(a) { let a; } }", fun, kBoth},
    {"(a) => { var a; };", 0, kBoth},
    // Blocks.
    {"{ let a; let a; }", blk, kBoth},
    {"{ let a; var a; }", blk, kBoth},
    {"{ var a; let a; }", blk, kBoth},
    {"{ { var a; } let a; }", blk, kBoth},
    {"{ let a; { var a; } }", blk, kBoth},
    {"{ const a = 1; function a() {} }", blk, kBoth},
    {"{ function a() {} let a; }", blk, kBoth},
    {"{ class a {} function a() {} }", blk, kBoth},
    {"{ function a() {} var a; }", blk, kBoth},
    {"{ var a; function a() {} }", blk, kBoth},
    {"if (x) { let a; let a; }", blk, kBoth},
    {"while (x) { let a; var a; }", blk, kBoth},
    {"try {} catch (e) { { let a; let a; } }", blk, kBoth},
    {"try { let a; let a; } finally {}", blk, kBoth},
    {"{ let a; { let a; } }", 0, kBoth},
    {"{ var a; { var a; } }", 0, kBoth},
    {"{ let a; } { var a; }", 0, kBoth},
    // Annex B.3.2.4: duplicates among plain function declarations, in sloppy code.
    {"{ function a() {} function a() {} }", 0, kSloppy},
    {"{ function a() {} function a() {} }", blk, kStrict},
    {"{ function a() {} function a() {} function a() {} }", 0, kSloppy},
    {"{ function a() {} function* a() {} }", blk, kBoth},
    {"{ function* a() {} function* a() {} }", blk, kBoth},
    {"{ function a() {} async function a() {} }", blk, kBoth},
    {"{ async function a() {} async function a() {} }", blk, kBoth},
    {"{ function a() {} class a {} }", blk, kBoth},
    {"{ function* a() {} function a() {} }", blk, kBoth},
    {"function f() { function a() {} function a() {} }", 0, kBoth},
    {"function f() { function a() {} function* a() {} async function a() {} }", 0, kBoth},
    {"function a() {} function* a() {}", 0, kBoth},
    {"if (x) function a() {} else function a() {}", 0, kSloppy},
    {"a: function f() {} a2: function f() {}", 0, kSloppy},
    {"{ a: function f() {} function f() {} }", 0, kSloppy},
    // Switch case blocks (14.12.1, B.3.2.5).
    {"switch (a) { case 1: let b; case 2: let b; }", sw, kBoth},
    {"switch (a) { case 1: let b; default: var b; }", sw, kBoth},
    {"switch (a) { case 1: var b; case 2: let b; }", sw, kBoth},
    {"switch (a) { case 1: function b() {} case 2: let b; }", sw, kBoth},
    {"switch (a) { case 1: class b {} case 2: class b {} }", sw, kBoth},
    {"switch (a) { case 1: function b() {} case 2: function b() {} }", 0, kSloppy},
    {"switch (a) { case 1: function b() {} case 2: function b() {} }", sw, kStrict},
    {"switch (a) { case 1: function b() {} case 2: function* b() {} }", sw, kBoth},
    {"switch (a) { case 1: let b; } switch (a) { case 1: let b; }", 0, kBoth},
    {"switch (a) { case 1: { let b; } case 2: let b; }", 0, kBoth},
    {"switch (a) { case 1: var b; case 2: var b; }", 0, kBoth},
    {"let a; switch (b) { case 1: let a; }", 0, kBoth},
    // Many plain blocks around many vars: nothing to check, and the var still conflicts at the top.
    {"{{{{{{{{ var a; var b; }}}}}}}} let a;", scr, kBoth},
    {"{{{{{{{{ var a; var b; }}}}}}}} let c; { { let a; } }", 0, kBoth},
    // A var written in a scope that declares a name of its own is still seen by the scopes around.
    {"let a; { let b; var a; }", scr, kBoth},
    {"{ let a; { let b; { var a; } } }", blk, kBoth},
    {"function f() { let a; { let b; var a; } }", fun, kBoth},
    {"let z; { let y; { { var q; } { let q2; } } }", 0, kBoth},
    {"{ let y; { { var y; } } }", blk, kBoth},
    // A name the code uses many times is checked in time that does not grow with the count.
    {"{ let a0; let a1; let a2; let a3; let a4; let a5; let a6; let a7; let a8; let a9; "
     "let b0; let b1; let b2; let b3; let b4; let b5; let b6; let b7; let b8; let b9; let a0; }", blk, kBoth},
    {"{ let a0; let a1; let a2; let a3; let a4; let a5; let a6; let a7; let a8; let a9; "
     "let b0; let b1; let b2; let b3; let b4; let b5; let b6; let b7; let b8; let b9; let b10; }", 0, kBoth},
  });
}

// Clause 15.1.1: a parameter list repeats a name only in sloppy code, in a plain
// function whose parameters are all simple.
TEST(EarlyErrors, DuplicateParameters) {
  const uint32_t dup = GLECMA_RULE_DUPLICATE_PARAMETER;
  RunCases({
    {"function f(a, a) {}", 0, kSloppy},
    {"function f(a, a) {}", dup, kStrict},
    {"function f(a, a) { 'use strict'; }", dup, kSloppy},
    {"function f(a, a) { function g() { 'use strict'; } }", 0, kSloppy},
    {"'use strict'; function f(a, a) {}", dup, kBoth},
    {"function g() { 'use strict'; function f(a, a) {} }", dup, kBoth},
    {"(function (a, b, a) {});", 0, kSloppy},
    {"(function* (a, a) {});", 0, kSloppy},
    {"async function f(a, a) {}", 0, kSloppy},
    {"(async function (a, a) {});", 0, kSloppy},
    {"function f(a, a = 1) {}", dup, kBoth},
    {"function f(a = 1, a) {}", dup, kBoth},
    {"function f(a, ...a) {}", dup, kBoth},
    {"function f(a, [a]) {}", dup, kBoth},
    {"function f([a, a]) {}", dup, kBoth},
    {"function f({a, b: a}) {}", dup, kBoth},
    {"function f({a}, {a}) {}", dup, kBoth},
    {"function f(a, {b: [a]}) {}", dup, kBoth},
    {"(a, a) => 1;", dup, kBoth},
    {"a => a; (a, b, a) => 1;", dup, kBoth},
    {"async (a, a) => 1;", dup, kBoth},
    {"(a, [a]) => 1;", dup, kBoth},
    {"(a, ...a) => 1;", dup, kBoth},
    {"(a, a = 1) => 1;", dup, kBoth},
    {"(a, b) => 1; (a, a2) => 2;", 0, kBoth},
    {"({ m(a, a) {} });", dup, kBoth},
    {"({ *m(a, a) {} });", dup, kBoth},
    {"({ async m(a, a) {} });", dup, kBoth},
    {"({ set s(a) {} });", 0, kBoth},
    {"class C { m(a, a) {} }", dup, kBoth},
    {"class C { constructor(a, a) {} }", dup, kBoth},
    {"class C { static m(a, a) {} }", dup, kBoth},
    {"class C { *m(a, a) {} }", dup, kBoth},
    {"x = class { m(a, b) { function f(a, a) {} } };", dup, kBoth},
    // What an inner function declares is its own.
    {"function f(a) { function g(a, b) {} }", 0, kBoth},
    {"function f(a, b) { (c, a) => 1; }", 0, kBoth},
  });
}

// Clause 14.15.1 and Annex B.3.4: a catch parameter and the block it guards. A simple
// parameter may be redeclared by a var (a for-of head included), a destructured one not.
TEST(EarlyErrors, CatchParameters) {
  const uint32_t c = GLECMA_RULE_CATCH_REDECLARATION;
  RunCases({
    {"try {} catch (e) { let e; }", c, kBoth},
    {"try {} catch (e) { const e = 1; }", c, kBoth},
    {"try {} catch (e) { class e {} }", c, kBoth},
    {"try {} catch (e) { function e() {} }", c, kBoth},
    {"try {} catch ([e]) { let e; }", c, kBoth},
    {"try {} catch ({e}) { let e; }", c, kBoth},
    {"try {} catch ([e, e]) {}", c, kBoth},
    {"try {} catch ({a, b: a}) {}", c, kBoth},
    {"try {} catch ({e}) { var e; }", c, kBoth},
    {"try {} catch ([e]) { var e; }", c, kBoth},
    {"try {} catch ([e]) { { var e; } }", c, kBoth},
    {"try {} catch (e) { var e; }", 0, kBoth},
    {"try {} catch (e) { { var e; } }", 0, kBoth},
    {"try {} catch (e) { for (var e;;) break; }", 0, kBoth},
    {"try {} catch (e) { for (var e in {}) ; }", 0, kBoth},
    // The for-of exception of earlier editions is gone: a var may redeclare a simple parameter.
    {"try {} catch (e) { for (var e of []) ; }", 0, kBoth},
    {"try {} catch (e) { { for (var e of []) ; } }", 0, kBoth},
    {"try {} catch ([e]) { for (var e of []) ; }", c, kBoth},
    {"try {} catch (e) { for (var [e] in {}) ; for (var [e] of []) ; }", 0, kBoth},
    {"try {} catch (e) { { let e; } }", 0, kBoth},
    {"try {} catch (e) { { function e() {} } }", 0, kBoth},
    {"try {} catch (e) { function f() { let e; var e2; } }", 0, kBoth},
    {"try {} catch (e) { var e; } let e;", GLECMA_RULE_SCRIPT_REDECLARATION, kBoth},
    {"let e; try {} catch (e) { let e2; }", 0, kBoth},
    {"try {} catch (e) { } try {} catch (e) { }", 0, kBoth},
    {"try {} catch { let e; var e2; }", 0, kBoth},
    {"try { let e; } catch (e) { let e2; }", 0, kBoth},
    {"try {} catch (e) { { var e; } let e2; }", 0, kBoth},
  });
}

// Clauses 14.7.4.1, 14.7.5.1: a for head's lexical names and the body.
TEST(EarlyErrors, ForHeads) {
  const uint32_t f = GLECMA_RULE_FOR_REDECLARATION;
  RunCases({
    {"for (let a, a;;) ;", f, kBoth},
    {"for (const a = 1, a = 2;;) ;", f, kBoth},
    {"for (let [a, a] of b) ;", f, kBoth},
    {"for (let {a, b: a} in c) ;", f, kBoth},
    {"for (let a;;) { var a; }", f, kBoth},
    {"for (const a = 1;;) var a;", f, kBoth},
    {"for (let a of b) { var a; }", f, kBoth},
    {"for (let a in b) { var a; }", f, kBoth},
    {"for (let a of b) { { var a; } }", f, kBoth},
    {"for (let a;;) if (b) { var a; }", f, kBoth},
    {"for (let a of b) for (var a of c) ;", f, kBoth},
    {"for (let a;;) { let a; }", 0, kBoth},
    {"for (let a of b) { let a; { let a; } }", 0, kBoth},
    {"for (let a of b) { var c; { var d; } }", 0, kBoth},
    {"for (var a of b) { let a; }", 0, kBoth},
    {"for (var a of b) { var a; }", 0, kBoth},
    {"for (let a of b) ; for (let a of b) ;", 0, kBoth},
    {"for (let a of b) function_(a);", 0, kBoth},
    {"let a; for (let a;;) break;", 0, kBoth},
    {"for (let a;;) { function g() { var a; } }", 0, kBoth},
  });
}

// Clause 15.7.1: a static block is a scope of its own, laid out like a function body.
TEST(EarlyErrors, StaticBlocks) {
  const uint32_t s = GLECMA_RULE_STATIC_BLOCK_REDECLARATION;
  RunCases({
    {"class C { static { let a; let a; } }", s, kBoth},
    {"class C { static { let a; var a; } }", s, kBoth},
    {"class C { static { var a; let a; } }", s, kBoth},
    {"class C { static { { var a; } let a; } }", s, kBoth},
    {"class C { static { function a() {} let a; } }", s, kBoth},
    {"class C { static { class a {} function a() {} } }", s, kBoth},
    {"class C { static { var a; var a; function a() {} function a() {} } }", 0, kBoth},
    {"class C { static { let a; { let a; } } static { let a; } }", 0, kBoth},
    {"let a; class C { static { var a; } }", 0, kBoth},
    {"class C { static { var a; } } let a;", 0, kBoth},
  });
}

// The checks above hold under the Module goal too: strict code, `await`
// reserved, and a top level that declares its functions as lexical names
// (clause 16.2.1.1).
TEST(EarlyErrors, TheChecksHoldInAModule) {
  struct Row { const char * source; uint32_t rule; };
  const Row rows[] = {
    {"function f() {} function f() {}", GLECMA_RULE_MODULE_REDECLARATION},
    {"var f; function f() {}", GLECMA_RULE_MODULE_REDECLARATION},
    {"function f() {} var f;", GLECMA_RULE_MODULE_REDECLARATION},
    {"let a; function a() {}", GLECMA_RULE_MODULE_REDECLARATION},
    {"function f() { function g() {} function g() {} }", 0},
    {"{ function f() {} function f() {} }", GLECMA_RULE_BLOCK_REDECLARATION},
    {"var a; var a;", 0},
    {"with (a) b;", GLECMA_RULE_WITH_STRICT},
    {"var eval;", GLECMA_RULE_BINDING_EVAL_ARGUMENTS},
    {"delete a;", GLECMA_RULE_DELETE_IDENTIFIER_STRICT},
    {"function f(a, a) {}", GLECMA_RULE_DUPLICATE_PARAMETER},
    {"return;", GLECMA_RULE_RETURN_POSITION},
    {"new.target;", GLECMA_RULE_NEW_TARGET_POSITION},
    {"x = await;", GLECMA_RULE_UNEXPECTED_TOKEN},  // an operator at the top level
    {"class C { #a; m() { this.#b; } }", GLECMA_RULE_PRIVATE_NAME_UNDECLARED},
    {"a: { break a; } b: while (1) { continue b; }", 0},
    // `await` is reserved everywhere in a module, in every function and arrow.
    {"function f() { var await; }", GLECMA_RULE_RESERVED_WORD_STRICT},
    {"function f(await) {}", GLECMA_RULE_RESERVED_WORD_STRICT},
    {"() => { var await; };", GLECMA_RULE_RESERVED_WORD_STRICT},
    {"function f() { await: 1; }", GLECMA_RULE_RESERVED_WORD_STRICT},
    {"x = { m() { var await; } };", GLECMA_RULE_RESERVED_WORD_STRICT},
    {"class C { m() { var await; } }", GLECMA_RULE_RESERVED_WORD_STRICT},
    {"class C { x = await; }", GLECMA_RULE_RESERVED_WORD_STRICT},
    {"function* g() { var await; }", GLECMA_RULE_RESERVED_WORD_STRICT},
    {"x = { await: 1 }; x.await;", 0},
  };
  for (const Row & row : rows) {
    auto parsed = glecma_test::ParseModule(row.source);
    if (row.rule == 0) {
      EXPECT_EQ(parsed.result, GLECMA_OK) << row.source << glecma_rule_name(parsed.failure.rule);
    } else {
      EXPECT_EQ(parsed.result, GLECMA_ERR_SYNTAX) << row.source;
      EXPECT_EQ(parsed.failure.rule, row.rule)
          << row.source << ": " << glecma_rule_name(parsed.failure.rule);
    }
  }
  // As a Script the same sources are not errors.
  EXPECT_EQ(Parse("function f() {} function f() {}").result, GLECMA_OK);
  EXPECT_EQ(Parse("x = await;").result, GLECMA_OK);
}

int main(int argc, char * * argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

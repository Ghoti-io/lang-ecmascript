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
  });
}

// Clause 14.3.1.1.
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

int main(int argc, char * * argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

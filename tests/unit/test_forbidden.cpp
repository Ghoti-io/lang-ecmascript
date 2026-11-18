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

// Clause 17.1, "Forbidden Extensions": the checklist of what an implementation
// must not accept, grammar side. The side that concerns objects and built-ins
// (function objects' own "caller" and "arguments", arguments objects,
// ECMA-402) belongs to the library work that builds them, not to a parser.
// Each test names the item of the clause it checks, and states the
// control: the extension that is allowed where the clause allows it, so that a
// refusal is shown to come from the rule and not from a parser that refuses
// everything.

#include <gtest/gtest.h>

#include "../parse_test_support.h"

using glecma_test::Parse;

namespace {

bool Accepts(const std::string & source, bool strict, uint32_t internal = 0) {
  return Parse(source, strict, internal).result == GLECMA_OK;
}

void ExpectRefused(const std::string & source, bool strict, uint32_t internal = 0) {
  auto parsed = Parse(source, strict, internal);
  EXPECT_EQ(parsed.result, GLECMA_ERR_SYNTAX)
      << source << (strict ? " [strict]" : " [sloppy]");
}

}  // namespace

// Item: the RegExp pattern grammars must not recognize a letter as an
// IdentityEscape when UnicodeMode is present. (Annex B's grammar, without it,
// does; clause B.1.2.)
TEST(ForbiddenExtensions, ALetterIsNoIdentityEscapeInUnicodeMode) {
  for (const char * letter : {"a", "A", "z", "Z", "e", "m", "q"}) {
    std::string pattern = std::string("/\\") + letter;
    EXPECT_FALSE(Accepts(pattern + "/u;", false)) << letter;
    EXPECT_FALSE(Accepts(pattern + "/v;", false)) << letter;
    EXPECT_FALSE(Accepts("/[" + pattern.substr(1) + "]/u;", false)) << letter;
  }
  // The control: the same escapes are Annex B's in a pattern without the flag, and a
  // punctuator is an identity escape in unicode mode.
  EXPECT_TRUE(Accepts("/\\a\\q\\Z/;", false));
  EXPECT_TRUE(Accepts("/\\$\\/\\^\\./u;", false));
}

// Item: the token `:` must not be allowed to follow a BindingIdentifier.
TEST(ForbiddenExtensions, ABindingIdentifierIsNotFollowedByAColon) {
  for (bool strict : {false, true}) {
    ExpectRefused("var a: number = 1;", strict);
    ExpectRefused("let a: any;", strict);
    ExpectRefused("const a: T = 1;", strict);
    ExpectRefused("function f(a: number) {}", strict);
    ExpectRefused("function f(a, b?: number) {}", strict);
    ExpectRefused("function f(): void {}", strict);
    ExpectRefused("(a: number) => a;", strict);
    ExpectRefused("x = function (a: any) {};", strict);
    ExpectRefused("class C { m(a: number) {} }", strict);
    ExpectRefused("try {} catch (e: unknown) {}", strict);
    ExpectRefused("for (var a: number of b) ;", strict);
    // The controls: a colon after a name is a label, a property or a conditional.
    EXPECT_TRUE(Accepts("a: b; ({a: b} = {}); c ? d : e; switch (a) { case b: }", strict));
  }
}

// Item: strict code must not relax the early errors of numeric literals.
TEST(ForbiddenExtensions, StrictCodeKeepsTheNumericLiteralErrors) {
  for (const char * literal : {"010", "0777", "09", "089", "00", "01_0"}) {
    ExpectRefused(std::string("x = ") + literal + ";", true);
  }
  ExpectRefused("'use strict'; x = 010;", false);
  ExpectRefused("function f() { 'use strict'; return 017; }", false);
  // Strict code is refused where sloppy code (Annex B.1.1) is not.
  for (const char * literal : {"010", "0777", "09", "089"}) {
    EXPECT_TRUE(Accepts(std::string("x = ") + literal + ";", false)) << literal;
  }
  ExpectRefused("x = 0_1;", false);
  ExpectRefused("x = 08_1;", false);
}

// Item: TemplateEscapeSequence must not include the legacy octal or non-octal
// decimal escapes, in any code.
TEST(ForbiddenExtensions, ATemplateHasNoLegacyEscapes) {
  for (bool strict : {false, true}) {
    ExpectRefused("`\\01`;", strict);
    ExpectRefused("`\\1`;", strict);
    ExpectRefused("`\\8`;", strict);
    ExpectRefused("`\\9`;", strict);
    ExpectRefused("`a${b}\\07`;", strict);
    // The controls: \0 not followed by a digit is the null character, and a tagged
    // template keeps an invalid escape as undefined.
    EXPECT_TRUE(Accepts("`\\0`;", strict));
    EXPECT_TRUE(Accepts("tag`\\01 \\8`;", strict));
  }
  // The same escapes in a string are Annex B's, in sloppy code only.
  EXPECT_TRUE(Accepts("'\\01\\8';", false));
  ExpectRefused("'\\01';", true);
  ExpectRefused("'\\8';", true);
}

// Item: strict code must not support labelled function declarations, a function
// declaration as the body of an `if`, block-level function semantics' duplicates, or an
// initializer in a for-in head.
TEST(ForbiddenExtensions, StrictCodeHasNoAnnexBSyntax) {
  // B.3.1 labelled function declarations.
  EXPECT_TRUE(Accepts("a: function f() {}", false));
  ExpectRefused("a: function f() {}", true);
  ExpectRefused("a: b: function f() {}", true);
  // B.3.3 function declarations in if statement clauses.
  EXPECT_TRUE(Accepts("if (a) function f() {}", false));
  EXPECT_TRUE(Accepts("if (a) ; else function f() {}", false));
  ExpectRefused("if (a) function f() {}", true);
  ExpectRefused("if (a) ; else function f() {}", true);
  // B.3.2 block-level functions: one plain function declared twice in a block.
  EXPECT_TRUE(Accepts("{ function f() {} function f() {} }", false));
  ExpectRefused("{ function f() {} function f() {} }", true);
  EXPECT_TRUE(Accepts("switch (a) { case 1: function f() {} default: function f() {} }", false));
  ExpectRefused("switch (a) { case 1: function f() {} default: function f() {} }", true);
  // B.3.5 initializers in for-in statement heads.
  EXPECT_TRUE(Accepts("for (var a = 1 in b) ;", false));
  ExpectRefused("for (var a = 1 in b) ;", true);
  // Even in sloppy code the extension reaches no further than the Annex says.
  ExpectRefused("for (let a = 1 in b) ;", false);
  ExpectRefused("for (var [a] = 1 in b) ;", false);
  ExpectRefused("for (var a = 1 of b) ;", false);
  ExpectRefused("a: function* f() {}", false);
  ExpectRefused("while (a) b: function f() {}", false);
}

// Item: strict code must not relax the early errors of object initializers
// (clause 13.2.5.1).
TEST(ForbiddenExtensions, StrictCodeKeepsTheObjectInitializerErrors) {
  for (bool strict : {false, true}) {
    ExpectRefused("x = {a = 1};", strict);
    ExpectRefused("x = {__proto__: a, __proto__: b};", strict);
    EXPECT_TRUE(Accepts("({a = 1} = {}); ({__proto__: a, __proto__: b} = {});", strict));
  }
}

// Item: the lexical grammar extensions of HTML-like comments must not be
// supported when parsing for the Module goal. The Module goal itself is served by the
// goals' story; its lexical and early-error settings are reached through the internal
// entry that runs a Script with the Module goal's settings.
TEST(ForbiddenExtensions, AModuleHasNoHtmlLikeComments) {
  const uint32_t module = GLECMA_PARSE_INTERNAL_MODULE_RULES;
  EXPECT_TRUE(Accepts("a;\n<!-- b\n--> c\nd\n", false));
  ExpectRefused("a;\n<!-- b\n", false, module);
  ExpectRefused("a\n--> b\n", false, module);
  EXPECT_TRUE(Accepts("a; /* x */ // y\n", false, module));
  // Strictness is not the cause: strict Script code has the comments.
  EXPECT_TRUE(Accepts("a;\n<!-- b\n--> c\nd\n", true));
}

// Item: ImportCall must not be extended: one specifier and at most one options argument.
TEST(ForbiddenExtensions, AnImportCallTakesOneOrTwoArguments) {
  for (bool strict : {false, true}) {
    EXPECT_TRUE(Accepts("import(a);", strict));
    EXPECT_TRUE(Accepts("import(a, b);", strict));
    EXPECT_TRUE(Accepts("import(a, b,);", strict));
    EXPECT_TRUE(Accepts("import(a,);", strict));
    ExpectRefused("import();", strict);
    ExpectRefused("import(a, b, c);", strict);
    ExpectRefused("import(...a);", strict);
    ExpectRefused("import(a, ...b);", strict);
    ExpectRefused("new import(a);", strict);
    ExpectRefused("import.foo;", strict);
    ExpectRefused("import a from 'b';", strict);
    ExpectRefused("import a;", strict);
  }
}

int main(int argc, char * * argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

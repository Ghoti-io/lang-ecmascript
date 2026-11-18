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

// The Module goal (clause 16.2): the shape of its import and export
// declarations in the tree, top-level await, `import.meta`, and each early error
// of clauses 16.2.1.1, 16.2.2.1 and 16.2.3.1. Every refusal is paired with the
// control that is accepted, so that it is shown to come from the rule. Every
// case is parsed with the tree builder and the checking builder, which must
// agree.

#include <gtest/gtest.h>

#include "../parse_test_support.h"
#include "parse/builder.h"

using glecma_test::ParseAs;
using glecma_test::ParseModule;

namespace {

// A module parsed with both builders; the two must agree on the result, the
// rule and the place.
glecma_test::Parsed Module(const std::string & source) {
  auto tree = ParseModule(source);
  auto checking = ParseAs(source, GLECMA_PARSE_MODULE, false,
      GLECMA_PARSE_INTERNAL_CHECKING);
  EXPECT_EQ(tree.result, checking.result) << source;
  EXPECT_EQ(tree.failure.rule, checking.failure.rule) << source;
  EXPECT_EQ(tree.failure.location.offset, checking.failure.location.offset) << source;
  return tree;
}

// The refusal of `source` with `rule` at the `nth` occurrence of `at` in it.
void ExpectRule(const std::string & source, uint32_t rule, const std::string & at,
    int nth = 0) {
  size_t offset = std::string::npos;
  for (int i = 0; i <= nth; i++) {
    offset = source.find(at, offset == std::string::npos ? 0 : offset + 1);
    ASSERT_NE(offset, std::string::npos) << "the test names no " << at << " in " << source;
  }
  auto parsed = Module(source);
  ASSERT_EQ(parsed.result, GLECMA_ERR_SYNTAX) << source;
  EXPECT_EQ(parsed.failure.rule, rule)
      << source << ": " << glecma_rule_name(parsed.failure.rule);
  EXPECT_EQ(parsed.failure.location.offset, offset) << source << " at " << at;
}

void ExpectAccepted(const std::string & source) {
  auto parsed = Module(source);
  EXPECT_EQ(parsed.result, GLECMA_OK)
      << source << ": " << (parsed.failure.message ? parsed.failure.message : "");
}

}  // namespace

TEST(Module, TheGoalIsRecordedAndTheTreeIsAModule) {
  auto parsed = Module("a;");
  ASSERT_EQ(parsed.result, GLECMA_OK);
  EXPECT_EQ(glecma_tree_goal(parsed.tree), GLECMA_PARSE_MODULE);
  EXPECT_TRUE(glecma_tree_strict(parsed.tree));
  EXPECT_EQ(parsed.Whole(), "(Module/strict (Expression (Identifier a)))");
  EXPECT_EQ(std::string(glecma_node_kind_name(GLECMA_NODE_MODULE)), "Module");
  // An empty module is a module.
  EXPECT_EQ(Module("").Whole(), "(Module/strict)");
}

TEST(Module, AModuleHasNoDirectivePrologue) {
  // A Script's "use strict" is a directive; a Module is strict already and has none
  // (clause 16.2.1), so the statement is an ordinary one.
  EXPECT_EQ(Module("'use strict';").Whole(), "(Module/strict (Expression (StringLiteral \"use strict\")))");
  EXPECT_EQ(glecma_test::Parse("'use strict';").Whole(),
      "(Script/strict (Expression/directive (StringLiteral \"use strict\")))");
}

// ---- Import declarations (clause 16.2.2) ---------------------------------------

TEST(Module, ImportDeclarationsHaveTheShapeOfTheirForm) {
  EXPECT_EQ(Module("import 'm';").Body(),
      "(ImportDeclaration (StringLiteral \"m\") _)");
  EXPECT_EQ(Module("import a from 'm';").Body(),
      "(ImportDeclaration (StringLiteral \"m\") _ (ImportDefaultSpecifier (Identifier a)))");
  EXPECT_EQ(Module("import * as ns from 'm';").Body(),
      "(ImportDeclaration (StringLiteral \"m\") _ (ImportNamespaceSpecifier (Identifier ns)))");
  EXPECT_EQ(Module("import {a, b as c, 'd e' as f, default as g} from 'm';").Body(),
      "(ImportDeclaration (StringLiteral \"m\") _ "
      "(ImportSpecifier (Identifier a) (Identifier a)) "
      "(ImportSpecifier (Identifier b) (Identifier c)) "
      "(ImportSpecifier (StringLiteral \"d e\") (Identifier f)) "
      "(ImportSpecifier (Identifier default) (Identifier g)))");
  EXPECT_EQ(Module("import a, * as b from 'm';").Body(),
      "(ImportDeclaration (StringLiteral \"m\") _ (ImportDefaultSpecifier (Identifier a)) "
      "(ImportNamespaceSpecifier (Identifier b)))");
  EXPECT_EQ(Module("import a, {b,} from 'm';").Body(),
      "(ImportDeclaration (StringLiteral \"m\") _ (ImportDefaultSpecifier (Identifier a)) "
      "(ImportSpecifier (Identifier b) (Identifier b)))");
  EXPECT_EQ(Module("import {} from 'm';").Body(), "(ImportDeclaration (StringLiteral \"m\") _)");
  // The words of the grammar are names too.
  ExpectAccepted("import {as} from 'm'; import {as as b} from 'n'; import from from 'o';");
  ExpectAccepted("import {from} from 'm'; import {from as from2} from 'n'; import {as as as2} from 'q';");
}

TEST(Module, ImportAttributesAreAListOfKeysAndStrings) {
  EXPECT_EQ(Module("import a from 'm' with {type: 'json', 'k': 'v',};").Body(),
      "(ImportDeclaration (StringLiteral \"m\") (ImportAttributes "
      "(ImportAttribute (Identifier type) (StringLiteral \"json\")) "
      "(ImportAttribute (StringLiteral \"k\") (StringLiteral \"v\"))) (ImportDefaultSpecifier (Identifier a)))");
  EXPECT_EQ(Module("import 'm' with {};").Body(),
      "(ImportDeclaration (StringLiteral \"m\") (ImportAttributes))");
  EXPECT_EQ(Module("import 'm'\nwith {a: 'b'}").Body(),
      "(ImportDeclaration (StringLiteral \"m\") (ImportAttributes (ImportAttribute (Identifier a) (StringLiteral \"b\"))))");
  // Clause 16.2.2.1: two entries with one key are an error; a key written as an
  // identifier and as a string is the same key.
  EXPECT_STREQ(glecma_rule_clause(GLECMA_RULE_IMPORT_ATTRIBUTE_DUPLICATE), "16.2.2.1");
  ExpectRule("import 'm' with {a: 'b', a: 'c'};", GLECMA_RULE_IMPORT_ATTRIBUTE_DUPLICATE, "a: 'c'");
  ExpectRule("import 'm' with {a: 'b', 'a': 'c'};", GLECMA_RULE_IMPORT_ATTRIBUTE_DUPLICATE, "'a': 'c'");
  ExpectRule("export * from 'm' with {a: 'b', a: 'c'};", GLECMA_RULE_IMPORT_ATTRIBUTE_DUPLICATE, "a: 'c'");
  ExpectAccepted("import 'm' with {a: 'b', b: 'c'}; import 'n' with {a: 'b'};");
  // The value is a string, the keys are names or strings, and `assert` is not `with`.
  ExpectRule("import 'm' with {a: b};", GLECMA_RULE_UNEXPECTED_TOKEN, "b}");
  ExpectRule("import 'm' with {1: 'b'};", GLECMA_RULE_UNEXPECTED_TOKEN, "1:");
  ExpectRule("import 'm' assert {a: 'b'};", GLECMA_RULE_MISSING_SEMICOLON, "assert");
  ExpectRule("import 'm' w\\u0069th {a: 'b'};", GLECMA_RULE_MISSING_SEMICOLON, "w\\u0069th");
}

TEST(Module, ImportedBindingsAreCheckedAsBindings) {
  // Clause 16.2.2.1: a module's names are strict code's, and `await` is reserved.
  ExpectRule("import {default} from 'm';", GLECMA_RULE_RESERVED_WORD, "default");
  ExpectAccepted("import {default as d} from 'm';");
  ExpectRule("import {'a'} from 'm';", GLECMA_RULE_UNEXPECTED_TOKEN, "} from");
  ExpectAccepted("import {'a' as a} from 'm';");
  ExpectRule("import eval from 'm';", GLECMA_RULE_BINDING_EVAL_ARGUMENTS, "eval");
  ExpectRule("import {arguments} from 'm';", GLECMA_RULE_BINDING_EVAL_ARGUMENTS, "arguments");
  ExpectRule("import await from 'm';", GLECMA_RULE_RESERVED_WORD_STRICT, "await");
  ExpectRule("import {x as yield} from 'm';", GLECMA_RULE_RESERVED_WORD_STRICT, "yield");
  ExpectRule("import {x as let} from 'm';", GLECMA_RULE_RESERVED_WORD_STRICT, "let");
  ExpectRule("import {if} from 'm';", GLECMA_RULE_RESERVED_WORD, "if");
  ExpectAccepted("import {if as i} from 'm';");
  // An escaped `as` or `from` is not the word.
  ExpectRule("import {a \\u0061s b} from 'm';", GLECMA_RULE_UNEXPECTED_TOKEN, "\\u0061s");
  ExpectRule("import a fr\\u006fm 'm';", GLECMA_RULE_UNEXPECTED_TOKEN, "fr\\u006fm");
  // Not a declaration without a clause or a string, nor with both.
  ExpectRule("import;", GLECMA_RULE_UNEXPECTED_TOKEN, ";");
  ExpectRule("import a;", GLECMA_RULE_UNEXPECTED_TOKEN, ";");
  ExpectRule("import a, b from 'm';", GLECMA_RULE_UNEXPECTED_TOKEN, "b from");
  ExpectRule("import * from 'm';", GLECMA_RULE_UNEXPECTED_TOKEN, "from");
  ExpectRule("import {a} 'm';", GLECMA_RULE_UNEXPECTED_TOKEN, "'m'");
}

TEST(Module, ABindingIsDeclaredOnceAndNotAlsoWithVar) {
  // Clause 16.2.1.1: the lexically declared names are unique, and none is also var-declared.
  EXPECT_STREQ(glecma_rule_clause(GLECMA_RULE_MODULE_REDECLARATION), "16.2.1.1");
  ExpectRule("import a from 'm'; import a from 'n';", GLECMA_RULE_MODULE_REDECLARATION, "a from 'n'");
  ExpectRule("import {a, a} from 'm';", GLECMA_RULE_MODULE_REDECLARATION, "a}");
  ExpectRule("import a from 'm'; let a;", GLECMA_RULE_MODULE_REDECLARATION, "a;");
  ExpectRule("import a from 'm'; var a;", GLECMA_RULE_MODULE_REDECLARATION, "a;");
  ExpectRule("var a; import a from 'm';", GLECMA_RULE_MODULE_REDECLARATION, "a from");
  ExpectRule("import a from 'm'; function a() {}", GLECMA_RULE_MODULE_REDECLARATION, "a()");
  ExpectRule("import a from 'm'; class a {}", GLECMA_RULE_MODULE_REDECLARATION, "a {}");
  ExpectRule("function f() {} function f() {}", GLECMA_RULE_MODULE_REDECLARATION, "f() {}", 1);
  ExpectRule("var f; function f() {}", GLECMA_RULE_MODULE_REDECLARATION, "f() {}");
  // The controls: distinct names, a name in another scope, repeated var.
  ExpectAccepted("import a from 'm'; import b from 'n'; var c; var c; { let a; }");
  ExpectAccepted("import a from 'm'; function f(a) { var a; }");
}

// ---- Export declarations (clause 16.2.3) -----------------------------------------

TEST(Module, ExportDeclarationsHaveTheShapeOfTheirForm) {
  EXPECT_EQ(Module("export var a = 1, b;").Body(),
      "(ExportDeclaration (VariableDeclaration/var (VariableDeclarator (Identifier a) (NumberLiteral)) (VariableDeclarator (Identifier b) _)))");
  EXPECT_EQ(Module("export let l; export const c = 2;").Body(),
      "(ExportDeclaration (VariableDeclaration/let (VariableDeclarator (Identifier l) _))) "
      "(ExportDeclaration (VariableDeclaration/const (VariableDeclarator (Identifier c) (NumberLiteral))))");
  EXPECT_EQ(Module("export function f() {}").Body(),
      "(ExportDeclaration (FunctionDeclaration (Identifier f) (ParameterList) (FunctionBody/strict)))");
  EXPECT_EQ(Module("export async function* g() {}").Body(),
      "(ExportDeclaration (FunctionDeclaration/async/gen (Identifier g) (ParameterList) (FunctionBody/strict)))");
  EXPECT_EQ(Module("export class C {}").Body(),
      "(ExportDeclaration (ClassDeclaration (Identifier C) _ (ClassBody)))");
  EXPECT_EQ(Module("var a, b; export {a, b as c};").Body(),
      "(VariableDeclaration/var (VariableDeclarator (Identifier a) _) "
      "(VariableDeclarator (Identifier b) _)) "
      "(ExportNamed _ _ (ExportSpecifier (Identifier a) (Identifier a)) "
      "(ExportSpecifier (Identifier b) (Identifier c)))");
  EXPECT_EQ(Module("export {a, 'd' as 'e'} from 'm';").Body(),
      "(ExportNamed (StringLiteral \"m\") _ (ExportSpecifier (Identifier a) (Identifier a)) "
      "(ExportSpecifier (StringLiteral \"d\") (StringLiteral \"e\")))");
  EXPECT_EQ(Module("export {} from 'm';").Body(), "(ExportNamed (StringLiteral \"m\") _)");
  EXPECT_EQ(Module("export {a as b} from 'm' with {type: 'json'};").Body(),
      "(ExportNamed (StringLiteral \"m\") (ImportAttributes (ImportAttribute (Identifier type) (StringLiteral \"json\"))) "
      "(ExportSpecifier (Identifier a) (Identifier b)))");
  EXPECT_EQ(Module("export * from 'm';").Body(), "(ExportAll (StringLiteral \"m\") _ _)");
  EXPECT_EQ(Module("export * as ns from 'm';").Body(),
      "(ExportAll (StringLiteral \"m\") _ (Identifier ns))");
  EXPECT_EQ(Module("export * as 'a b' from 'm';").Body(),
      "(ExportAll (StringLiteral \"m\") _ (StringLiteral \"a b\"))");
}

TEST(Module, ExportDefaultTakesADeclarationOrAnExpression) {
  EXPECT_EQ(Module("export default 1 + 2;").Body(),
      "(ExportDefault (Binary + (NumberLiteral) (NumberLiteral)))");
  EXPECT_EQ(Module("export default function () {}").Body(),
      "(ExportDefault (FunctionDeclaration _ (ParameterList) (FunctionBody/strict)))");
  EXPECT_EQ(Module("export default function f() {}").Body(),
      "(ExportDefault (FunctionDeclaration (Identifier f) (ParameterList) (FunctionBody/strict)))");
  EXPECT_EQ(Module("export default async function* () {}").Body(),
      "(ExportDefault (FunctionDeclaration/async/gen _ (ParameterList) (FunctionBody/strict)))");
  EXPECT_EQ(Module("export default class {}").Body(),
      "(ExportDefault (ClassDeclaration _ _ (ClassBody)))");
  EXPECT_EQ(Module("export default class extends A {}").Body(),
      "(ExportDefault (ClassDeclaration/derived _ (Identifier A) (ClassBody)))");
  // The lookahead of the production: a function or class is a declaration, and a
  // parenthesized one, an async arrow and `async` alone are expressions.
  EXPECT_EQ(Module("export default (function () {});").Body(),
      "(ExportDefault (Function _ (ParameterList) (FunctionBody/strict)))");
  EXPECT_EQ(Module("export default async () => 1;").Body().substr(0, 40),
      "(ExportDefault (ArrowFunction/async/expr");
  EXPECT_EQ(Module("export default async\nfunction f() {}").Body().substr(0, 40),
      "(ExportDefault (Identifier async)) (Func");
  // A named default is bound in the module; an anonymous one is not.
  ExpectAccepted("export default function f() {} f();");
  ExpectRule("export default function f() {} var f;", GLECMA_RULE_MODULE_REDECLARATION, "f;");
  ExpectAccepted("export default function () {} function f() {}");
  ExpectRule("export default;", GLECMA_RULE_UNEXPECTED_TOKEN, ";");
  ExpectAccepted("export default 1");  // the end of the source ends the statement
}

TEST(Module, ExportNamesAreUnique) {
  // Clause 16.2.1.1: no name is exported twice, `default` included.
  EXPECT_STREQ(glecma_rule_clause(GLECMA_RULE_MODULE_DUPLICATE_EXPORT), "16.2.1.1");
  ExpectRule("export default 1; export default 2;", GLECMA_RULE_MODULE_DUPLICATE_EXPORT, "default 2");
  ExpectRule("var a; export {a}; export {a};", GLECMA_RULE_MODULE_DUPLICATE_EXPORT, "a}", 1);
  ExpectRule("var a, b; export {a, b as a};", GLECMA_RULE_MODULE_DUPLICATE_EXPORT, "a}");
  ExpectRule("export var a; export {a};", GLECMA_RULE_MODULE_DUPLICATE_EXPORT, "a}");
  ExpectRule("export var a, a;", GLECMA_RULE_MODULE_DUPLICATE_EXPORT, "a;");
  ExpectRule("export function f() {} export class f {}", GLECMA_RULE_MODULE_DUPLICATE_EXPORT, "f {}");
  ExpectRule("export let [a, {b: [a]}] = c;", GLECMA_RULE_MODULE_DUPLICATE_EXPORT, "a]}]");
  ExpectRule("export default 1; var a; export {a as default};", GLECMA_RULE_MODULE_DUPLICATE_EXPORT, "default}");
  ExpectRule("export * as a from 'm'; export * as a from 'n';", GLECMA_RULE_MODULE_DUPLICATE_EXPORT, "a from 'n'");
  ExpectRule("export {a} from 'm'; export {b as a} from 'n';", GLECMA_RULE_MODULE_DUPLICATE_EXPORT, "a} from 'n'");
  // A string and an identifier spelling one name are one name.
  ExpectRule("export {a as 'b'} from 'm'; export {c as b} from 'n';", GLECMA_RULE_MODULE_DUPLICATE_EXPORT, "b} from 'n'");
  // Controls: names that differ; `export *` names nothing.
  ExpectAccepted("export var a, b; export {a as c}; export default 1; export * from 'm'; export * from 'n';");
  ExpectAccepted("export {a as x, b as y} from 'm'; export * as z from 'n';");
  // A name declared inside a function or a class is not the module's.
  ExpectAccepted("export var f = function () { var g; }; export {f as g};");
  ExpectAccepted("export class C { static { var s; } } export {C as s};");
}

TEST(Module, AnExportedLocalNameIsDeclaredOrImported) {
  // Clause 16.2.1.1: every exported binding is var-declared, lexically declared or imported.
  EXPECT_STREQ(glecma_rule_clause(GLECMA_RULE_MODULE_EXPORT_UNDECLARED), "16.2.1.1");
  ExpectRule("export {x};", GLECMA_RULE_MODULE_EXPORT_UNDECLARED, "x}");
  ExpectRule("var a; export {a, b};", GLECMA_RULE_MODULE_EXPORT_UNDECLARED, "b}");
  ExpectRule("export {a as b}; { var a; }; function f() { var c; } export {c};", GLECMA_RULE_MODULE_EXPORT_UNDECLARED, "c}");
  ExpectRule("{ let a; } export {a};", GLECMA_RULE_MODULE_EXPORT_UNDECLARED, "a}");
  ExpectRule("function f(a) {} export {a};", GLECMA_RULE_MODULE_EXPORT_UNDECLARED, "a}");
  // The controls: a name declared before or after, imported, or var-declared in a block.
  ExpectAccepted("var a; export {a};");
  ExpectAccepted("export {a}; var a;");
  ExpectAccepted("export {a}; let a;");
  ExpectAccepted("export {a}; const a = 1;");
  ExpectAccepted("export {a}; class a {}");
  ExpectAccepted("export {a}; function a() {}");
  ExpectAccepted("export {a}; async function* a() {}");
  ExpectAccepted("export {a}; import a from 'm';");
  ExpectAccepted("export {a}; import {a} from 'm';");
  ExpectAccepted("export {a}; import * as a from 'm';");
  ExpectAccepted("export {a}; { var a; }");
  ExpectAccepted("export {a}; for (var a of b);");
  ExpectAccepted("export {a}; try {} catch (e) { var a; }");
  // `from` makes the names another module's.
  ExpectAccepted("export {x, y as z} from 'm';");
}

TEST(Module, AnExportWithoutFromNamesBindingsNotStrings) {
  // Clause 16.2.3.1: the local names are IdentifierReferences.
  EXPECT_STREQ(glecma_rule_clause(GLECMA_RULE_MODULE_EXPORT_STRING_LOCAL), "16.2.3.1");
  ExpectRule("export {'a'};", GLECMA_RULE_MODULE_EXPORT_STRING_LOCAL, "'a'");
  ExpectRule("var a; export {a, 'b' as c};", GLECMA_RULE_MODULE_EXPORT_STRING_LOCAL, "'b'");
  ExpectAccepted("export {'a'} from 'm';");
  ExpectAccepted("export {'a' as 'b'} from 'm';");
  ExpectAccepted("var a; export {a as 'b'};");
  for (const char * word : {"if", "default", "class", "yield", "await", "let", "static",
           "implements", "interface", "package", "private", "protected", "public", "enum",
           "null", "true", "typeof"}) {
    std::string source = std::string("export {") + word + "};";
    auto parsed = Module(source);
    EXPECT_EQ(parsed.result, GLECMA_ERR_SYNTAX) << source;
    EXPECT_NE(parsed.failure.rule, static_cast<uint32_t>(GLECMA_RULE_MODULE_EXPORT_UNDECLARED)) << source;
    EXPECT_EQ(Module(std::string("export {") + word + "} from 'm';").result, GLECMA_OK) << word;
    EXPECT_EQ(Module(std::string("var a; export {a as ") + word + "};").result, GLECMA_OK) << word;
  }
  // An escaped reserved word is the word.
  EXPECT_EQ(Module("export {\\u0069f};").result, GLECMA_ERR_SYNTAX);
  EXPECT_EQ(Module("export {\\u0069f} from 'm';").result, GLECMA_OK);
}

TEST(Module, AStringUsedAsAModuleNameIsWellFormed) {
  // Clause 16.2.1.1: IsStringWellFormedUnicode of the string's value.
  EXPECT_STREQ(glecma_rule_clause(GLECMA_RULE_MODULE_NAME_ILL_FORMED), "16.2.1.1");
  ExpectRule("export * as '\\uD800' from 'm';", GLECMA_RULE_MODULE_NAME_ILL_FORMED, "'\\uD800'");
  ExpectRule("import {'\\uDC00' as a} from 'm';", GLECMA_RULE_MODULE_NAME_ILL_FORMED, "'\\uDC00'");
  ExpectRule("export {a as '\\uD800x'} from 'm';", GLECMA_RULE_MODULE_NAME_ILL_FORMED, "'\\uD800x'");
  ExpectRule("export {'\\uDBFF\\uD800'} from 'm';", GLECMA_RULE_MODULE_NAME_ILL_FORMED, "'\\uDBFF");
  ExpectAccepted("export * as '\\uD83D\\uDE00' from 'm';");
  ExpectAccepted("import {'\\u{1F600}' as a} from 'm';");
  // The module specifier and an attribute's key or value are not export names.
  ExpectAccepted("import '\\uD800';");
  ExpectAccepted("import 'm' with {'\\uD800': '\\uD800'};");
}

TEST(Module, ExportsAndImportsAreAtTheTopLevelOnly) {
  for (const char * source : {"{ export var a; }", "{ import a from 'm'; }",
           "function f() { export default 1; }", "if (a) export var b;",
           "if (a) import 'm';", "label: export var a;", "class C { static { export var a; } }",
           "export export var a;", "export import a from 'm';"}) {
    auto parsed = Module(source);
    EXPECT_EQ(parsed.result, GLECMA_ERR_SYNTAX) << source;
    EXPECT_EQ(parsed.failure.rule, static_cast<uint32_t>(GLECMA_RULE_UNEXPECTED_TOKEN)) << source;
  }
  // The same words in a Script are unexpected tokens too.
  EXPECT_EQ(glecma_test::Parse("export var a;").result, GLECMA_ERR_SYNTAX);
  EXPECT_EQ(glecma_test::Parse("import a from 'm';").result, GLECMA_ERR_SYNTAX);
  // `import(` and `import.meta` are expressions, which a statement may be.
  ExpectAccepted("{ import('m'); } if (a) import.meta;");
  // Only some statements may follow `export`.
  for (const char * source : {"export if (a) b;", "export 1;", "export;", "export a;",
           "export async () => 1;", "export (class {});", "export let;"}) {
    EXPECT_EQ(Module(source).result, GLECMA_ERR_SYNTAX) << source;
  }
  // An escaped keyword is no keyword.
  EXPECT_EQ(Module("ex\\u0070ort var a;").result, GLECMA_ERR_SYNTAX);
  EXPECT_EQ(Module("\\u0069mport a from 'm';").result, GLECMA_ERR_SYNTAX);
}

TEST(Module, ExportedDeclarationsAreCheckedLikeTheirStatements) {
  ExpectRule("export const a;", GLECMA_RULE_CONST_WITHOUT_INITIALIZER, "a;");
  ExpectRule("export let let = 1;", GLECMA_RULE_RESERVED_WORD_STRICT, "let =");
  ExpectRule("export var eval;", GLECMA_RULE_BINDING_EVAL_ARGUMENTS, "eval");
  ExpectRule("export function f(a, a) {}", GLECMA_RULE_DUPLICATE_PARAMETER, "a)");
  ExpectRule("export class C { constructor() {} constructor() {} }", GLECMA_RULE_CLASS_CONSTRUCTOR_DUPLICATE, "constructor() {} }");
  ExpectRule("export default class { #a; #a; }", GLECMA_RULE_PRIVATE_NAME_DUPLICATE, "#a; }");
  ExpectRule("export var a = b => { return await; };", GLECMA_RULE_RESERVED_WORD_STRICT, "await");
  ExpectAccepted("export var [a, b = 1, ...c] = d; export let {e, f: g, ...h} = i;");
  ExpectAccepted("export async function f() { await 1; } export function* g() { yield 1; }");
  ExpectAccepted("export class C extends B { #p = 1; static m() { return #p in this; } }");
}

// ---- Top-level await and import.meta ----------------------------------------------

TEST(Module, AwaitIsAnOperatorAtTheTopLevelAndReservedEverywhere) {
  EXPECT_EQ(Module("await a;").Body(), "(Expression (Await (Identifier a)))");
  ExpectAccepted("const x = await import('m');");
  ExpectAccepted("for await (a of b) c; for await (const d of e) {}");
  ExpectAccepted("await 1, await 2; x = [await 1, ...(await y)]; ({[await k]: 1});");
  ExpectAccepted("class C { [await k]() {} static [await k] = 1; }");
  ExpectAccepted("export default await a;");
  ExpectAccepted("export var a = await b;");
  ExpectAccepted("if (await a) { await b; } while (await c) await d;");
  ExpectAccepted("async function f() { await 1; } async () => await 2;");
  // A Script has no top-level await: `await` is a name there.
  EXPECT_EQ(glecma_test::Parse("await a;").result, GLECMA_ERR_SYNTAX);
  EXPECT_EQ(glecma_test::Parse("var await;").result, GLECMA_OK);
  // Not an operator in a function that is not async, in parameters, in a class
  // field or in a static block; reserved as a name everywhere (clause 13.1.1).
  ExpectRule("function f() { await 1; }", GLECMA_RULE_RESERVED_WORD_STRICT, "await");
  ExpectRule("() => await 1;", GLECMA_RULE_RESERVED_WORD_STRICT, "await");
  ExpectRule("class C { x = await 1; }", GLECMA_RULE_RESERVED_WORD_STRICT, "await");
  ExpectRule("class C { static { await 1; } }", GLECMA_RULE_RESERVED_WORD_STRICT, "await");
  ExpectRule("(a = await 1) => a;", GLECMA_RULE_AWAIT_IN_PARAMETERS, "await");
  ExpectRule("async (a = await 1) => a;", GLECMA_RULE_AWAIT_IN_PARAMETERS, "await");
  ExpectRule("var await;", GLECMA_RULE_RESERVED_WORD_STRICT, "await");
  ExpectRule("await: 1;", GLECMA_RULE_UNEXPECTED_TOKEN, ":");
  ExpectRule("x = { await };", GLECMA_RULE_RESERVED_WORD_STRICT, "await");
  ExpectRule("function await() {}", GLECMA_RULE_RESERVED_WORD_STRICT, "await");
  ExpectRule("class await {}", GLECMA_RULE_RESERVED_WORD_STRICT, "await");
  ExpectAccepted("x = { await: 1 }; x.await; x = { async await() {} };");
  // The operator is the operator's: it needs its operand.
  ExpectRule("await;", GLECMA_RULE_UNEXPECTED_TOKEN, ";");
}

TEST(Module, ImportMetaIsAModuleExpression) {
  EXPECT_EQ(Module("import.meta;").Body(), "(Expression (MetaProperty import.meta))");
  EXPECT_EQ(Module("import.meta.url;").Body(),
      "(Expression (Member (MetaProperty import.meta) (Identifier url)))");
  EXPECT_EQ(Module("new import.meta.x();").Body().substr(0, 50),
      "(Expression (New (Member (MetaProperty import.meta");
  ExpectAccepted("function f() { return import.meta; } () => import.meta; class C { x = import.meta; static { import.meta; } m() { import.meta; } }");
  ExpectAccepted("typeof import.meta; delete import.meta.a; import.meta?.a; import.meta`x`;");
  // A Script, and eval code, have none (clause 13.3.12).
  auto script = glecma_test::Parse("import.meta;");
  EXPECT_EQ(script.result, GLECMA_ERR_SYNTAX);
  EXPECT_EQ(script.failure.rule, static_cast<uint32_t>(GLECMA_RULE_IMPORT_META_OUTSIDE_MODULE));
  EXPECT_EQ(glecma_test::Parse("function f() { import.meta; }").failure.rule,
      static_cast<uint32_t>(GLECMA_RULE_IMPORT_META_OUTSIDE_MODULE));
  // What `import.` may not be: other names, an escaped `meta`, an assignment target.
  ExpectRule("import.foo;", GLECMA_RULE_UNEXPECTED_TOKEN, "foo");
  ExpectRule("import.m\\u0065ta;", GLECMA_RULE_UNEXPECTED_TOKEN, "m\\u0065ta");
  ExpectRule("import.meta = 1;", GLECMA_RULE_INVALID_ASSIGNMENT_TARGET, "=");
  ExpectRule("import.meta++;", GLECMA_RULE_INVALID_UPDATE_TARGET, "++");
  ExpectRule("new import(a);", GLECMA_RULE_UNEXPECTED_TOKEN, "(a");
  // Dynamic import is in both goals.
  ExpectAccepted("import('m'); import('m', {with: {type: 'json'}});");
}

TEST(Module, AModuleHasNoSloppyCode) {
  // Strict code (clause 11.2.2): the strict early errors hold, and no `with`.
  ExpectRule("with (a) b;", GLECMA_RULE_WITH_STRICT, "with");
  ExpectRule("delete a;", GLECMA_RULE_DELETE_IDENTIFIER_STRICT, "a;");
  ExpectRule("010;", GLECMA_RULE_NUMERIC_LEGACY_OCTAL_STRICT, "010");
  ExpectRule("'\\07';", GLECMA_RULE_ESCAPE_LEGACY_OCTAL_STRICT, "\\07");
  ExpectRule("<!-- a\n", GLECMA_RULE_UNEXPECTED_TOKEN, "<!--");
  ExpectRule("a\n--> b\n", GLECMA_RULE_UNEXPECTED_TOKEN, ">");
  ExpectRule("return;", GLECMA_RULE_RETURN_POSITION, "return");
  ExpectRule("new.target;", GLECMA_RULE_NEW_TARGET_POSITION, "new");
  ExpectRule("super.a;", GLECMA_RULE_SUPER_PROPERTY_POSITION, "super");
  ExpectRule("this.#a;", GLECMA_RULE_PRIVATE_NAME_UNDECLARED, "#a");
  // The top level's `this` is not a syntax matter, and a hashbang is allowed.
  ExpectAccepted("this;");
  ExpectAccepted("#!/usr/bin/env node\nexport var a;");
}

TEST(Module, EveryEarlyErrorHoldsUnderBothBuilders) {
  // A module nests the checks in functions: the checking builder builds no body and
  // must still refuse.
  for (const char * source : {"function f() { export var a; }",
           "var a; export {a}; function f(x, x) { 'use strict'; }",
           "function f() { import.meta; var await; }",
           "export {x}; function f() {}",
           "class C { m() { import 'm'; } }"}) {
    EXPECT_EQ(Module(source).result, GLECMA_ERR_SYNTAX) << source;
  }
}

TEST(Module, ImportAndExportBindingsAreEventsForTheCompiler) {
  // The builder's events carry an import's binding as a declaration of its own
  // kind in the module scope, so that a scope summary needs no node.
  auto parsed = Module("import a from 'm'; export var b;");
  ASSERT_EQ(parsed.result, GLECMA_OK);
  size_t declares = 0;
  bool imported = false, scope = false;
  for (size_t i = 0; i < glecma_tree_event_count(parsed.tree); i++) {
    const GLECMA_BEvent * event = glecma_tree_event(parsed.tree, i);
    if (event->type == GLECMA_BEV_SCOPE_ENTER && event->what == GLECMA_BSCOPE_MODULE) {
      scope = true;
    }
    if (event->type == GLECMA_BEV_DECLARE) {
      declares++;
      if (event->what == GLECMA_BDECL_IMPORT) {
        imported = true;
      }
    }
  }
  EXPECT_TRUE(scope);
  EXPECT_TRUE(imported);
  EXPECT_EQ(declares, 2u);
}

TEST(Module, ManyDeclarationsStayLinear) {
  // A module with thousands of imports and exports parses in one pass: the
  // name sets are hashed, not scanned.
  std::string source;
  for (int i = 0; i < 20000; i++) {
    source += "import {a" + std::to_string(i) + " as b" + std::to_string(i) + "} from 'm'; export {b" +
        std::to_string(i) + " as c" + std::to_string(i) + "};\n";
  }
  auto parsed = ParseModule(source);
  EXPECT_EQ(parsed.result, GLECMA_OK);
  source += "export {b0 as c19999};\n";
  EXPECT_EQ(ParseModule(source).failure.rule, static_cast<uint32_t>(GLECMA_RULE_MODULE_DUPLICATE_EXPORT));
  std::string attrs = "import 'm' with {";
  for (int i = 0; i < 20000; i++) {
    attrs += "k" + std::to_string(i) + ": 'v',";
  }
  EXPECT_EQ(ParseModule(attrs + "};").result, GLECMA_OK);
  EXPECT_EQ(ParseModule(attrs + "k0: 'w'};").failure.rule,
      static_cast<uint32_t>(GLECMA_RULE_IMPORT_ATTRIBUTE_DUPLICATE));
}

int main(int argc, char * * argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

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

// The eval goals (clause 19.2.1.1): direct eval code read in the context of the
// code that called `eval`, with the context a caller-supplied struct on the parse
// options, private names included, and indirect eval code, a Script in the global
// context. Each context flag has the construct it allows accepted and the one it
// forbids refused, and the control that shows the refusal is the flag's.

#include <gtest/gtest.h>

#include "../parse_test_support.h"
#include "../test_helpers.h"

#include <string>
#include <vector>

namespace {

using glecma_test::Parsed;

// A private name as the caller writes it, in UTF-16.
struct Names {
  std::vector<std::u16string> text;
  std::vector<GLECMA_PrivateName> list;
  Names(std::initializer_list<const char16_t *> names) {
    for (const char16_t * n : names) {
      text.emplace_back(n);
    }
    for (const auto & t : text) {
      GLECMA_PrivateName name = {reinterpret_cast<const uint16_t *>(t.data()),
          static_cast<uint32_t>(t.size()), 0};
      list.push_back(name);
    }
  }
};

Parsed ParseWith(const std::string & text, uint32_t goal, const GLECMA_EvalContext * context,
    uint32_t internal = 0, uint32_t options_context = 0) {
  Parsed p;
  GLECMA_Result made = glecma_source_create_utf8(text.data(), text.size(), nullptr,
      nullptr, &p.failure, &p.source);
  if (made != GLECMA_OK) {
    p.result = made;
    return p;
  }
  GLECMA_ParseOptions options = GLECMA_PARSE_OPTIONS_INIT;
  options.goal = goal;
  options.context = options_context;
  options.eval_context = context;
  p.result = glecma_parse_internal(p.source, &options, nullptr, internal, &p.failure, &p.tree);
  return p;
}

// Direct eval code, parsed with both builders, which must agree.
Parsed Direct(const std::string & text, uint32_t flags = 0, const Names * names = nullptr) {
  GLECMA_EvalContext context = GLECMA_EVAL_CONTEXT_INIT;
  context.flags = flags;
  if (names != nullptr) {
    context.private_names = names->list.data();
    context.private_name_count = static_cast<uint32_t>(names->list.size());
  }
  auto tree = ParseWith(text, GLECMA_PARSE_DIRECT_EVAL, &context);
  auto checking = ParseWith(text, GLECMA_PARSE_DIRECT_EVAL, &context, GLECMA_PARSE_INTERNAL_CHECKING);
  EXPECT_EQ(tree.result, checking.result) << text;
  EXPECT_EQ(tree.failure.rule, checking.failure.rule) << text;
  EXPECT_EQ(tree.failure.location.offset, checking.failure.location.offset) << text;
  return tree;
}

Parsed Indirect(const std::string & text) {
  auto tree = ParseWith(text, GLECMA_PARSE_INDIRECT_EVAL, nullptr);
  auto checking = ParseWith(text, GLECMA_PARSE_INDIRECT_EVAL, nullptr, GLECMA_PARSE_INTERNAL_CHECKING);
  EXPECT_EQ(tree.result, checking.result) << text;
  EXPECT_EQ(tree.failure.rule, checking.failure.rule) << text;
  return tree;
}

// The refusal is at `delta` units after the first `at` in the source.
void ExpectRule(const Parsed & parsed, uint32_t rule, const std::string & source,
    const std::string & at, size_t delta = 0) {
  ASSERT_EQ(parsed.result, GLECMA_ERR_SYNTAX) << source;
  EXPECT_EQ(parsed.failure.rule, rule) << source << ": " << glecma_rule_name(parsed.failure.rule);
  EXPECT_EQ(parsed.failure.location.offset, source.find(at) + delta) << source << " at " << at;
}

constexpr uint32_t kFn = GLECMA_CONTEXT_NEW_TARGET;
constexpr uint32_t kMethod = kFn | GLECMA_CONTEXT_SUPER_PROPERTY;
constexpr uint32_t kDerivedCtor = kMethod | GLECMA_CONTEXT_SUPER_CALL;
constexpr uint32_t kField = kMethod | GLECMA_CONTEXT_FIELD_INITIALIZER;

}  // namespace

TEST(EvalGoals, DirectEvalIsAScriptThatRecordsItsGoalAndContext) {
  auto parsed = Direct("a;");
  ASSERT_EQ(parsed.result, GLECMA_OK);
  EXPECT_EQ(glecma_tree_goal(parsed.tree), GLECMA_PARSE_DIRECT_EVAL);
  EXPECT_FALSE(glecma_tree_strict(parsed.tree));
  EXPECT_EQ(parsed.Whole(), "(Script (Expression (Identifier a)))");
  const GLECMA_EvalContext * context = glecma_tree_eval_context(parsed.tree);
  ASSERT_NE(context, nullptr);
  EXPECT_EQ(context->flags, 0u);
  EXPECT_EQ(context->private_name_count, 0u);
  // Eval code that is not in strict code may make itself strict; the tree says what it ended in.
  auto strict = Direct("'use strict'; a;");
  ASSERT_EQ(strict.result, GLECMA_OK);
  EXPECT_TRUE(glecma_tree_strict(strict.tree));
  EXPECT_EQ(glecma_tree_eval_context(strict.tree)->flags, 0u);
  // Other goals carry no eval context.
  EXPECT_EQ(glecma_tree_eval_context(glecma_test::Parse("a;").tree), nullptr);
  EXPECT_EQ(glecma_tree_eval_context(Indirect("a;").tree), nullptr);
  EXPECT_EQ(glecma_tree_eval_context(nullptr), nullptr);
  // No context struct at all is the global context.
  auto bare = ParseWith("new.target;", GLECMA_PARSE_DIRECT_EVAL, nullptr);
  EXPECT_EQ(bare.result, GLECMA_ERR_SYNTAX);
  EXPECT_EQ(glecma_tree_eval_context(ParseWith("a;", GLECMA_PARSE_DIRECT_EVAL, nullptr).tree)->flags, 0u);
}

TEST(EvalGoals, NewTargetIsAllowedWhereTheCallerIsInAFunction) {
  const std::string source = "new.target; () => new.target;";
  ExpectRule(Direct(source), GLECMA_RULE_NEW_TARGET_POSITION, source, "new.target");
  EXPECT_EQ(Direct(source, kFn).result, GLECMA_OK);
  // A function inside the eval code is a function of its own, whatever the caller is.
  EXPECT_EQ(Direct("function f() { new.target; }").result, GLECMA_OK);
  // Every flag that implies it carries it.
  EXPECT_EQ(Direct(source, kMethod).result, GLECMA_OK);
  EXPECT_EQ(Direct(source, kField).result, GLECMA_OK);
}

TEST(EvalGoals, SuperPropertyIsAllowedInAMethodAndSuperCallInADerivedConstructor) {
  const std::string property = "super.x; super[y]; () => super.x;";
  ExpectRule(Direct(property), GLECMA_RULE_SUPER_PROPERTY_POSITION, property, "super.x");
  ExpectRule(Direct(property, kFn), GLECMA_RULE_SUPER_PROPERTY_POSITION, property, "super.x");
  EXPECT_EQ(Direct(property, kMethod).result, GLECMA_OK);
  // The context is the caller's, not the nested function's.
  ExpectRule(Direct("function f() { super.x; }", kMethod), GLECMA_RULE_SUPER_PROPERTY_POSITION,
      "function f() { super.x; }", "super.x");
  EXPECT_EQ(Direct("({ m() { super.x; } });").result, GLECMA_OK);
  const std::string call = "super(); () => super();";
  ExpectRule(Direct(call), GLECMA_RULE_SUPER_CALL_POSITION, call, "super()");
  ExpectRule(Direct(call, kMethod), GLECMA_RULE_SUPER_CALL_POSITION, call, "super()");
  EXPECT_EQ(Direct(call, kDerivedCtor).result, GLECMA_OK);
  ExpectRule(Direct("function f() { super(); }", kDerivedCtor), GLECMA_RULE_SUPER_CALL_POSITION,
      "function f() { super(); }", "super()");
  // A derived constructor's eval may also use what a method may.
  EXPECT_EQ(Direct("super.x; new.target;", kDerivedCtor).result, GLECMA_OK);
}

TEST(EvalGoals, ArgumentsIsNotANameInAFieldInitializer) {
  const std::string source = "arguments; () => arguments; ({ a: arguments });";
  EXPECT_EQ(Direct(source).result, GLECMA_OK);
  EXPECT_EQ(Direct(source, kMethod).result, GLECMA_OK);
  ExpectRule(Direct(source, kField), GLECMA_RULE_ARGUMENTS_IN_INITIALIZER, source, "arguments");
  ExpectRule(Direct("() => arguments;", kField), GLECMA_RULE_ARGUMENTS_IN_INITIALIZER,
      "() => arguments;", "arguments");
  ExpectRule(Direct("({ arguments });", kField), GLECMA_RULE_ARGUMENTS_IN_INITIALIZER,
      "({ arguments });", "arguments");
  // A function of its own has its own `arguments`; a property named so is a name.
  EXPECT_EQ(Direct("function f() { arguments; }", kField).result, GLECMA_OK);
  EXPECT_EQ(Direct("o.arguments; ({ arguments: 1 });", kField).result, GLECMA_OK);
}

TEST(EvalGoals, StrictnessIsTheCallersAndCanOnlyBeAdded) {
  const std::string sloppy = "with (a) b; 010; var eval; arguments = 1; delete x;";
  EXPECT_EQ(Direct(sloppy).result, GLECMA_OK);
  for (const char * strict : {"with (a) b;", "010;", "var eval;", "arguments = 1;", "delete x;",
           "var yield;", "var let;", "function f(a, a) {}", "'\\07';"}) {
    EXPECT_EQ(Direct(strict).result, GLECMA_OK) << strict;
    auto refused = Direct(strict, GLECMA_CONTEXT_STRICT);
    EXPECT_EQ(refused.result, GLECMA_ERR_SYNTAX) << strict;
  }
  auto strict = Direct("a;", GLECMA_CONTEXT_STRICT);
  ASSERT_EQ(strict.result, GLECMA_OK);
  EXPECT_TRUE(glecma_tree_strict(strict.tree));
  EXPECT_EQ(glecma_tree_eval_context(strict.tree)->flags, GLECMA_CONTEXT_STRICT);
  // The options' own strict bit does the same for direct eval, as for a Script.
  auto by_options = ParseWith("with (a) b;", GLECMA_PARSE_DIRECT_EVAL, nullptr, 0,
      GLECMA_CONTEXT_STRICT);
  EXPECT_EQ(by_options.result, GLECMA_ERR_SYNTAX);
  EXPECT_TRUE(ParseWith("a;", GLECMA_PARSE_DIRECT_EVAL, nullptr, 0, GLECMA_CONTEXT_STRICT).result == GLECMA_OK);
  // No directive undoes it.
  EXPECT_TRUE(glecma_tree_strict(Direct("'use sloppy'; a;", GLECMA_CONTEXT_STRICT).tree));
}

TEST(EvalGoals, EvalCodeIsAScriptAndNotAModuleOrAFunctionBody) {
  for (const char * source : {"import x from 'm';", "export var a;", "import.meta;",
           "return;", "await 1;", "yield 1;", "break;", "super.x;", "#x in o;"}) {
    EXPECT_EQ(Direct(source).result, GLECMA_ERR_SYNTAX) << source;
    EXPECT_EQ(Indirect(source).result, GLECMA_ERR_SYNTAX) << source;
  }
  // `import()` and a name called await or yield are a Script's.
  EXPECT_EQ(Direct("import('m'); var await; await: 1; var yield;").result, GLECMA_OK);
  EXPECT_EQ(Direct("<!-- c\n--> d\n").result, GLECMA_OK);
  // Whatever the caller was, `await` and `yield` are names in eval code that is not in a generator
  // or async function of its own, and `import.meta` is refused.
  auto meta = Direct("import.meta;", kMethod);
  EXPECT_EQ(meta.failure.rule, static_cast<uint32_t>(GLECMA_RULE_IMPORT_META_OUTSIDE_MODULE));
  // Top-level declarations are var-like, as in a Script.
  EXPECT_EQ(Direct("function f() {} function f() {} var f;").result, GLECMA_OK);
  ExpectRule(Direct("let a; var a;"), GLECMA_RULE_SCRIPT_REDECLARATION, "let a; var a;", "var a", 4);
}

TEST(EvalGoals, PrivateNamesInScopeAnswerReferencesNoClassOfTheCodeDeclares) {
  const Names x = {u"x"};
  const std::string reference = "o.#x; #x in o;";
  ExpectRule(Direct(reference), GLECMA_RULE_PRIVATE_NAME_UNDECLARED, reference, "#x");
  EXPECT_EQ(Direct(reference, kMethod, &x).result, GLECMA_OK);
  // A name that is not in scope is refused, among names that are.
  const Names xy = {u"x", u"y"};
  ExpectRule(Direct("o.#x; o.#z;", kMethod, &xy), GLECMA_RULE_PRIVATE_NAME_UNDECLARED,
      "o.#x; o.#z;", "#z");
  // The eval code's own classes declare their own, and see the caller's.
  const std::string own = "class C { #y; m(o) { return o.#y + o.#x; } }";
  EXPECT_EQ(Direct(own, kMethod, &x).result, GLECMA_OK);
  ExpectRule(Direct(own), GLECMA_RULE_PRIVATE_NAME_UNDECLARED, own, "#x");
  // A name a class declares is not visible outside it, whatever the caller has.
  ExpectRule(Direct("class C { #y; } o.#y;", kMethod, &x), GLECMA_RULE_PRIVATE_NAME_UNDECLARED,
      "class C { #y; } o.#y;", "o.#y", 2);
  // The heritage, a computed key, a static block and a field see the caller's names.
  EXPECT_EQ(Direct("class D extends o.#x {}", kMethod, &x).result, GLECMA_OK);
  EXPECT_EQ(Direct("class D { [o.#x]() {} static { o.#x; } f = o.#x; }", kMethod, &x).result, GLECMA_OK);
  // In a function and an arrow in the eval code, and inside nested classes.
  EXPECT_EQ(Direct("function f(o) { return () => o.#x; } class A { class() { class B { m(o) { o.#x; } } } }",
                kMethod, &x).result, GLECMA_OK);
  // An escaped spelling and a name outside the BMP are the names they spell.
  const Names escaped = {u"ab", u"\xD835\xDC9Cz"};  // U+1D49C is an ID_Start outside the BMP
  EXPECT_EQ(Direct("o.#\\u0061b; o.#\\u{1D49C}z; o.#\xF0\x9D\x92\x9Cz;", kMethod, &escaped).result, GLECMA_OK);
  ExpectRule(Direct("o.#ab;", kMethod, &x), GLECMA_RULE_PRIVATE_NAME_UNDECLARED, "o.#ab;", "#ab");
  // A name is looked up exactly: a prefix and an extension are other names.
  const Names longer = {u"xx"};
  ExpectRule(Direct("o.#x;", kMethod, &longer), GLECMA_RULE_PRIVATE_NAME_UNDECLARED, "o.#x;", "#x");
  ExpectRule(Direct("o.#xxx;", kMethod, &longer), GLECMA_RULE_PRIVATE_NAME_UNDECLARED, "o.#xxx;", "#xxx");
  // `delete` of a private member is refused whichever class it belongs to.
  EXPECT_EQ(Direct("delete o.#x;", kMethod, &x).failure.rule,
      static_cast<uint32_t>(GLECMA_RULE_DELETE_PRIVATE_REFERENCE));
  // Indirect eval has no caller, so none.
  ExpectRule(Indirect("o.#x;"), GLECMA_RULE_PRIVATE_NAME_UNDECLARED, "o.#x;", "#x");
}

TEST(EvalGoals, TheTreeKeepsItsOwnCopyOfTheContext) {
  Names names = {u"alpha", u"beta"};
  GLECMA_EvalContext context = GLECMA_EVAL_CONTEXT_INIT;
  context.flags = kDerivedCtor | GLECMA_CONTEXT_STRICT;
  context.private_names = names.list.data();
  context.private_name_count = 2;
  auto parsed = ParseWith("o.#alpha;", GLECMA_PARSE_DIRECT_EVAL, &context);
  ASSERT_EQ(parsed.result, GLECMA_OK);
  // The caller's memory goes away.
  names.text.assign(2, std::u16string(10, u'?'));
  names.list.clear();
  const GLECMA_EvalContext * kept = glecma_tree_eval_context(parsed.tree);
  ASSERT_NE(kept, nullptr);
  EXPECT_EQ(kept->flags, kDerivedCtor | GLECMA_CONTEXT_STRICT);
  EXPECT_EQ(kept->size, sizeof(GLECMA_EvalContext));
  ASSERT_EQ(kept->private_name_count, 2u);
  ASSERT_NE(kept->private_names, nullptr);
  EXPECT_EQ(std::u16string(reinterpret_cast<const char16_t *>(kept->private_names[0].units),
                kept->private_names[0].length), u"alpha");
  EXPECT_EQ(std::u16string(reinterpret_cast<const char16_t *>(kept->private_names[1].units),
                kept->private_names[1].length), u"beta");
  EXPECT_NE(static_cast<const void *>(kept->private_names), static_cast<const void *>(names.list.data()));
}

TEST(EvalGoals, IndirectEvalIsAScriptInTheGlobalContext) {
  auto parsed = Indirect("a;");
  ASSERT_EQ(parsed.result, GLECMA_OK);
  EXPECT_EQ(glecma_tree_goal(parsed.tree), GLECMA_PARSE_INDIRECT_EVAL);
  EXPECT_EQ(parsed.Whole(), "(Script (Expression (Identifier a)))");
  EXPECT_FALSE(glecma_tree_strict(parsed.tree));
  EXPECT_TRUE(glecma_tree_strict(Indirect("'use strict'; a;").tree));
  // Nothing a function or a method allows is allowed here, and arguments is a name.
  ExpectRule(Indirect("new.target;"), GLECMA_RULE_NEW_TARGET_POSITION, "new.target;", "new");
  ExpectRule(Indirect("super.x;"), GLECMA_RULE_SUPER_PROPERTY_POSITION, "super.x;", "super");
  ExpectRule(Indirect("super();"), GLECMA_RULE_SUPER_CALL_POSITION, "super();", "super");
  EXPECT_EQ(Indirect("arguments; with (a) b; 010;").result, GLECMA_OK);
  // The caller of an indirect eval has no strictness to give, and no context.
  GLECMA_EvalContext context = GLECMA_EVAL_CONTEXT_INIT;
  EXPECT_EQ(ParseWith("a;", GLECMA_PARSE_INDIRECT_EVAL, &context).result, GLECMA_ERR_INVALID);
  EXPECT_EQ(ParseWith("a;", GLECMA_PARSE_INDIRECT_EVAL, nullptr, 0, GLECMA_CONTEXT_STRICT).result,
      GLECMA_ERR_INVALID);
  // It reads what a Script reads: the same decision for each of these.
  for (const char * source : {"var a = 1; function f() { return a; }", "let a; var a;", "x = {a = 1};",
           "a: b: while (1) { continue a; }", "class C { #a; m() { this.#b; } }", "a\n<!-- b\n",
           "x = /(/;", "for (let of x);", "async () => await 1;", "'use strict'; with (a) b;"}) {
    auto script = glecma_test::Parse(source);
    auto eval = Indirect(source);
    EXPECT_EQ(script.result, eval.result) << source;
    EXPECT_EQ(script.failure.rule, eval.failure.rule) << source;
    EXPECT_EQ(script.failure.location.offset, eval.failure.location.offset) << source;
  }
}

TEST(EvalGoals, ASourceGoalWithNoSuchEntryAndAContextWhereNoneBelongsAreRefused) {
  GLECMA_EvalContext context = GLECMA_EVAL_CONTEXT_INIT;
  for (uint32_t goal : {GLECMA_PARSE_SCRIPT, GLECMA_PARSE_MODULE}) {
    auto parsed = ParseWith("a;", goal, &context);
    EXPECT_EQ(parsed.result, GLECMA_ERR_INVALID) << goal;
    EXPECT_NE(parsed.failure.message, nullptr);
  }
  // The dynamic-function and the re-parse goals have entries of their own.
  EXPECT_EQ(ParseWith("a;", GLECMA_PARSE_DYNAMIC_FUNCTION, nullptr).result, GLECMA_ERR_INVALID);
  EXPECT_EQ(ParseWith("a;", GLECMA_PARSE_FUNCTION, nullptr).result, GLECMA_ERR_INVALID);
  EXPECT_EQ(ParseWith("a;", 6, nullptr).result, GLECMA_ERR_INVALID);
}

TEST(EvalGoals, AnEvalContextIsCheckedBeforeAnyWork) {
  Names ok = {u"x"};
  glecma_test::CountingAllocator counting;
  auto refused = [&](GLECMA_EvalContext context) {
    GLECMA_Source * source = nullptr;
    EXPECT_EQ(glecma_source_create_utf8("a;", 2, nullptr, nullptr, nullptr, &source), GLECMA_OK);
    counting.reset();
    GLECMA_ParseOptions options = GLECMA_PARSE_OPTIONS_INIT;
    options.goal = GLECMA_PARSE_DIRECT_EVAL;
    options.eval_context = &context;
    GLECMA_Tree * tree = nullptr;
    GLECMA_Failure failure = GLECMA_FAILURE_INIT;
    GLECMA_Result r = glecma_parse(source, &options, counting.get(), &failure, &tree);
    EXPECT_EQ(counting.calls(), 0u);
    EXPECT_EQ(tree, nullptr);
    glecma_source_release(source);
    return r;
  };
  GLECMA_EvalContext base = GLECMA_EVAL_CONTEXT_INIT;
  GLECMA_EvalContext c = base;
  c.size = 4;
  EXPECT_EQ(refused(c), GLECMA_ERR_INVALID);                       // below the header
  c = base; c.flags = 0x20;                                        // a flag the eval goal lacks
  EXPECT_EQ(refused(c), GLECMA_ERR_INVALID);
  c = base; c.flags = GLECMA_CONTEXT_YIELD;
  EXPECT_EQ(refused(c), GLECMA_ERR_INVALID);
  c = base; c.flags = GLECMA_CONTEXT_SUPER_PROPERTY;               // implies new.target
  EXPECT_EQ(refused(c), GLECMA_ERR_INVALID);
  c = base; c.flags = GLECMA_CONTEXT_NEW_TARGET | GLECMA_CONTEXT_SUPER_CALL;  // implies super.x
  EXPECT_EQ(refused(c), GLECMA_ERR_INVALID);
  c = base; c.flags = GLECMA_CONTEXT_NEW_TARGET | GLECMA_CONTEXT_FIELD_INITIALIZER;
  EXPECT_EQ(refused(c), GLECMA_ERR_INVALID);
  c = base; c.private_name_count = 1;                              // no array
  EXPECT_EQ(refused(c), GLECMA_ERR_INVALID);
  c = base; c.private_names = ok.list.data(); c.private_name_count = 1;
  ok.list[0].length = 0;                                           // an empty name
  EXPECT_EQ(refused(c), GLECMA_ERR_INVALID);
  ok.list[0].length = 1; ok.list[0].units = nullptr;               // no units
  EXPECT_EQ(refused(c), GLECMA_ERR_INVALID);
  ok.list[0].units = reinterpret_cast<const uint16_t *>(ok.text[0].data());
  ok.list[0].reserved = 1;
  EXPECT_EQ(refused(c), GLECMA_ERR_INVALID);
  c = base; c.reserved = 1;
  EXPECT_EQ(refused(c), GLECMA_ERR_INVALID);
  // A struct that declares the header alone is the global context: the members it lacks are default.
  struct { uint32_t size; uint32_t flags; } header = {8, 0};
  GLECMA_ParseOptions options = GLECMA_PARSE_OPTIONS_INIT;
  options.goal = GLECMA_PARSE_DIRECT_EVAL;
  options.eval_context = reinterpret_cast<const GLECMA_EvalContext *>(&header);
  GLECMA_Source * source = nullptr;
  ASSERT_EQ(glecma_source_create_utf8("new.target;", 11, nullptr, nullptr, nullptr, &source), GLECMA_OK);
  GLECMA_Tree * tree = nullptr;
  EXPECT_EQ(glecma_parse(source, &options, nullptr, nullptr, &tree), GLECMA_ERR_SYNTAX);
  header.flags = kFn;  // the flags are within the header: they are read
  ASSERT_EQ(glecma_parse(source, &options, nullptr, nullptr, &tree), GLECMA_OK);
  glecma_tree_destroy(tree);
  glecma_source_release(source);
}

TEST(EvalGoals, ADefaultingCallFillsOnlyWhatTheSizeDeclares) {
  GLECMA_EvalContext c = GLECMA_EVAL_CONTEXT_INIT;
  c.flags = 0xFFu; c.private_name_count = 9; c.reserved = 3;
  ASSERT_EQ(glecma_eval_context_default(&c), GLECMA_OK);
  EXPECT_EQ(c.flags, 0u);
  EXPECT_EQ(c.private_name_count, 0u);
  EXPECT_EQ(c.reserved, 0u);
  EXPECT_EQ(c.private_names, nullptr);
  struct { uint32_t size; uint32_t flags; uint32_t after; } part = {8, 7, 0x5A5A5A5Au};
  ASSERT_EQ(glecma_eval_context_default(reinterpret_cast<GLECMA_EvalContext *>(&part)), GLECMA_OK);
  EXPECT_EQ(part.flags, 0u);
  EXPECT_EQ(part.after, 0x5A5A5A5Au);  // not written
  c.size = 4;
  EXPECT_EQ(glecma_eval_context_default(&c), GLECMA_ERR_INVALID);
  EXPECT_EQ(glecma_eval_context_default(nullptr), GLECMA_ERR_INVALID);
  // The options struct has the member, and its default is none.
  GLECMA_ParseOptions options = GLECMA_PARSE_OPTIONS_INIT;
  GLECMA_EvalContext some = GLECMA_EVAL_CONTEXT_INIT;
  options.eval_context = &some;
  options.goal = GLECMA_PARSE_DIRECT_EVAL;
  ASSERT_EQ(glecma_parse_options_default(&options), GLECMA_OK);
  EXPECT_EQ(options.eval_context, nullptr);
  EXPECT_EQ(options.goal, GLECMA_PARSE_SCRIPT);
}

TEST(EvalGoals, AFailedEvalParseLeaksNothingAtAnyPoint) {
  Names names = {u"x", u"yy"};
  GLECMA_EvalContext context = GLECMA_EVAL_CONTEXT_INIT;
  context.flags = kMethod;
  context.private_names = names.list.data();
  context.private_name_count = 2;
  const std::string text = "class C { #z; m(o) { return o.#z + o.#x + o.#yy; } } o.#x;";
  GLECMA_Source * source = nullptr;
  ASSERT_EQ(glecma_source_create_utf8(text.data(), text.size(), nullptr, nullptr, nullptr, &source),
      GLECMA_OK);
  GLECMA_ParseOptions options = GLECMA_PARSE_OPTIONS_INIT;
  options.goal = GLECMA_PARSE_DIRECT_EVAL;
  options.eval_context = &context;
  size_t total = 0;
  {
    glecma_test::CountingAllocator counting;
    GLECMA_Tree * tree = nullptr;
    ASSERT_EQ(glecma_parse(source, &options, counting.get(), nullptr, &tree), GLECMA_OK);
    total = counting.calls();
    glecma_tree_destroy(tree);
    EXPECT_EQ(counting.live(), 0u);
  }
  ASSERT_GT(total, 8u);
  for (size_t i = 0; i < total; i++) {
    glecma_test::CountingAllocator counting;
    counting.fail_at(i);
    GLECMA_Tree * tree = nullptr;
    GLECMA_Failure failure = GLECMA_FAILURE_INIT;
    EXPECT_EQ(glecma_parse(source, &options, counting.get(), &failure, &tree), GLECMA_ERR_OOM)
        << "allocation " << i << " of " << total;
    EXPECT_EQ(tree, nullptr);
    EXPECT_EQ(counting.live(), 0u) << "allocation " << i;
  }
  glecma_source_release(source);
}

// `%Name` is an intrinsic only in the internal built-in entry (contract 11.4): in every
// public goal, eval code and a dynamic function's pieces included, `%` is the remainder
// operator, so user source can never spell one.
TEST(EvalGoals, NoPublicGoalReadsAnIntrinsicName) {
  const std::string text = "%ArrayPrototype(x);";
  for (const char * source : {"%ArrayPrototype(x);", "a = %B;"}) {
    auto direct = Direct(source);
    ASSERT_EQ(direct.result, GLECMA_ERR_SYNTAX) << source;
    EXPECT_EQ(direct.failure.rule, static_cast<uint32_t>(GLECMA_RULE_UNEXPECTED_TOKEN)) << source;
    auto indirect = Indirect(source);
    EXPECT_EQ(indirect.failure.rule, static_cast<uint32_t>(GLECMA_RULE_UNEXPECTED_TOKEN)) << source;
    EXPECT_EQ(glecma_test::ParseModule(source).failure.rule, static_cast<uint32_t>(GLECMA_RULE_UNEXPECTED_TOKEN));
    EXPECT_EQ(glecma_test::Parse(source).failure.rule, static_cast<uint32_t>(GLECMA_RULE_UNEXPECTED_TOKEN));
  }
  // The remainder it is read as.
  EXPECT_EQ(Direct("a %b;").result, GLECMA_OK);
  EXPECT_EQ(Indirect("a %b;").result, GLECMA_OK);
  // A dynamic function's body and parameters.
  GLECMA_Source * body = nullptr;
  ASSERT_EQ(glecma_source_create_utf8(text.data(), text.size(), nullptr, nullptr, nullptr, &body), GLECMA_OK);
  GLECMA_DynamicFunction function = GLECMA_DYNAMIC_FUNCTION_INIT;
  function.body = body;
  GLECMA_Tree * tree = nullptr;
  GLECMA_Failure failure = GLECMA_FAILURE_INIT;
  EXPECT_EQ(glecma_parse_dynamic_function(&function, nullptr, nullptr, &failure, &tree), GLECMA_ERR_SYNTAX);
  EXPECT_EQ(failure.rule, static_cast<uint32_t>(GLECMA_RULE_UNEXPECTED_TOKEN));
  EXPECT_EQ(failure.location.offset, 0u);
  function.body = nullptr;
  function.parameters = body;
  EXPECT_EQ(glecma_parse_dynamic_function(&function, nullptr, nullptr, &failure, &tree), GLECMA_ERR_SYNTAX);
  EXPECT_EQ(failure.location.offset, 0u);
  glecma_source_release(body);
  // One function read again does not read an intrinsic either.
  auto program = glecma_test::Parse("x = function () { return a %b; };");
  GLECMA_FunctionContext context = GLECMA_FUNCTION_CONTEXT_INIT;
  context.kind = GLECMA_FNKIND_FUNCTION;
  context.start = 4;
  context.end = 32;
  ASSERT_EQ(glecma_parse_function(program.source, &context, nullptr, nullptr, &failure, &tree), GLECMA_OK);
  glecma_tree_destroy(tree);
}

int main(int argc, char * * argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

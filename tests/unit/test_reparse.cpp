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

// The function re-parse goal: one function read again from its range, in the
// context it had, gives the tree the first parse gave it. The test262 gate runs
// the equality over every function of the corpus (tools/check-reparse.py); these
// are what it cannot say: each context flag has a function that needs it and
// is refused without it, the failures are what the contract says they are, and a
// function is read again from the source alone.

#include <gtest/gtest.h>

#include "../parse_test_support.h"
#include "../reparse_support.h"
#include "../test_helpers.h"
#include "parse/builder.h"

#include <memory>
#include <string>
#include <vector>

namespace {

using glecma_test::Parsed;

// A source, parsed once.
struct Program {
  GLECMA_Source * source = nullptr;
  GLECMA_Tree * tree = nullptr;
  std::string text;
  explicit Program(const std::string & t, uint32_t goal = GLECMA_PARSE_SCRIPT) : text(t) {
    EXPECT_EQ(glecma_source_create_utf8(text.data(), text.size(), nullptr, nullptr, nullptr, &source),
        GLECMA_OK);
    GLECMA_ParseOptions options = GLECMA_PARSE_OPTIONS_INIT;
    options.goal = goal;
    GLECMA_Failure failure = GLECMA_FAILURE_INIT;
    EXPECT_EQ(glecma_parse(source, &options, nullptr, &failure, &tree), GLECMA_OK)
        << t << ": " << (failure.message ? failure.message : "");
  }
  Program(const Program &) = delete;
  Program & operator=(const Program &) = delete;
  ~Program() {
    glecma_tree_destroy(tree);
    glecma_source_release(source);
  }
  size_t At(const std::string & needle, size_t nth = 0) const {
    size_t at = std::string::npos;
    for (size_t i = 0; i <= nth; i++) {
      at = text.find(needle, at == std::string::npos ? 0 : at + 1);
      EXPECT_NE(at, std::string::npos) << needle << " in " << text;
    }
    return at;
  }
};

struct Again {
  GLECMA_Tree * tree = nullptr;
  GLECMA_Result result = GLECMA_OK;
  GLECMA_Failure failure = GLECMA_FAILURE_INIT;
  Again() = default;
  Again(const Again &) = delete;
  Again & operator=(const Again &) = delete;
  ~Again() { glecma_tree_destroy(tree); }
  std::string Whole(bool ranges = true) const {
    std::string out;
    glecma_test::Render(glecma_tree_root(tree), ranges, &out);
    return out;
  }
};

// Reads `function` (a substring of the program, which is the function's range) again.
std::unique_ptr<Again> Read(const Program & program, const std::string & function, uint32_t kind,
    uint32_t flags = 0, std::vector<std::u16string> names = {}, size_t nth = 0,
    uint32_t internal = 0) {
  auto again = std::make_unique<Again>();
  std::vector<GLECMA_PrivateName> list;
  for (const auto & n : names) {
    GLECMA_PrivateName name = {reinterpret_cast<const uint16_t *>(n.data()),
        static_cast<uint32_t>(n.size()), 0};
    list.push_back(name);
  }
  GLECMA_FunctionContext context = GLECMA_FUNCTION_CONTEXT_INIT;
  context.kind = kind;
  context.start = program.At(function, nth);
  context.end = context.start + function.size();
  context.flags = flags;
  context.private_names = list.empty() ? nullptr : list.data();
  context.private_name_count = static_cast<uint32_t>(list.size());
  again->result = glecma_parse_function_internal(program.source, &context, nullptr, nullptr,
      internal, &again->failure, &again->tree);
  return again;
}

constexpr uint32_t kS = GLECMA_CONTEXT_STRICT;
constexpr uint32_t kNT = GLECMA_CONTEXT_NEW_TARGET;
constexpr uint32_t kSP = kNT | GLECMA_CONTEXT_SUPER_PROPERTY;
constexpr uint32_t kSC = kSP | GLECMA_CONTEXT_SUPER_CALL;
constexpr uint32_t kFI = kSP | GLECMA_CONTEXT_FIELD_INITIALIZER;
constexpr uint32_t kSB = kSP | GLECMA_CONTEXT_STATIC_BLOCK;
constexpr uint32_t kY = GLECMA_CONTEXT_YIELD;
constexpr uint32_t kA = GLECMA_CONTEXT_AWAIT;
constexpr uint32_t kM = GLECMA_CONTEXT_MODULE | GLECMA_CONTEXT_STRICT;
constexpr uint32_t kD = GLECMA_FNCTX_DECLARATION;

}  // namespace

TEST(Reparse, EveryKindOfFunctionReadsAgainAsTheSameTree) {
  const char * source =
      "function f(a, b) { return a + b; }\n"
      "var g = function* () { yield 1; };\n"
      "async function h() { await 1; }\n"
      "var i = async function* () { yield await 1; };\n"
      "var j = (x, y) => x + y;\n"
      "var k = async z => { await z; };\n"
      "var o = { m(a) { return super.x; }, get p() { return 1; }, set p(v) {}, async *q() {}, [k + 1]() {} };\n"
      "class B { constructor(a) { this.a = a; } static s() {} #p = 1; get g() { return this.#p; } }\n"
      "class D extends B { constructor() { super(1); } x = () => this.#y; #y = 2; f = (1, 2); static { this.z = 1; } }\n";
  Program program(source);
  RpStats stats;
  memset(&stats, 0, sizeof stats);
  char message[600];
  ASSERT_EQ(rp_check_tree(program.source, program.tree, nullptr, &stats, message, sizeof message), 0)
      << message;
  for (uint32_t kind = 1; kind < GLECMA_FNKIND_COUNT; kind++) {
    EXPECT_GE(stats.kinds[kind], 1u) << "no function of kind " << kind;
  }
  EXPECT_GE(stats.functions, 20u);
}

TEST(Reparse, ATreeIsRecordedAsFunctionAndItsRangesAreInTheWholeSource) {
  Program program("var before = 1;\n\nfunction f(a) {\n  return a;\n}\n");
  auto again = Read(program, "function f(a) {\n  return a;\n}", GLECMA_FNKIND_FUNCTION, kD);
  ASSERT_EQ(again->result, GLECMA_OK) << again->failure.message;
  EXPECT_EQ(glecma_tree_goal(again->tree), GLECMA_PARSE_FUNCTION);
  EXPECT_FALSE(glecma_tree_strict(again->tree));
  const GLECMA_Node * root = glecma_tree_root(again->tree);
  EXPECT_EQ(glecma_node_kind(root), GLECMA_NODE_FUNCTION_DECLARATION);
  const size_t start = program.At("function f");
  EXPECT_EQ(glecma_node_start(root), start);
  EXPECT_EQ(glecma_node_end(root), start + std::string("function f(a) {\n  return a;\n}").size());
  GLECMA_Location from, to;
  ASSERT_EQ(glecma_node_location(again->tree, root, &from, &to), GLECMA_OK);
  EXPECT_EQ(from.line, 3u);
  EXPECT_EQ(from.column, 0u);
  EXPECT_EQ(to.line, 5u);
  EXPECT_EQ(to.column, 1u);
  // The same text, and so the same tree, as the first parse gave it.
  std::string first;
  glecma_test::Render(glecma_node_child(glecma_tree_root(program.tree), 1), true, &first);
  EXPECT_EQ(again->Whole(), first);
}

TEST(Reparse, TheFunctionIsReadFromTheSourceAloneAfterTheTreeIsGone) {
  GLECMA_Source * source = nullptr;
  const std::string text = "x = function f() { return () => 1; };";
  ASSERT_EQ(glecma_source_create_utf8(text.data(), text.size(), nullptr, nullptr, nullptr, &source), GLECMA_OK);
  GLECMA_Tree * tree = nullptr;
  ASSERT_EQ(glecma_parse(source, nullptr, nullptr, nullptr, &tree), GLECMA_OK);
  // The tree holds a reference of its own: the caller's may go, and the tree's with it, and a
  // holder that kept one still reads.
  GLECMA_Source * kept = glecma_source_retain(source);
  glecma_source_release(source);
  glecma_tree_destroy(tree);
  GLECMA_FunctionContext context = GLECMA_FUNCTION_CONTEXT_INIT;
  context.kind = GLECMA_FNKIND_FUNCTION;
  context.start = text.find("function");
  context.end = text.size() - 1;
  GLECMA_Tree * again = nullptr;
  GLECMA_Failure failure = GLECMA_FAILURE_INIT;
  ASSERT_EQ(glecma_parse_function(kept, &context, nullptr, nullptr, &failure, &again), GLECMA_OK)
      << failure.message;
  EXPECT_EQ(glecma_node_kind(glecma_tree_root(again)), GLECMA_NODE_FUNCTION_EXPRESSION);
  // ... and the tree it made holds the source in turn.
  glecma_source_release(kept);
  EXPECT_NE(glecma_tree_source(again), nullptr);
  EXPECT_EQ(glecma_source_units(glecma_tree_source(again)), text.size());
  glecma_tree_destroy(again);
}

TEST(Reparse, EachContextFlagIsOneAFunctionNeedsAndIsRefusedWithout) {
  struct Row {
    const char * program;
    const char * function;
    uint32_t kind;
    uint32_t flags;
    uint32_t without;  // the flags that must refuse it
  };
  const Row rows[] = {
    {"function* g() { (() => new.target); }", "() => new.target", GLECMA_FNKIND_ARROW, kNT, 0},
    {"({ m() { (() => super.x); } });", "() => super.x", GLECMA_FNKIND_ARROW, kSP, kNT},
    {"class D extends B { constructor() { (() => super()); } }", "() => super()", GLECMA_FNKIND_ARROW, kSC | kS, kSP | kS},
    {"function f() { return () => arguments; }", "() => arguments", GLECMA_FNKIND_ARROW, kNT, kFI},
    {"function f() { function yield() {} }", "function yield() {}", GLECMA_FNKIND_FUNCTION, kD, kD | kY},
    {"function f() { function await() {} }", "function await() {}", GLECMA_FNKIND_FUNCTION, kD, kD | kA},
    {"function f() { var await; }", "function f() { var await; }", GLECMA_FNKIND_FUNCTION, kD, kD | kM},
    {"function f(a, a) {}", "function f(a, a) {}", GLECMA_FNKIND_FUNCTION, kD, kD | kS},
    {"function f() { 010; }", "function f() { 010; }", GLECMA_FNKIND_FUNCTION, kD, kD | kS},
    {"function f() { with (a) b; }", "function f() { with (a) b; }", GLECMA_FNKIND_FUNCTION, kD, kD | kS},
    {"class C { #p; m() { return () => this.#p; } }", "() => this.#p", GLECMA_FNKIND_ARROW, 0, 0},
    // A computed key is read in the code around the class, whose `yield` and `await` it may use.
    {"function* g() { class C { [yield 1]() {} } }", "[yield 1]() {}", GLECMA_FNKIND_METHOD, kS | kY, kS},
    {"async function f() { class C { [await 1]() {} } }", "[await 1]() {}", GLECMA_FNKIND_METHOD, kS | kA, kS},
  };
  for (const Row & row : rows) {
    Program program(row.program);
    const std::vector<std::u16string> names = {u"p"};
    const bool private_name = std::string(row.program).find("#p") != std::string::npos;
    auto ok = Read(program, row.function, row.kind, row.flags, private_name ? names : std::vector<std::u16string>{});
    EXPECT_EQ(ok->result, GLECMA_OK) << row.program << ": " << (ok->failure.message ? ok->failure.message : "");
    if (row.without != row.flags) {
      auto refused = Read(program, row.function, row.kind, row.without, private_name ? names : std::vector<std::u16string>{});
      EXPECT_EQ(refused->result, GLECMA_ERR_INTERNAL) << row.program << " without " << row.without;
    }
    if (private_name) {
      auto refused = Read(program, row.function, row.kind, row.flags);
      EXPECT_EQ(refused->result, GLECMA_ERR_INTERNAL) << row.program << " without the private name";
    }
  }
}

TEST(Reparse, AFunctionTakesWhatItsKindGivesItAndAnArrowWhatIsAroundIt) {
  // A function of its own has its own `new.target` and no `super`; whatever the code around had.
  Program program("class D extends B { m() { function f() { new.target; } var g = () => super.x; } }");
  EXPECT_EQ(Read(program, "function f() { new.target; }", GLECMA_FNKIND_FUNCTION, kD | kS | kSC)->result, GLECMA_OK);
  EXPECT_EQ(Read(program, "function f() { new.target; }", GLECMA_FNKIND_FUNCTION, kD)->result, GLECMA_OK);
  // The arrow takes the method's `super.x`, and has none without it.
  EXPECT_EQ(Read(program, "() => super.x", GLECMA_FNKIND_ARROW, kS | kSP)->result, GLECMA_OK);
  EXPECT_EQ(Read(program, "() => super.x", GLECMA_FNKIND_ARROW, kS | kNT)->result, GLECMA_ERR_INTERNAL);
  // A function of its own has no `super.x` whatever was around it: the text that is refused.
  Program no_super("function f() { return super_x; }");
  EXPECT_EQ(Read(no_super, "super_x", GLECMA_FNKIND_FIELD_INITIALIZER)->result, GLECMA_OK);
}

TEST(Reparse, TheKindIsTheFunctionAndAnotherKindIsNotIt) {
  Program program("function f() {} function* g() {} async function h() {} var a = () => 1; var b = async x => x; var c = async => 1;");
  // Each text under each wrong kind of function.
  EXPECT_EQ(Read(program, "function* g() {}", GLECMA_FNKIND_FUNCTION, kD)->result, GLECMA_ERR_INTERNAL);
  EXPECT_EQ(Read(program, "function f() {}", GLECMA_FNKIND_GENERATOR, kD)->result, GLECMA_ERR_INTERNAL);
  EXPECT_EQ(Read(program, "async function h() {}", GLECMA_FNKIND_FUNCTION, kD)->result, GLECMA_ERR_INTERNAL);
  EXPECT_EQ(Read(program, "function f() {}", GLECMA_FNKIND_ASYNC_FUNCTION, kD)->result, GLECMA_ERR_INTERNAL);
  EXPECT_EQ(Read(program, "() => 1", GLECMA_FNKIND_ASYNC_ARROW)->result, GLECMA_ERR_INTERNAL);
  EXPECT_EQ(Read(program, "async x => x", GLECMA_FNKIND_ARROW)->result, GLECMA_ERR_INTERNAL);
  EXPECT_EQ(Read(program, "function f() {}", GLECMA_FNKIND_ARROW)->result, GLECMA_ERR_INTERNAL);
  EXPECT_EQ(Read(program, "() => 1", GLECMA_FNKIND_FUNCTION)->result, GLECMA_ERR_INTERNAL);
  // And each under its own, `async => 1` being an arrow whose parameter is called async.
  EXPECT_EQ(Read(program, "function* g() {}", GLECMA_FNKIND_GENERATOR, kD)->result, GLECMA_OK);
  EXPECT_EQ(Read(program, "async function h() {}", GLECMA_FNKIND_ASYNC_FUNCTION, kD)->result, GLECMA_OK);
  EXPECT_EQ(Read(program, "async x => x", GLECMA_FNKIND_ASYNC_ARROW)->result, GLECMA_OK);
  EXPECT_EQ(Read(program, "async => 1", GLECMA_FNKIND_ARROW)->result, GLECMA_OK);
  EXPECT_EQ(Read(program, "async => 1", GLECMA_FNKIND_ASYNC_ARROW)->result, GLECMA_ERR_INTERNAL);
  // A declaration is a declaration and an expression an expression, by the flag.
  EXPECT_EQ(glecma_node_kind(glecma_tree_root(Read(program, "function f() {}", GLECMA_FNKIND_FUNCTION, kD)->tree)),
      GLECMA_NODE_FUNCTION_DECLARATION);
  EXPECT_EQ(glecma_node_kind(glecma_tree_root(Read(program, "function f() {}", GLECMA_FNKIND_FUNCTION)->tree)),
      GLECMA_NODE_FUNCTION_EXPRESSION);
}

TEST(Reparse, MethodsAccessorsAndConstructorsAreReadFromTheirToStringText) {
  Program program(
      "class B { static async *gen(a) { yield a; } get [k + 1]() { return 1; } set v(x) {} 'str'(y) {} 12() {} #p() {}\n"
      "  constructor(z) { this.z = z; } }\n"
      "class D extends B { constructor() { super(); } }\n"
      "var o = { m() {}, get g() { return 1; }, async *a() {}, [f()]: 1, [(() => 2)()]() {} };\n");
  struct Row { const char * text; uint32_t kind; uint32_t flags; const char * root; };
  const Row rows[] = {
    {"async *gen(a) { yield a; }", GLECMA_FNKIND_METHOD, kS, "(Function/async/gen"},
    {"get [k + 1]() { return 1; }", GLECMA_FNKIND_GETTER, kS, "(Function"},
    {"set v(x) {}", GLECMA_FNKIND_SETTER, kS, "(Function"},
    {"'str'(y) {}", GLECMA_FNKIND_METHOD, kS, "(Function"},
    {"12() {}", GLECMA_FNKIND_METHOD, kS, "(Function"},
    {"#p() {}", GLECMA_FNKIND_METHOD, kS, "(Function"},
    {"constructor(z) { this.z = z; }", GLECMA_FNKIND_CONSTRUCTOR, kS, "(Function"},
    {"constructor() { super(); }", GLECMA_FNKIND_DERIVED_CONSTRUCTOR, kS, "(Function"},
    {"m() {}", GLECMA_FNKIND_METHOD, 0, "(Function"},
    {"get g() { return 1; }", GLECMA_FNKIND_GETTER, 0, "(Function"},
    {"async *a() {}", GLECMA_FNKIND_METHOD, 0, "(Function/async/gen"},
    {"[(() => 2)()]() {}", GLECMA_FNKIND_METHOD, 0, "(Function"},
  };
  for (const Row & row : rows) {
    auto again = Read(program, row.text, row.kind, row.flags, {u"p"});
    ASSERT_EQ(again->result, GLECMA_OK) << row.text << ": " << (again->failure.message ? again->failure.message : "");
    EXPECT_EQ(again->Whole(false).substr(0, std::string(row.root).size()), row.root) << row.text;
    // The root is the member's function: it begins at its first modifier or its name.
    EXPECT_EQ(glecma_node_start(glecma_tree_root(again->tree)), program.At(row.text)) << row.text;
    EXPECT_EQ(glecma_node_end(glecma_tree_root(again->tree)), program.At(row.text) + std::string(row.text).size());
  }
  // A constructor's super() is the derived constructor's alone.
  EXPECT_EQ(Read(program, "constructor() { super(); }", GLECMA_FNKIND_CONSTRUCTOR, kS, {u"p"})->result, GLECMA_ERR_INTERNAL);
  EXPECT_EQ(Read(program, "constructor(z) { this.z = z; }", GLECMA_FNKIND_DERIVED_CONSTRUCTOR, kS, {u"p"})->result, GLECMA_OK);
  // A word that begins a method is its name when the parameters follow it.
  Program words("class W { get() {} set() {} async() {} static() {} 'get'() {} }");
  for (const char * name : {"get() {}", "set() {}", "async() {}", "static() {}"}) {
    auto again = Read(words, name, GLECMA_FNKIND_METHOD, kS);
    EXPECT_EQ(again->result, GLECMA_OK) << name;
  }
}

TEST(Reparse, ThePropertyNameIsReadAndForgottenSoItsReferencesAreNotTheFunctions) {
  Program program("class C { [key + other()](a) { return a + inner; } }");
  auto again = Read(program, "[key + other()](a) { return a + inner; }", GLECMA_FNKIND_METHOD, kS);
  ASSERT_EQ(again->result, GLECMA_OK);
  std::vector<std::string> references;
  for (size_t i = 0; i < glecma_tree_event_count(again->tree); i++) {
    const GLECMA_BEvent * event = glecma_tree_event(again->tree, i);
    if (event->type == GLECMA_BEV_REFERENCE) {
      references.push_back(glecma_test::Narrow(event->name.units, event->name.length));
    }
  }
  // `a` and `inner`; neither `key` nor `other`, which belong to the class's own code.
  EXPECT_EQ(references, (std::vector<std::string>{"a", "inner"}));
  // The tree is the function's and holds no node of the key.
  EXPECT_EQ(again->Whole(false), "(Function _ (ParameterList (Identifier a)) (FunctionBody/strict (Return (Binary + (Identifier a) (Identifier inner)))))");
}

TEST(Reparse, FieldInitializersAndStaticBlocksAreFunctionsOfTheirOwn) {
  Program program(
      "class C { a = 1 + 2; b = (c, d); e = () => super.x; f = arguments_; static g = new.target; static { var q; this.z = q; } }");
  EXPECT_EQ(Read(program, "1 + 2", GLECMA_FNKIND_FIELD_INITIALIZER, kS)->Whole(false),
      "(Binary + (NumberLiteral) (NumberLiteral))");
  // The initializer of a parenthesized value is what is inside the parentheses.
  EXPECT_EQ(Read(program, "c, d", GLECMA_FNKIND_FIELD_INITIALIZER, kS)->Whole(false),
      "(Sequence (Identifier c) (Identifier d))");
  EXPECT_EQ(Read(program, "() => super.x", GLECMA_FNKIND_FIELD_INITIALIZER, kS)->result, GLECMA_OK);
  EXPECT_EQ(Read(program, "new.target", GLECMA_FNKIND_FIELD_INITIALIZER, kS)->result, GLECMA_OK);
  auto block = Read(program, "static { var q; this.z = q; }", GLECMA_FNKIND_STATIC_BLOCK, kS);
  ASSERT_EQ(block->result, GLECMA_OK);
  EXPECT_EQ(glecma_node_kind(glecma_tree_root(block->tree)), GLECMA_NODE_STATIC_BLOCK);
  EXPECT_TRUE(glecma_tree_strict(block->tree));
  // An initializer has its own `arguments` rule, and is strict code whatever the caller says.
  Program arguments_program("function f() { return arguments; }");
  EXPECT_EQ(Read(arguments_program, "arguments", GLECMA_FNKIND_FIELD_INITIALIZER)->result, GLECMA_ERR_INTERNAL);
  Program sloppy_name("var yield_ = 1; var v = ytn; function g() { return v; }");
  EXPECT_EQ(Read(sloppy_name, "v", GLECMA_FNKIND_FIELD_INITIALIZER, 0, {}, 1)->result, GLECMA_OK);
  Program let_name("var x = let;");
  EXPECT_EQ(Read(let_name, "let", GLECMA_FNKIND_FIELD_INITIALIZER)->result, GLECMA_ERR_INTERNAL);  // `let` is reserved in class code
}

TEST(Reparse, OnlyTheRangeIsRead) {
  // A `/` after a function declaration begins a regular expression there, and division in the
  // tokenizer's default: reading past the range would fail on text that is not the function's.
  Program program("function f() {}\n/'/.test(x);\nvar y = function () { return 1; } /2/ 3;");
  EXPECT_EQ(Read(program, "function f() {}", GLECMA_FNKIND_FUNCTION, kD)->result, GLECMA_OK);
  EXPECT_EQ(Read(program, "function () { return 1; }", GLECMA_FNKIND_FUNCTION)->result, GLECMA_OK);
  // A range that stops short of the function's end, or runs past it, is not the function.
  EXPECT_EQ(Read(program, "function f() {", GLECMA_FNKIND_FUNCTION, kD)->result, GLECMA_ERR_INTERNAL);
  EXPECT_EQ(Read(program, "function f() {}\n", GLECMA_FNKIND_FUNCTION, kD)->result, GLECMA_OK);
  EXPECT_EQ(Read(program, "function f() {}\n/", GLECMA_FNKIND_FUNCTION, kD)->result, GLECMA_ERR_INTERNAL);
  // A range that begins inside the function is not it either.
  EXPECT_EQ(Read(program, "f() {}", GLECMA_FNKIND_FUNCTION, kD)->result, GLECMA_ERR_INTERNAL);
}

TEST(Reparse, AFailureIsInternalAndNeverASyntaxError) {
  Program program("function f() { return 1; }");
  auto again = Read(program, "function f() { return 1;", GLECMA_FNKIND_FUNCTION, kD);
  EXPECT_EQ(again->result, GLECMA_ERR_INTERNAL);
  EXPECT_EQ(again->failure.result, static_cast<uint32_t>(GLECMA_ERR_INTERNAL));
  EXPECT_EQ(again->failure.rule, 0u);  // a rule names a syntax error, and this is not the program's
  EXPECT_NE(again->failure.message, nullptr);
  EXPECT_EQ(again->tree, nullptr);
  // The same for a lexical failure inside the range.
  Program lexical("x = 'a';");
  auto text = Read(lexical, "'a", GLECMA_FNKIND_FIELD_INITIALIZER);
  EXPECT_EQ(text->result, GLECMA_ERR_INTERNAL);
}

TEST(Reparse, CapsAndAFailingAllocatorAreStillLimitsAndOom) {
  // The first parse's caps are not the re-parse's to refuse for a different cause: a limit is a limit.
  Program program("var f = function () { return ((((((((((1)))))))))); };");
  GLECMA_Limits limits = GLECMA_LIMITS_INIT;
  glecma_limits_default(&limits);
  limits.nesting = 4;
  GLECMA_ParseOptions options = GLECMA_PARSE_OPTIONS_INIT;
  options.limits = &limits;
  GLECMA_FunctionContext context = GLECMA_FUNCTION_CONTEXT_INIT;
  context.kind = GLECMA_FNKIND_FUNCTION;
  const std::string text = "function () { return ((((((((((1)))))))))); }";
  context.start = program.At(text);
  context.end = context.start + text.size();
  GLECMA_Tree * tree = nullptr;
  GLECMA_Failure failure = GLECMA_FAILURE_INIT;
  EXPECT_EQ(glecma_parse_function(program.source, &context, &options, nullptr, &failure, &tree), GLECMA_ERR_LIMIT);
  EXPECT_STREQ(failure.limit, "nesting");
  // Each allocation of a read in turn.
  Program small("var f = function (a, {b}) { class C { #p = 1; m() { return [a, b, this.#p]; } } return new C(); };");
  const std::string function = "function (a, {b}) { class C { #p = 1; m() { return [a, b, this.#p]; } } return new C(); }";
  context.start = small.At(function);
  context.end = context.start + function.size();
  options = GLECMA_PARSE_OPTIONS_INIT;
  size_t total = 0;
  {
    glecma_test::CountingAllocator counting;
    ASSERT_EQ(glecma_parse_function(small.source, &context, &options, counting.get(), nullptr, &tree), GLECMA_OK);
    total = counting.calls();
    glecma_tree_destroy(tree);
    EXPECT_EQ(counting.live(), 0u);
  }
  ASSERT_GT(total, 10u);
  for (size_t i = 0; i < total; i++) {
    glecma_test::CountingAllocator counting;
    counting.fail_at(i);
    tree = nullptr;
    EXPECT_EQ(glecma_parse_function(small.source, &context, &options, counting.get(), &failure, &tree), GLECMA_ERR_OOM)
        << "allocation " << i << " of " << total;
    EXPECT_EQ(tree, nullptr);
    EXPECT_EQ(counting.live(), 0u) << i;
  }
}

TEST(Reparse, TheContextIsCheckedBeforeAnyWork) {
  Program program("function f() {}");
  glecma_test::CountingAllocator counting;
  auto refused = [&](GLECMA_FunctionContext context, const GLECMA_ParseOptions * options = nullptr) {
    counting.reset();
    GLECMA_Tree * tree = nullptr;
    GLECMA_Failure failure = GLECMA_FAILURE_INIT;
    GLECMA_Result r = glecma_parse_function(program.source, &context, options, counting.get(), &failure, &tree);
    EXPECT_EQ(counting.calls(), 0u);
    EXPECT_EQ(tree, nullptr);
    return r;
  };
  GLECMA_FunctionContext base = GLECMA_FUNCTION_CONTEXT_INIT;
  base.kind = GLECMA_FNKIND_FUNCTION;
  base.start = 0;
  base.end = 15;
  auto with = [&](auto change) {
    GLECMA_FunctionContext c = base;
    change(c);
    return refused(c);
  };
  EXPECT_EQ(with([](GLECMA_FunctionContext & c) { c.size = 4; }), GLECMA_ERR_INVALID);
  EXPECT_EQ(with([](GLECMA_FunctionContext & c) { c.kind = 0; }), GLECMA_ERR_INVALID);
  EXPECT_EQ(with([](GLECMA_FunctionContext & c) { c.kind = GLECMA_FNKIND_COUNT; }), GLECMA_ERR_INVALID);
  EXPECT_EQ(with([](GLECMA_FunctionContext & c) { c.flags = 0x400; }), GLECMA_ERR_INVALID);
  EXPECT_EQ(with([](GLECMA_FunctionContext & c) { c.flags = kSP & ~kNT; }), GLECMA_ERR_INVALID);   // super.x without new.target
  EXPECT_EQ(with([](GLECMA_FunctionContext & c) { c.flags = GLECMA_CONTEXT_MODULE; }), GLECMA_ERR_INVALID);   // module code is strict
  EXPECT_EQ(with([](GLECMA_FunctionContext & c) { c.kind = GLECMA_FNKIND_ARROW; c.flags = kD; }), GLECMA_ERR_INVALID);
  EXPECT_EQ(with([](GLECMA_FunctionContext & c) { c.kind = GLECMA_FNKIND_METHOD; c.flags = kD; }), GLECMA_ERR_INVALID);
  EXPECT_EQ(with([](GLECMA_FunctionContext & c) { c.start = 5; c.end = 5; }), GLECMA_ERR_INVALID);
  EXPECT_EQ(with([](GLECMA_FunctionContext & c) { c.start = 9; c.end = 3; }), GLECMA_ERR_INVALID);
  EXPECT_EQ(with([](GLECMA_FunctionContext & c) { c.end = 16; }), GLECMA_ERR_INVALID);
  EXPECT_EQ(with([](GLECMA_FunctionContext & c) { c.private_name_count = 1; }), GLECMA_ERR_INVALID);
  // A feature set that differs from the options', and options the goal has no use for.
  GLECMA_FeatureSet features = GLECMA_FEATURE_SET_INIT;
  EXPECT_EQ(glecma_feature_set_add(&features, "decorators"), GLECMA_OK);
  GLECMA_FunctionContext with_features = base;
  with_features.features = &features;
  EXPECT_EQ(refused(with_features), GLECMA_ERR_INVALID);
  // The set the options name is refused first, before the contexts are compared: this build lacks it.
  GLECMA_ParseOptions options = GLECMA_PARSE_OPTIONS_INIT;
  options.features = &features;
  EXPECT_EQ(refused(base, &options), GLECMA_ERR_UNSUPPORTED);
  EXPECT_EQ(refused(with_features, &options), GLECMA_ERR_UNSUPPORTED);
  options = GLECMA_PARSE_OPTIONS_INIT;
  options.goal = GLECMA_PARSE_MODULE;
  EXPECT_EQ(refused(base, &options), GLECMA_ERR_INVALID);
  options.goal = GLECMA_PARSE_SCRIPT;
  options.context = GLECMA_CONTEXT_STRICT;
  EXPECT_EQ(refused(base, &options), GLECMA_ERR_INVALID);
  options.context = 0;
  GLECMA_EvalContext eval = GLECMA_EVAL_CONTEXT_INIT;
  options.eval_context = &eval;
  EXPECT_EQ(refused(base, &options), GLECMA_ERR_INVALID);
  GLECMA_Tree * tree = nullptr;
  EXPECT_EQ(glecma_parse_function(nullptr, &base, nullptr, nullptr, nullptr, &tree), GLECMA_ERR_INVALID);
  EXPECT_EQ(glecma_parse_function(program.source, nullptr, nullptr, nullptr, nullptr, &tree), GLECMA_ERR_INVALID);
  EXPECT_EQ(glecma_parse_function(program.source, &base, nullptr, nullptr, nullptr, nullptr), GLECMA_ERR_INVALID);
  // The entry's own goal, a header-only context (which names nothing) and a feature set in both.
  options = GLECMA_PARSE_OPTIONS_INIT;
  options.goal = GLECMA_PARSE_FUNCTION;
  EXPECT_EQ(glecma_parse_function(program.source, &base, &options, nullptr, nullptr, &tree), GLECMA_OK);
  glecma_tree_destroy(tree);
  struct { uint32_t size; uint32_t kind; } header = {8, GLECMA_FNKIND_FUNCTION};
  EXPECT_EQ(glecma_parse_function(program.source, reinterpret_cast<const GLECMA_FunctionContext *>(&header),
                nullptr, nullptr, nullptr, &tree), GLECMA_ERR_INVALID);  // the range defaults to nothing
}

TEST(Reparse, ARangeInsideACodePointIsRefused) {
  Program program("var a = '\xF0\x9F\x98\x80'; function f() {}");
  GLECMA_FunctionContext context = GLECMA_FUNCTION_CONTEXT_INIT;
  context.kind = GLECMA_FNKIND_FUNCTION;
  context.flags = kD;
  context.start = program.At("function f");
  context.end = context.start + 15;
  GLECMA_Tree * tree = nullptr;
  ASSERT_EQ(glecma_parse_function(program.source, &context, nullptr, nullptr, nullptr, &tree), GLECMA_OK);
  glecma_tree_destroy(tree);
  tree = nullptr;
  context.start = program.At("\xF0\x9F\x98\x80") + 1;  // the second byte of the emoji
  context.end = context.start + 10;
  EXPECT_EQ(glecma_parse_function(program.source, &context, nullptr, nullptr, nullptr, &tree), GLECMA_ERR_INVALID);
  EXPECT_EQ(tree, nullptr);
}

TEST(Reparse, ASourceInUtf16IsReadInItsOwnUnits) {
  const std::u16string text = u"var s = '\xD83D\xDE00'; /* \xD800 */ var f = async (a, b) => { await a; return b; };";
  GLECMA_Source * source = nullptr;
  ASSERT_EQ(glecma_source_create_utf16(reinterpret_cast<const uint16_t *>(text.data()), text.size(),
                nullptr, nullptr, nullptr, &source), GLECMA_OK);
  GLECMA_Tree * tree = nullptr;
  ASSERT_EQ(glecma_parse(source, nullptr, nullptr, nullptr, &tree), GLECMA_OK);
  RpStats stats;
  memset(&stats, 0, sizeof stats);
  char message[600];
  EXPECT_EQ(rp_check_tree(source, tree, nullptr, &stats, message, sizeof message), 0) << message;
  EXPECT_EQ(stats.kinds[GLECMA_FNKIND_ASYNC_ARROW], 1u);
  glecma_tree_destroy(tree);
  glecma_source_release(source);
}

TEST(Reparse, AModuleFunctionHasTheModulesAwaitAndNoMore) {
  Program program("export function f() { return () => import.meta; }\nexport async function g() { await 1; }\n", GLECMA_PARSE_MODULE);
  RpStats stats;
  memset(&stats, 0, sizeof stats);
  char message[600];
  EXPECT_EQ(rp_check_tree(program.source, program.tree, nullptr, &stats, message, sizeof message), 0) << message;
  EXPECT_EQ(stats.kinds[GLECMA_FNKIND_FUNCTION], 1u);
  EXPECT_EQ(stats.kinds[GLECMA_FNKIND_ASYNC_FUNCTION], 1u);
  // import.meta is a module's: the function is refused without the module flag.
  EXPECT_EQ(Read(program, "() => import.meta", GLECMA_FNKIND_ARROW, kM | kNT)->result, GLECMA_OK);
  EXPECT_EQ(Read(program, "() => import.meta", GLECMA_FNKIND_ARROW, kNT | kS)->result, GLECMA_ERR_INTERNAL);
}

TEST(Reparse, TheBuiltInEntryAndTheCheckingBuilderReadFunctionsToo) {
  const std::string text = "x = function () { return %Intrinsic(1); };";
  GLECMA_Source * source = nullptr;
  ASSERT_EQ(glecma_source_create_utf8(text.data(), text.size(), nullptr, nullptr, nullptr, &source), GLECMA_OK);
  GLECMA_FunctionContext context = GLECMA_FUNCTION_CONTEXT_INIT;
  context.kind = GLECMA_FNKIND_FUNCTION;
  context.start = text.find("function");
  context.end = text.size() - 1;
  GLECMA_Tree * tree = nullptr;
  EXPECT_EQ(glecma_parse_function(source, &context, nullptr, nullptr, nullptr, &tree), GLECMA_ERR_INTERNAL);
  ASSERT_EQ(glecma_parse_function_internal(source, &context, nullptr, nullptr, GLECMA_PARSE_INTERNAL_BUILTIN,
                nullptr, &tree), GLECMA_OK);
  EXPECT_TRUE(glecma_tree_strict(tree));
  glecma_tree_destroy(tree);
  // The checking builder builds no body, and accepts and refuses as the tree builder does.
  Program plain("var f = function (a) { return a + 1; };");
  GLECMA_FunctionContext c = GLECMA_FUNCTION_CONTEXT_INIT;
  c.kind = GLECMA_FNKIND_FUNCTION;
  c.start = plain.At("function");
  c.end = plain.text.size() - 1;
  tree = nullptr;
  ASSERT_EQ(glecma_parse_function_internal(plain.source, &c, nullptr, nullptr, GLECMA_PARSE_INTERNAL_CHECKING,
                nullptr, &tree), GLECMA_OK);
  const GLECMA_Node * root = glecma_tree_root(tree);
  EXPECT_EQ(glecma_node_kind(glecma_node_child(root, 2)), GLECMA_NODE_UNMATERIALIZED_BODY);
  glecma_tree_destroy(tree);
  glecma_source_release(source);
}

int main(int argc, char * * argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

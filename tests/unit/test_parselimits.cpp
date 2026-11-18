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

// What bounds a parse: the nesting, node and native-stack caps, a parse on a
// thread whose stack is small, every allocation failing in turn, the options
// and the feature set, and the internal built-in entry.

#include <ghoti.io/lang-ecmascript/lang-ecmascript.h>

#include <gtest/gtest.h>

#include <pthread.h>

#include <atomic>

#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "../parse_test_support.h"
#include "../test_deadline.h"
#include "../test_helpers.h"

using glecma_test::Parse;

namespace {

std::string Repeat(const std::string & open, const std::string & middle,
    const std::string & close, size_t n) {
  std::string s;
  for (size_t i = 0; i < n; i++) {
    s += open;
  }
  s += middle;
  for (size_t i = 0; i < n; i++) {
    s += close;
  }
  return s;
}

GLECMA_Result ParseLimited(const std::string & text, const GLECMA_Limits & limits,
    GLECMA_Failure * failure = nullptr, uint32_t internal = 0) {
  GLECMA_Source * source = nullptr;
  GLECMA_Failure local = GLECMA_FAILURE_INIT;
  if (failure == nullptr) {
    failure = &local;
  }
  EXPECT_EQ(glecma_source_create_utf8(text.data(), text.size(), nullptr, nullptr,
                nullptr, &source),
      GLECMA_OK);
  GLECMA_ParseOptions options = GLECMA_PARSE_OPTIONS_INIT;
  options.limits = &limits;
  GLECMA_Tree * tree = nullptr;
  GLECMA_Result result = glecma_parse_internal(source, &options, nullptr,
      internal, failure, &tree);
  glecma_tree_destroy(tree);
  glecma_source_release(source);
  return result;
}

// The least value of a field at which `text` parses, found by bisection
// between 0 (refused) and `high` (accepted).
uint64_t LeastAccepted(const std::string & text, uint64_t GLECMA_Limits::*field,
    uint64_t high, uint32_t internal = 0) {
  uint64_t low = 0;
  while (low + 1 < high) {
    uint64_t middle = (low + high) / 2;
    GLECMA_Limits limits = GLECMA_LIMITS_INIT;
    limits.native_stack_bytes = uint64_t{1} << 30;
    limits.nesting = 100000;
    limits.*field = middle;
    if (ParseLimited(text, limits, nullptr, internal) == GLECMA_OK) {
      high = middle;
    } else {
      low = middle;
    }
  }
  return high;
}

// Runs `body` on a thread whose whole stack is `bytes`.
void OnSmallStack(size_t bytes, const std::function<void()> & body) {
  struct Job {
    const std::function<void()> * body;
  } job{&body};
  pthread_attr_t attr;
  ASSERT_EQ(pthread_attr_init(&attr), 0);
  ASSERT_EQ(pthread_attr_setstacksize(&attr, bytes), 0);
  pthread_t thread;
  ASSERT_EQ(pthread_create(&thread, &attr,
                [](void * arg) -> void * {
                  (*static_cast<Job *>(arg)->body)();
                  return nullptr;
                },
                &job),
      0);
  pthread_join(thread, nullptr);
  pthread_attr_destroy(&attr);
}

}  // namespace

TEST(ParseLimits, TheDefaultsAreTheMeasuredOnes) {
  GLECMA_Limits limits;
  std::memset(&limits, 0, sizeof limits);
  limits.size = sizeof limits;
  ASSERT_EQ(glecma_limits_default(&limits), GLECMA_OK);
  EXPECT_EQ(limits.nesting, 256u);
  EXPECT_EQ(limits.nodes, uint64_t{16} * 1024 * 1024);
  EXPECT_EQ(limits.native_stack_bytes, uint64_t{1} << 20);
  EXPECT_EQ(limits.nesting, GLECMA_LIMIT_NESTING_DEFAULT);
  EXPECT_EQ(limits.nodes, GLECMA_LIMIT_NODES_DEFAULT);
  EXPECT_EQ(limits.native_stack_bytes, GLECMA_LIMIT_NATIVE_STACK_BYTES_DEFAULT);
}

TEST(ParseLimits, ALimitsStructBuiltBeforeTheParserKeepsItsMeaning) {
  // A caller compiled against the three-cap struct: the parser's caps take
  // their defaults, and the caps it did set are honoured.
  struct Short {
    uint32_t size;
    uint32_t reserved;
    uint64_t source_units;
    uint64_t tokens;
    uint64_t literal_length;
  } shorter = {sizeof(Short), 0, 10, 1000, 1000};
  GLECMA_ParseOptions options = GLECMA_PARSE_OPTIONS_INIT;
  options.limits = reinterpret_cast<const GLECMA_Limits *>(&shorter);
  GLECMA_Source * source = nullptr;
  std::string deep = Repeat("(", "x", ")", 100);
  ASSERT_EQ(glecma_source_create_utf8(deep.data(), deep.size(), nullptr, nullptr,
                nullptr, &source),
      GLECMA_OK);
  GLECMA_Tree * tree = nullptr;
  EXPECT_EQ(glecma_parse(source, &options, nullptr, nullptr, &tree), GLECMA_OK);
  glecma_tree_destroy(tree);
  glecma_source_release(source);
}

TEST(ParseLimits, NestingIsRefusedOneLevelPastItsCapNamingTheField) {
  const std::string shapes[] = {
    Repeat("(", "x", ")", 40), Repeat("[", "x", "]", 40),
    Repeat("{", "", "}", 40), Repeat("function f() {", "", "}", 40),
    Repeat("!", "x", "", 40), Repeat("a = ", "x", "", 40),
    Repeat("a ** ", "x", "", 40), Repeat("if (a) ", "x;", "", 40),
    Repeat("f(", "x", ")", 40), Repeat("`${", "x", "}`", 40),
    Repeat("class A { m() {", "", "}}", 40),
    std::string("class A") + Repeat(" extends class", " extends B {}", " {}", 40),
  };
  for (const std::string & text : shapes) {
    uint64_t least = LeastAccepted(text, &GLECMA_Limits::nesting, 100000);
    EXPECT_GE(least, 40u) << text;
    EXPECT_LE(least, 44u) << text;  // one level for each, and the top level's own
    GLECMA_Limits at = GLECMA_LIMITS_INIT;
    at.nesting = least;
    EXPECT_EQ(ParseLimited(text, at), GLECMA_OK) << text;
    GLECMA_Limits past = GLECMA_LIMITS_INIT;
    past.nesting = least - 1;
    GLECMA_Failure failure = GLECMA_FAILURE_INIT;
    EXPECT_EQ(ParseLimited(text, past, &failure), GLECMA_ERR_LIMIT) << text;
    EXPECT_STREQ(failure.limit, "nesting") << text;
    EXPECT_EQ(failure.rule, 0u);
  }
  // The count itself is pinned, so that a cap one off in either direction is
  // seen: a statement is a level, and so is each parenthesized group.
  EXPECT_EQ(LeastAccepted(Repeat("(", "x", ")", 40), &GLECMA_Limits::nesting, 100000), 42u);
  EXPECT_EQ(LeastAccepted(Repeat("{", "", "}", 40), &GLECMA_Limits::nesting, 100000), 40u);
  // Twice as deep needs twice the cap: the levels grow with the nesting.
  EXPECT_EQ(LeastAccepted(Repeat("(", "x", ")", 80), &GLECMA_Limits::nesting, 100000) -
          LeastAccepted(Repeat("(", "x", ")", 40), &GLECMA_Limits::nesting, 100000),
      40u);
  // A long left-leaning chain of operators is not nesting: it is a loop.
  std::string chain = "a";
  for (int i = 0; i < 20000; i++) {
    chain += " + a";
  }
  GLECMA_Limits tight = GLECMA_LIMITS_INIT;
  tight.nesting = 4;
  EXPECT_EQ(ParseLimited(chain, tight), GLECMA_OK);
  // A cap of zero is a real cap of zero.
  tight.nesting = 0;
  EXPECT_EQ(ParseLimited("a;", tight), GLECMA_ERR_LIMIT);
}

TEST(ParseLimits, NodesAreCountedTheSameByBothBuilders) {
  const std::string programs[] = {
    "a;", "a + b * c;", "function f(a, b) { return a + b; }",
    "x = (a, b) => { let c = [a, b]; return c; };", "class A { m() { return 1; } }",
    "[a, b] = c;", "(a = (b) => 1) => 2;",
  };
  for (const std::string & text : programs) {
    uint64_t tree = LeastAccepted(text, &GLECMA_Limits::nodes, 100000, 0);
    uint64_t checking = LeastAccepted(text, &GLECMA_Limits::nodes, 100000,
        GLECMA_PARSE_INTERNAL_CHECKING);
    EXPECT_EQ(tree, checking) << text;
    GLECMA_Limits past = GLECMA_LIMITS_INIT;
    past.nodes = tree - 1;
    GLECMA_Failure failure = GLECMA_FAILURE_INIT;
    EXPECT_EQ(ParseLimited(text, past, &failure), GLECMA_ERR_LIMIT) << text;
    EXPECT_STREQ(failure.limit, "nodes");
    EXPECT_EQ(ParseLimited(text, past, nullptr, GLECMA_PARSE_INTERNAL_CHECKING),
        GLECMA_ERR_LIMIT)
        << text;
    // The tree holds no more nodes than the cap that admits it.
    auto parsed = Parse(text);
    size_t count = 0;
    std::vector<const GLECMA_Node *> stack = {glecma_tree_root(parsed.tree)};
    while (!stack.empty()) {
      const GLECMA_Node * node = stack.back();
      stack.pop_back();
      if (node == nullptr) {
        continue;
      }
      count++;
      for (size_t i = 0; i < glecma_node_child_count(node); i++) {
        stack.push_back(glecma_node_child(node, i));
      }
    }
    EXPECT_LE(count, tree) << text;
  }
  // The reread of arrow parameters and patterns does not count twice: a node
  // that a rewind discarded is not counted, so the cap that admits a program
  // is exactly the nodes of the tree it makes (the body of an arrow function
  // with an expression body counts as a node: the checking builder makes one
  // for it, and the two builders must count alike).
  for (const char * text : {"(a, b, c, d) => 1;", "[a, b, {c}, ...d] = e;",
           "async (a, b) => { c }; for ([a, b] of c);"}) {
    auto parsed = Parse(text);
    ASSERT_EQ(parsed.result, GLECMA_OK) << text;
    size_t count = 0;
    std::vector<const GLECMA_Node *> stack = {glecma_tree_root(parsed.tree)};
    while (!stack.empty()) {
      const GLECMA_Node * node = stack.back();
      stack.pop_back();
      if (node == nullptr) {
        continue;
      }
      count++;
      if ((glecma_node_flags(node) & GLECMA_NODEF_EXPRESSION_BODY) != 0) {
        count++;
      }
      for (size_t i = 0; i < glecma_node_child_count(node); i++) {
        stack.push_back(glecma_node_child(node, i));
      }
    }
    EXPECT_EQ(LeastAccepted(text, &GLECMA_Limits::nodes, 1000), count) << text;
  }
}

TEST(ParseLimits, NativeStackBytesRefusesBeforeTheStackIsUsedUp) {
  const std::string text = Repeat("(", "x", ")", 200);
  GLECMA_Limits limits = GLECMA_LIMITS_INIT;
  limits.nesting = 100000;
  limits.native_stack_bytes = 16 * 1024;  // below what 200 levels take
  GLECMA_Failure failure = GLECMA_FAILURE_INIT;
  EXPECT_EQ(ParseLimited(text, limits, &failure), GLECMA_ERR_LIMIT);
  EXPECT_STREQ(failure.limit, "native_stack_bytes");
  // The least grant that admits it is within what the depth can take.
  uint64_t least = LeastAccepted(text, &GLECMA_Limits::native_stack_bytes,
      uint64_t{1} << 28);
  EXPECT_GT(least, 24u * 1024);
  EXPECT_LT(least, uint64_t{200} * 16384);  // a sanitizer's frames are several times larger
  // A grant of zero is a real zero: no parse can start.
  limits.native_stack_bytes = 0;
  EXPECT_EQ(ParseLimited("a;", limits, &failure), GLECMA_ERR_LIMIT);
  EXPECT_STREQ(failure.limit, "native_stack_bytes");
}

TEST(ParseLimits, AParseOnAThreadWithASmallStackNeverFaults) {
  // The thread has 256 KiB. The grant is what it can spare, and input far
  // deeper than that stack could hold is refused by naming the field. Were
  // the check wrong, this would fault and the whole suite would end.
  const std::string shapes[] = {
    Repeat("(", "x", ")", 20000), Repeat("[", "x", "]", 20000),
    Repeat("function f() {", "", "}", 20000), Repeat("a = ", "x", "", 20000),
    Repeat("class A { m() {", "", "}}", 20000), Repeat("!", "x", "", 20000),
    Repeat("() =>", "x", "", 20000), Repeat("if (a) ", "x;", "", 20000),
    Repeat("`${", "x", "}`", 20000), Repeat("a ? b : ", "x", "", 20000),
    std::string("class A") + Repeat(" extends class", " extends B {}", " {}", 20000),
    Repeat("[a=", "1", "]=1", 20000), Repeat("({a=", "1", "}=1)", 20000),
  };
  std::atomic<int> refused{0};
  OnSmallStack(256 * 1024, [&] {
    for (const std::string & text : shapes) {
      GLECMA_Limits limits = GLECMA_LIMITS_INIT;
      limits.nesting = 1000000;
      limits.nodes = 100000000;
      limits.native_stack_bytes = 128 * 1024;
      GLECMA_Failure failure = GLECMA_FAILURE_INIT;
      GLECMA_Result r = ParseLimited(text, limits, &failure);
      if (r == GLECMA_ERR_LIMIT && std::strcmp(failure.limit, "native_stack_bytes") == 0) {
        refused++;
      }
    }
  });
  EXPECT_EQ(refused.load(), 13);
}

#if !defined(__SANITIZE_ADDRESS__) && !defined(__SANITIZE_THREAD__)
// The default nesting cap against the native stack the design states: the
// deepest input of each shape that the default cap accepts parses, with the
// default limits, on a thread that has the stack the default grants. This is
// the release build's frames; a sanitizer's are larger, and its arms check
// only that nothing faults.
TEST(ParseLimits, TheDeepestInputTheDefaultNestingAcceptsParsesOnADefaultStack) {
  glecma_test::Deadline deadline(std::chrono::seconds(120), "the deepest-input search did not finish");
  struct Shape {
    const char * name;
    std::function<std::string(size_t)> make;
  };
  const Shape shapes[] = {
    {"paren", [](size_t n) { return Repeat("(", "x", ")", n); }},
    {"array", [](size_t n) { return Repeat("[", "x", "]", n); }},
    {"object", [](size_t n) { return Repeat("({a:", "x", "})", n); }},
    {"block", [](size_t n) { return Repeat("{", "", "}", n); }},
    {"function", [](size_t n) { return Repeat("function f() {", "", "}", n); }},
    {"arrow", [](size_t n) { return Repeat("() =>", "x", "", n); }},
    {"call", [](size_t n) { return Repeat("f(", "x", ")", n); }},
    {"unary", [](size_t n) { return Repeat("!", "x", "", n); }},
    {"assign", [](size_t n) { return Repeat("a = ", "x", "", n); }},
    {"exponent", [](size_t n) { return Repeat("a ** ", "x", "", n); }},
    {"conditional", [](size_t n) { return Repeat("a ? b : ", "x", "", n); }},
    {"template", [](size_t n) { return Repeat("`${", "x", "}`", n); }},
    {"if", [](size_t n) { return Repeat("if (a) ", "x;", "", n); }},
    {"class", [](size_t n) { return Repeat("class A { m() {", "", "}}", n); }},
    {"pattern", [](size_t n) { return Repeat("[", "x", "]", n) + " = y;"; }},
    {"arrow-parameter", [](size_t n) { return Repeat("(a = ", "x", ") => 0", n); }},
    {"new", [](size_t n) { return Repeat("new ", "x", "", n); }},
    // Each label is its own: a label cannot be declared inside itself.
    {"label", [](size_t n) {
      std::string out;
      for (size_t i = 0; i < n; i++) {
        out += "l" + std::to_string(i) + ": ";
      }
      return out + "x;";
    }},
    {"await", [](size_t n) { return Repeat("async function f() { await ", "x", "}", n); }},
    {"heritage", [](size_t n) { return std::string("class A") + Repeat(" extends class", " extends B {}", " {}", n); }},
  };
  for (const Shape & shape : shapes) {
    // The deepest the default nesting cap admits, found with the stack free.
    size_t low = 1, high = 4096;
    while (low + 1 < high) {
      size_t middle = (low + high) / 2;
      GLECMA_Limits free_stack = GLECMA_LIMITS_INIT;
      free_stack.native_stack_bytes = uint64_t{1} << 30;
      (ParseLimited(shape.make(middle), free_stack) == GLECMA_OK ? low : high) = middle;
    }
    ASSERT_GT(low, 30u) << shape.name;
    const std::string deepest = shape.make(low);
    std::atomic<int> result{-1};
    OnSmallStack(size_t{1} << 20, [&] {
      GLECMA_Limits limits = GLECMA_LIMITS_INIT;  // the defaults
      GLECMA_Failure failure = GLECMA_FAILURE_INIT;
      result = ParseLimited(deepest, limits, &failure);
    });
    EXPECT_EQ(result.load(), static_cast<int>(GLECMA_OK))
        << shape.name << " at depth " << low << " on a 1 MiB stack";
  }
}
#endif

TEST(ParseLimits, EveryAllocationOfAParseMayFailWithNothingLeaked) {
  const std::string unit =
      "let [a, {b = 1, ...c}] = d;\n"
      "class K extends L { static #p = 1; get x() { return this.#p; } static { q; } *m(...r) { for (const s of r) yield s; } }\n"
      "const f = async (u = 1, [v]) => { await u; return `t${v}u`; };\n"
      "x = (a, b) => a ? /re+/gi.test(b) : { y, z: [1, 2], ...w };\n"
      "[e.f, g[0]] = h; ({i, j: k = 2} = l); a?.b?.(c);\n"
      "label: for (var m in n) { if (o) continue label; else break label; }\n"
      "try { p } catch ({ q }) { r } finally { s } switch (t) { case 1: default: }\n";
  // Repeated until the arenas, the event list and the name table have each
  // grown more than once, so that the sweep reaches those allocations.
  std::string text = "'use strict';\n";
  for (int i = 0; i < 60; i++) {
    text += unit;
  }
  for (uint32_t internal : {0u, GLECMA_PARSE_INTERNAL_CHECKING}) {
    size_t total = 0;
    {
      glecma_test::CountingAllocator counting;
      GLECMA_Source * source = nullptr;
      ASSERT_EQ(glecma_source_create_utf8(text.data(), text.size(), nullptr, nullptr,
                    nullptr, &source),
          GLECMA_OK);
      GLECMA_Tree * tree = nullptr;
      GLECMA_Failure failure = GLECMA_FAILURE_INIT;
      ASSERT_EQ(glecma_parse_internal(source, nullptr, counting.get(), internal,
                    &failure, &tree),
          GLECMA_OK)
          << failure.message << " at " << failure.location.offset;
      total = counting.calls();
      glecma_tree_destroy(tree);
      EXPECT_EQ(counting.live(), 0u);
      glecma_source_release(source);
    }
    ASSERT_GT(total, 12u);
    GLECMA_Source * source = nullptr;
    ASSERT_EQ(glecma_source_create_utf8(text.data(), text.size(), nullptr, nullptr,
                  nullptr, &source),
        GLECMA_OK);
    for (size_t i = 0; i < total; i++) {
      glecma_test::CountingAllocator counting;
      counting.fail_at(i);
      GLECMA_Tree * tree = nullptr;
      GLECMA_Failure failure = GLECMA_FAILURE_INIT;
      GLECMA_Result r = glecma_parse_internal(source, nullptr, counting.get(),
          internal, &failure, &tree);
      EXPECT_EQ(r, GLECMA_ERR_OOM) << "allocation " << i << " of " << total;
      EXPECT_EQ(tree, nullptr);
      EXPECT_EQ(failure.result, static_cast<uint32_t>(GLECMA_ERR_OOM));
      EXPECT_EQ(counting.live(), 0u) << "allocation " << i;
    }
    glecma_source_release(source);
  }
}

TEST(ParseLimits, AFailedParseLeaksNothingAtAnyPointOfTheGrammar) {
  // Every prefix of a program is cut at a token: most are refused, each is
  // freed.
  const std::string text =
      "class A { static m(a = [1, {b}], ...c) { return `x${a}`; } } x = (y, z) => y ? z : [w] = v;";
  GLECMA_Source * keep = nullptr;
  for (size_t cut = 0; cut <= text.size(); cut += 1) {
    glecma_test::CountingAllocator counting;
    std::string prefix = text.substr(0, cut);
    GLECMA_Source * source = nullptr;
    ASSERT_EQ(glecma_source_create_utf8(prefix.data(), prefix.size(), nullptr,
                  nullptr, nullptr, &source),
        GLECMA_OK);
    GLECMA_Tree * tree = nullptr;
    GLECMA_Result r = glecma_parse(source, nullptr, counting.get(), nullptr, &tree);
    EXPECT_TRUE(r == GLECMA_OK || r == GLECMA_ERR_SYNTAX) << cut;
    glecma_tree_destroy(tree);
    EXPECT_EQ(counting.live(), 0u) << cut;
    glecma_source_release(source);
  }
  (void)keep;
}

TEST(ParseOptions, AFeatureTheBuildLacksIsRefusedBeforeAnyWork) {
  glecma_test::CountingAllocator counting;
  GLECMA_Source * source = nullptr;
  ASSERT_EQ(glecma_source_create_utf8("a;", 2, nullptr, nullptr, nullptr, &source),
      GLECMA_OK);
  GLECMA_FeatureSet features = GLECMA_FEATURE_SET_INIT;
  ASSERT_EQ(glecma_feature_set_add(&features, "decorators"), GLECMA_OK);
  GLECMA_ParseOptions options = GLECMA_PARSE_OPTIONS_INIT;
  options.features = &features;
  GLECMA_Tree * tree = nullptr;
  GLECMA_Failure failure = GLECMA_FAILURE_INIT;
  EXPECT_EQ(glecma_parse(source, &options, counting.get(), &failure, &tree),
      GLECMA_ERR_UNSUPPORTED);
  EXPECT_STREQ(failure.feature, "decorators");
  EXPECT_EQ(counting.calls(), 0u);  // before any allocation
  EXPECT_EQ(tree, nullptr);
  // The same through the checking builder, and with the first of two named.
  ASSERT_EQ(glecma_feature_set_add(&features, "other"), GLECMA_OK);
  EXPECT_EQ(glecma_parse_internal(source, &options, counting.get(),
                GLECMA_PARSE_INTERNAL_CHECKING, &failure, &tree),
      GLECMA_ERR_UNSUPPORTED);
  EXPECT_STREQ(failure.feature, "decorators");
  // An empty set and no set are the pinned edition.
  GLECMA_FeatureSet empty = GLECMA_FEATURE_SET_INIT;
  options.features = &empty;
  EXPECT_EQ(glecma_parse(source, &options, nullptr, nullptr, &tree), GLECMA_OK);
  glecma_tree_destroy(tree);
  glecma_source_release(source);
}

TEST(ParseOptions, GoalAndContextAreCheckedAndAShorterStructTakesTheDefaults) {
  GLECMA_Source * source = nullptr;
  ASSERT_EQ(glecma_source_create_utf8("a;", 2, nullptr, nullptr, nullptr, &source),
      GLECMA_OK);
  GLECMA_Tree * tree = nullptr;
  GLECMA_Failure failure = GLECMA_FAILURE_INIT;
  GLECMA_ParseOptions options = GLECMA_PARSE_OPTIONS_INIT;
  EXPECT_EQ(options.goal, GLECMA_PARSE_SCRIPT);
  EXPECT_EQ(options.context, 0u);
  options.goal = GLECMA_PARSE_MODULE;
  EXPECT_EQ(glecma_parse(source, &options, nullptr, &failure, &tree), GLECMA_ERR_UNSUPPORTED);
  EXPECT_STREQ(failure.message, "the Module goal is not built");
  options.goal = 7;
  EXPECT_EQ(glecma_parse(source, &options, nullptr, &failure, &tree), GLECMA_ERR_INVALID);
  options.goal = GLECMA_PARSE_SCRIPT;
  options.context = 0x80;
  EXPECT_EQ(glecma_parse(source, &options, nullptr, &failure, &tree), GLECMA_ERR_INVALID);
  options.context = GLECMA_CONTEXT_STRICT;
  ASSERT_EQ(glecma_parse(source, &options, nullptr, &failure, &tree), GLECMA_OK);
  EXPECT_TRUE(glecma_tree_strict(tree));
  glecma_tree_destroy(tree);
  // A caller that knows only the header, or the limits and the features,
  // gets a Script with no context.
  struct Older {
    uint32_t size;
    uint32_t reserved;
    const GLECMA_Limits * limits;
    const GLECMA_FeatureSet * features;
  } older = {sizeof(Older), 0, nullptr, nullptr};
  ASSERT_EQ(glecma_parse(source, reinterpret_cast<const GLECMA_ParseOptions *>(&older),
                nullptr, &failure, &tree),
      GLECMA_OK);
  EXPECT_FALSE(glecma_tree_strict(tree));
  EXPECT_EQ(glecma_tree_goal(tree), GLECMA_PARSE_SCRIPT);
  glecma_tree_destroy(tree);
  GLECMA_ParseOptions defaults;
  std::memset(&defaults, 0xAB, sizeof defaults);
  defaults.size = 8;
  ASSERT_EQ(glecma_parse_options_default(&defaults), GLECMA_OK);
  EXPECT_EQ(defaults.goal, 0xABABABABu);  // beyond the declared size: untouched
  defaults.size = sizeof defaults;
  ASSERT_EQ(glecma_parse_options_default(&defaults), GLECMA_OK);
  EXPECT_EQ(defaults.goal, GLECMA_PARSE_SCRIPT);
  EXPECT_EQ(defaults.context, 0u);
  // Arguments that are wrong.
  EXPECT_EQ(glecma_parse(nullptr, nullptr, nullptr, &failure, &tree), GLECMA_ERR_INVALID);
  EXPECT_EQ(glecma_parse(source, nullptr, nullptr, &failure, nullptr), GLECMA_ERR_INVALID);
  GLECMA_ParseOptions tiny = GLECMA_PARSE_OPTIONS_INIT;
  tiny.size = 4;
  EXPECT_EQ(glecma_parse(source, &tiny, nullptr, &failure, &tree), GLECMA_ERR_INVALID);
  GLECMA_Failure small = GLECMA_FAILURE_INIT;
  small.size = 4;
  EXPECT_EQ(glecma_parse(source, nullptr, nullptr, &small, &tree), GLECMA_ERR_INVALID);
  glecma_source_release(source);
}

TEST(ParseOptions, TheInternalBuiltInEntryAcceptsAnIntrinsicAndNoPublicGoalDoes) {
  const std::string text = "%ArrayPrototype(x); y = %B + a % b;";
  auto builtin = Parse(text, false, GLECMA_PARSE_INTERNAL_BUILTIN);
  ASSERT_EQ(builtin.result, GLECMA_OK) << builtin.failure.message;
  EXPECT_TRUE(glecma_tree_strict(builtin.tree));  // the built-in entry is strict
  EXPECT_EQ(builtin.Body(),
      "(Expression (Call (IntrinsicName ArrayPrototype) (Identifier x))) "
      "(Expression (Assignment = (Identifier y) (Binary + (IntrinsicName B) (Binary % (Identifier a) (Identifier b)))))");
  // `%` is the remainder operator in every public goal, as it always was.
  for (bool strict : {false, true}) {
    auto plain = Parse("%ArrayPrototype(x);", strict);
    EXPECT_EQ(plain.result, GLECMA_ERR_SYNTAX);
    EXPECT_EQ(plain.failure.rule, static_cast<uint32_t>(GLECMA_RULE_UNEXPECTED_TOKEN));
    EXPECT_EQ(plain.failure.location.offset, 0u);
    auto remainder = Parse("a %b;", strict);
    EXPECT_EQ(remainder.result, GLECMA_OK);
    EXPECT_EQ(remainder.Body(), "(Expression (Binary % (Identifier a) (Identifier b)))");
  }
  // The Module goal and an explicit feature set do not reach the entry.
  GLECMA_ParseOptions options = GLECMA_PARSE_OPTIONS_INIT;
  options.goal = GLECMA_PARSE_MODULE;
  GLECMA_Source * source = nullptr;
  ASSERT_EQ(glecma_source_create_utf8(text.data(), text.size(), nullptr, nullptr,
                nullptr, &source),
      GLECMA_OK);
  GLECMA_Tree * tree = nullptr;
  EXPECT_EQ(glecma_parse(source, &options, nullptr, nullptr, &tree), GLECMA_ERR_UNSUPPORTED);
  glecma_source_release(source);
}

int main(int argc, char * * argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

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

// The two builders over real programs, the declaration and reference events
// they report, the tree's lifetime and the parse's determinism.

#include <gtest/gtest.h>

#include <atomic>
#include <set>
#include <thread>

#include "../parse_test_support.h"
#include "parse/builder.h"

using glecma_test::Narrow;
using glecma_test::Parse;
using glecma_test::Parsed;

namespace {

const char * ScopeName(uint32_t what) {
  switch (what) {
    case GLECMA_BSCOPE_SCRIPT: return "script";
    case GLECMA_BSCOPE_FUNCTION: return "function";
    case GLECMA_BSCOPE_ARROW: return "arrow";
    case GLECMA_BSCOPE_BLOCK: return "block";
    case GLECMA_BSCOPE_CATCH: return "catch";
    case GLECMA_BSCOPE_FOR: return "for";
    case GLECMA_BSCOPE_CLASS: return "class";
    case GLECMA_BSCOPE_STATIC_BLOCK: return "static";
    case GLECMA_BSCOPE_FIELD_INIT: return "field";
    case GLECMA_BSCOPE_WITH: return "with";
  }
  return "?";
}

const char * DeclName(uint32_t what) {
  switch (what) {
    case GLECMA_BDECL_VAR: return "var";
    case GLECMA_BDECL_LET: return "let";
    case GLECMA_BDECL_CONST: return "const";
    case GLECMA_BDECL_FUNCTION: return "function";
    case GLECMA_BDECL_CLASS: return "class";
    case GLECMA_BDECL_PARAM: return "param";
    case GLECMA_BDECL_CATCH_PARAM: return "catch";
    case GLECMA_BDECL_FUNCTION_NAME: return "fname";
    case GLECMA_BDECL_CLASS_NAME: return "cname";
  }
  return "?";
}

// The events of a tree as one line: `+arrow`, `-arrow`, `let a`, `ref a:rw`,
// `use this`.
std::string Events(const Parsed & p) {
  std::string out;
  for (size_t i = 0; i < glecma_tree_event_count(p.tree); i++) {
    const GLECMA_BEvent * e = glecma_tree_event(p.tree, i);
    if (!out.empty()) {
      out += " ";
    }
    switch (e->type) {
      case GLECMA_BEV_SCOPE_ENTER: out += std::string("+") + ScopeName(e->what); break;
      case GLECMA_BEV_SCOPE_LEAVE: out += std::string("-") + ScopeName(e->what); break;
      case GLECMA_BEV_DECLARE:
        out += std::string(DeclName(e->what)) + " " + Narrow(e->name.units, e->name.length);
        break;
      case GLECMA_BEV_REFERENCE: {
        out += "ref " + Narrow(e->name.units, e->name.length) + ":";
        if (e->what & GLECMA_BREF_READ) out += "r";
        if (e->what & GLECMA_BREF_WRITE) out += "w";
        if (e->what & GLECMA_BREF_TYPEOF) out += "t";
        if (e->what & GLECMA_BREF_DELETE) out += "d";
        break;
      }
      case GLECMA_BEV_USE: {
        static const char * const names[] = {"?", "this", "super.", "super()", "new.target", "eval", "import.meta"};
        out += std::string("use ") + names[e->what];
        break;
      }
    }
  }
  return out;
}

std::string Dump(const Parsed & p) {
  char * buffer = nullptr;
  size_t size = 0;
  FILE * out = open_memstream(&buffer, &size);
  EXPECT_EQ(glecma_tree_dump(p.tree, out), GLECMA_OK);
  std::fclose(out);
  std::string text(buffer, size);
  std::free(buffer);
  return text;
}

}  // namespace

TEST(ParseBuilders, TheCheckingBuilderMakesTheReservedKindForEveryOutermostBody) {
  auto checking = Parse(
      "function f(a = () => 1) { function g() { x } }\n"
      "x = (a) => { y };\n"
      "x = b => b + 1;\n"
      "class C { m() { z } get g() { 1 } static { w } f = () => { v }; }\n",
      false, GLECMA_PARSE_INTERNAL_CHECKING);
  ASSERT_EQ(checking.result, GLECMA_OK);
  EXPECT_TRUE(glecma_tree_is_checking(checking.tree));
  // A body is one node, with no children: what it held was checked and not built.
  EXPECT_EQ(checking.Body(),
      "(FunctionDeclaration (Identifier f) (ParameterList (AssignmentPattern (Identifier a) "
      "(ArrowFunction/exprbody _ (ParameterList) (UnmaterializedBody)))) (UnmaterializedBody)) "
      "(Expression (Assignment = (Identifier x) (ArrowFunction _ (ParameterList (Identifier a)) (UnmaterializedBody)))) "
      "(Expression (Assignment = (Identifier x) (ArrowFunction/exprbody _ (ParameterList (Identifier b)) (UnmaterializedBody)))) "
      "(ClassDeclaration (Identifier C) _ (ClassBody "
      "(MethodDefinition/method (Identifier m) (Function _ (ParameterList) (UnmaterializedBody/strict))) "
      "(MethodDefinition/get (Identifier g) (Function _ (ParameterList) (UnmaterializedBody/strict))) "
      "(StaticBlock (Expression (Identifier w))) "
      "(FieldDefinition (Identifier f) (ArrowFunction _ (ParameterList) (UnmaterializedBody/strict)))))");
  // The public walker and the dump handle it, as they handle any kind.
  EXPECT_NE(Dump(checking).find("UnmaterializedBody ["), std::string::npos);
  auto tree = Parse("function f() { x }");
  EXPECT_EQ(tree.Body().find("Unmaterialized"), std::string::npos);
  EXPECT_FALSE(glecma_tree_is_checking(tree.tree));
}

TEST(ParseBuilders, ThePublicParseNeverBuildsTheReservedKind) {
  auto parsed = Parse("function f() { (function g() { h }); () => { i }; class A { m() {} } }");
  ASSERT_EQ(parsed.result, GLECMA_OK);
  std::vector<const GLECMA_Node *> stack = {glecma_tree_root(parsed.tree)};
  int bodies = 0;
  while (!stack.empty()) {
    const GLECMA_Node * node = stack.back();
    stack.pop_back();
    if (node == nullptr) {
      continue;
    }
    EXPECT_NE(glecma_node_kind(node), GLECMA_NODE_UNMATERIALIZED_BODY);
    bodies += glecma_node_kind(node) == GLECMA_NODE_FUNCTION_BODY;
    for (size_t i = 0; i < glecma_node_child_count(node); i++) {
      stack.push_back(glecma_node_child(node, i));
    }
  }
  EXPECT_EQ(bodies, 4);
}

TEST(ParseBuilders, BothBuildersAcceptAndRefuseTheSameAndNameTheSameError) {
  const char * programs[] = {
    "a;", "function f() { return 1 }", "(a, b) => { c }", "[a, b] = c;", "class A { m() { super.x } }",
    "x = async () => { await y };", "for (const a of b) { c }", "try { a } catch (e) { b }",
    "a +;", "function f() { a + }", "function f() { (a, ) => b }", "class A { m() { 1 = 2 } }",
    "x = () => { ({a = 1}) }", "function f() { `\\01` }", "function* g() { var yield; }",
    "(function () { 'use strict'; 010 })", "a\n=> 1", "function f() { async (a)\n=> 1 }",
    "({ get a(b) {} })", "function f() { for (let of x); }", "if (a) function* g() {}",
    "function f() { x = {a, b = 1}; }", "function f() { [a, ...b, c] = d; }",
  };
  for (const char * text : programs) {
    auto tree = Parse(text);
    auto checking = Parse(text, false, GLECMA_PARSE_INTERNAL_CHECKING);
    EXPECT_EQ(tree.result, checking.result) << text;
    if (tree.result != GLECMA_OK) {
      EXPECT_EQ(tree.failure.rule, checking.failure.rule) << text;
      EXPECT_EQ(tree.failure.location.offset, checking.failure.location.offset) << text;
      EXPECT_STREQ(tree.failure.message, checking.failure.message) << text;
    } else {
      // And they report the same events, bodies included.
      EXPECT_EQ(Events(tree), Events(checking)) << text;
    }
  }
}

TEST(ParseBuilders, DeclarationAndReferenceEventsCarryWhatAScopeSummaryNeeds) {
  struct Case {
    const char * source;
    const char * events;
  };
  const Case cases[] = {
    {"var a = b;", "+script var a ref b:r -script"},
    {"let a; const b = a;", "+script let a const b ref a:r -script"},
    {"a = 1;", "+script ref a:w -script"},
    {"a += 1; a++; --a;", "+script ref a:rw ref a:rw ref a:rw -script"},
    {"typeof a; delete b; delete b.c;", "+script ref a:rt ref b:rd ref b:r -script"},
    {"function f(a, [b], ...c) { d }",
        "+script function f +function param a param b param c ref d:r -function -script"},
    {"x = function g(a) { g; };", "+script ref x:w +function fname g param a ref g:r -function -script"},
    {"(a, b) => a;", "+script +arrow param a param b ref a:r -arrow -script"},
    {"a => a;", "+script +arrow param a ref a:r -arrow -script"},
    {"async (a) => b;", "+script +arrow param a ref b:r -arrow -script"},
    {"async(a);", "+script ref async:r ref a:r -script"},
    {"[a, b.c] = d;", "+script ref a:w ref b:r ref d:r -script"},
    {"({a, b: c = e} = f);", "+script ref a:w ref c:w ref e:r ref f:r -script"},
    {"for (var i = 0; i < n; i++) { j }",
        "+script +for var i ref i:r ref n:r ref i:rw +block ref j:r -block -for -script"},
    {"for (const a of b) c;", "+script +for const a ref b:r ref c:r -for -script"},
    {"for (a in b);", "+script +for ref a:w ref b:r -for -script"},
    {"try { a } catch (e) { b }",
        "+script +block ref a:r -block +catch catch e +block ref b:r -block -catch -script"},
    {"switch (a) { case b: let c; }", "+script ref a:r +block ref b:r let c -block -script"},
    {"with (a) b;", "+script ref a:r +with ref b:r -with -script"},
    {"class A extends B { m() { return this; } static x = 1; static { y } }",
        "+script class A +class cname A ref B:r +function use this -function +field -field +static ref y:r -static -class -script"},
    {"x = class C {};", "+script ref x:w +class cname C -class -script"},
    {"this; new.target; super.x; super(); eval(y); a.eval(y); (0, eval)(z);",
        "+script use this use new.target use super. use super() ref eval:r ref y:r use eval ref a:r ref y:r "
        "ref eval:r ref z:r -script"},
    {"if (a) function f() {}", "+script ref a:r +block function f +function -function -block -script"},
    {"a: b;", "+script ref b:r -script"},
  };
  for (const Case & c : cases) {
    auto parsed = Parse(c.source);
    ASSERT_EQ(parsed.result, GLECMA_OK) << c.source << ": " << parsed.failure.message;
    EXPECT_EQ(Events(parsed), c.events) << c.source;
  }
}

TEST(ParseBuilders, ARewoundCoverGrammarLeavesNoEventBehind) {
  // The parenthesized group is read first as an expression and then as
  // parameters; the references of the first reading are gone.
  auto arrow = Parse("(a, b = c, {d}) => 1;");
  ASSERT_EQ(arrow.result, GLECMA_OK);
  EXPECT_EQ(Events(arrow), "+script +arrow param a param b ref c:r param d -arrow -script");
  auto pattern = Parse("[a.b, c = d] = e;");
  ASSERT_EQ(pattern.result, GLECMA_OK);
  EXPECT_EQ(Events(pattern), "+script ref a:r ref c:w ref d:r ref e:r -script");
  auto nested = Parse("(a = (b = c) => d) => e;");
  ASSERT_EQ(nested.result, GLECMA_OK);
  EXPECT_EQ(Events(nested),
      "+script +arrow param a +arrow param b ref c:r ref d:r -arrow ref e:r -arrow -script");
}

TEST(ParseBuilders, EveryScopeEnteredIsLeftInOrderAndEveryEventHasARange) {
  auto parsed = Parse(
      "function f(a) { for (let i of a) { switch (i) { case 1: try { } catch ({e}) { } } } }\n"
      "class C extends D { static { x } y = () => z; m() { return () => 1 } }\n");
  ASSERT_EQ(parsed.result, GLECMA_OK);
  std::vector<uint32_t> open;
  for (size_t i = 0; i < glecma_tree_event_count(parsed.tree); i++) {
    const GLECMA_BEvent * e = glecma_tree_event(parsed.tree, i);
    EXPECT_LE(e->start, e->end);
    EXPECT_LE(e->end, glecma_source_units(glecma_tree_source(parsed.tree)));
    if (e->type == GLECMA_BEV_SCOPE_ENTER) {
      open.push_back(e->what);
    } else if (e->type == GLECMA_BEV_SCOPE_LEAVE) {
      ASSERT_FALSE(open.empty());
      EXPECT_EQ(open.back(), e->what);
      open.pop_back();
    }
  }
  EXPECT_TRUE(open.empty());
}

TEST(ParseBuilders, TheTreeOutlivesItsSourceAndTheSourceOutlivesTheTree) {
  const std::string text =
      "function a(x) { return x; }\nvar b = function () { return 1; };\nvar c = (y) => y;\n"
      "class D { m() {} static n() {} get o() { return 1; } }\n";
  GLECMA_Source * source = nullptr;
  ASSERT_EQ(glecma_source_create_utf8(text.data(), text.size(), nullptr, nullptr,
                nullptr, &source),
      GLECMA_OK);
  GLECMA_Tree * tree = nullptr;
  ASSERT_EQ(glecma_parse(source, nullptr, nullptr, nullptr, &tree), GLECMA_OK);
  std::vector<std::pair<size_t, size_t>> functions;
  std::vector<const GLECMA_Node *> stack = {glecma_tree_root(tree)};
  while (!stack.empty()) {
    const GLECMA_Node * node = stack.back();
    stack.pop_back();
    if (node == nullptr) {
      continue;
    }
    uint32_t kind = glecma_node_kind(node);
    if (kind == GLECMA_NODE_FUNCTION_DECLARATION || kind == GLECMA_NODE_FUNCTION_EXPRESSION ||
        kind == GLECMA_NODE_ARROW_FUNCTION) {
      functions.push_back({glecma_node_start(node), glecma_node_end(node)});
    }
    for (size_t i = 0; i < glecma_node_child_count(node); i++) {
      stack.push_back(glecma_node_child(node, i));
    }
  }
  ASSERT_EQ(functions.size(), 6u);
  // The caller lets go of the source first: the tree keeps it.
  glecma_source_release(source);
  EXPECT_EQ(glecma_source_units(glecma_tree_source(tree)), text.size());
  glecma_tree_destroy(tree);
  // Each function is parsed again from nothing but its text, as a function
  // that is compiled after its tree is gone is, and gives the same function:
  // a node of the same kind with exactly that range.
  const std::pair<const char *, const char *> wrappers[] = {{"", ""}, {"(", ")"}, {"({", "})"}};
  for (const auto & range : functions) {
    std::string piece = text.substr(range.first, range.second - range.first);
    bool found = false;
    for (const auto & wrap : wrappers) {
      auto again = Parse(wrap.first + piece + wrap.second);
      if (again.result != GLECMA_OK) {
        continue;
      }
      std::vector<const GLECMA_Node *> walk = {glecma_tree_root(again.tree)};
      while (!walk.empty() && !found) {
        const GLECMA_Node * node = walk.back();
        walk.pop_back();
        if (node == nullptr) {
          continue;
        }
        uint32_t kind = glecma_node_kind(node);
        if ((kind == GLECMA_NODE_FUNCTION_DECLARATION || kind == GLECMA_NODE_FUNCTION_EXPRESSION ||
                kind == GLECMA_NODE_ARROW_FUNCTION) &&
            glecma_node_start(node) == std::strlen(wrap.first) &&
            glecma_node_end(node) - glecma_node_start(node) == piece.size()) {
          found = true;
        }
        for (size_t i = 0; i < glecma_node_child_count(node); i++) {
          walk.push_back(glecma_node_child(node, i));
        }
      }
      if (found) {
        break;
      }
    }
    EXPECT_TRUE(found) << piece;
  }
}

TEST(ParseBuilders, TheSameInputGivesTheSameTreeAndTheSameFailureEveryTime) {
  const char * programs[] = {
    "function f(a, b) { return a ? [b, ...a] : { b, a }; } class C extends D { #x = 1; get y() { return this.#x } }",
    "a +;", "x = `a${b}c`; y = /z/g;",
  };
  for (const char * text : programs) {
    auto first = Parse(text);
    auto second = Parse(text);
    ASSERT_EQ(first.result, second.result) << text;
    EXPECT_EQ(first.Whole(true), second.Whole(true));
    EXPECT_EQ(first.failure.rule, second.failure.rule);
    EXPECT_EQ(first.failure.location.offset, second.failure.location.offset);
    if (first.result == GLECMA_OK) {
      EXPECT_EQ(Events(first), Events(second));
      EXPECT_EQ(Dump(first), Dump(second));
    }
  }
}

TEST(ParseBuilders, ASourceMadeFromAStreamParsesToTheSameTree) {
  const std::string text = "class A { m(a = 1) { return `t${a}`; } }\nvar \xC3\xA9 = '\xF0\x9F\x98\x80';\n";
  GLECMA_Stream * stream = nullptr;
  ASSERT_EQ(glecma_stream_create_memory(text.data(), text.size(), nullptr, &stream), GLECMA_OK);
  Parsed from_stream;
  ASSERT_EQ(glecma_source_create_stream(stream, GLECMA_ENCODING_UTF8, nullptr, nullptr,
                nullptr, &from_stream.source),
      GLECMA_OK);
  ASSERT_EQ(glecma_parse(from_stream.source, nullptr, nullptr, nullptr, &from_stream.tree), GLECMA_OK);
  auto from_buffer = Parse(text);
  EXPECT_EQ(from_stream.Whole(true), from_buffer.Whole(true));
  glecma_stream_destroy(stream);
}

TEST(ParseBuilders, ParsesOnSeparateThreadsAreIndependent) {
  const std::string text =
      "function f(a, b) { return a ? [b, ...a] : { b, a }; }\n"
      "class C extends D { #x = 1; get y() { return this.#x } static { z } }\n"
      "x = (a, b) => ({ a, b }); [p, q] = r;";
  auto expected = Parse(text).Whole(true);
  GLECMA_Source * shared = nullptr;
  ASSERT_EQ(glecma_source_create_utf8(text.data(), text.size(), nullptr, nullptr,
                nullptr, &shared),
      GLECMA_OK);
  std::atomic<int> bad{0};
  std::vector<std::thread> threads;
  for (int t = 0; t < 8; t++) {
    GLECMA_Source * mine = glecma_source_retain(shared);
    threads.emplace_back([&, mine] {
      for (int i = 0; i < 30; i++) {
        Parsed p;
        p.source = glecma_source_retain(mine);
        p.result = glecma_parse(p.source, nullptr, nullptr, nullptr, &p.tree);
        if (p.result != GLECMA_OK || p.Whole(true) != expected) {
          bad++;
        }
      }
      glecma_source_release(mine);
    });
  }
  for (auto & thread : threads) {
    thread.join();
  }
  glecma_source_release(shared);
  EXPECT_EQ(bad.load(), 0);
}

int main(int argc, char * * argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

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

// The tree object: nodes built through the builder interface by hand (the
// parser is tested in the parser's own files), read through the public
// walker; the two builders; events, marks and rewinds; the dump; lifetime.

#include <ghoti.io/lang-ecmascript/lang-ecmascript.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../parse_test_support.h"
#include "../test_helpers.h"
#include "parse/builder.h"

namespace {

GLECMA_Source * MakeSource(const std::string & text) {
  GLECMA_Source * source = nullptr;
  EXPECT_EQ(glecma_source_create_utf8(text.data(), text.size(), nullptr, nullptr,
                nullptr, &source),
      GLECMA_OK);
  return source;
}

// A builder with a few helpers, over a tree it owns until `Finish`.
struct Built {
  GLECMA_Source * source;
  GLECMA_Tree * tree = nullptr;
  GLECMA_TreeBuilder * builder = nullptr;

  explicit Built(uint32_t mode, const std::string & text = "a + b; function f() { x }",
      const GLECMA_Allocator * allocator = nullptr)
      : source(MakeSource(text)) {
    EXPECT_EQ(glecma_tree_open(mode, source, allocator, &tree), GLECMA_OK);
    builder = glecma_tree_builder(tree);
  }
  ~Built() {
    glecma_tree_destroy(tree);
    glecma_source_release(source);
  }

  GLECMA_BText Name(const char * ascii, bool intern = true) {
    std::vector<uint16_t> units;
    for (const char * c = ascii; *c != '\0'; c++) {
      units.push_back(static_cast<uint16_t>(*c));
    }
    GLECMA_BText text;
    EXPECT_EQ(builder->ops->text(builder, units.data(), units.size(), intern,
                  &text),
        GLECMA_OK);
    return text;
  }

  GLECMA_BNode Node(uint32_t kind, size_t start, size_t end,
      std::vector<GLECMA_BNode> children = {}, uint32_t flags = 0,
      uint32_t op = 0, std::vector<GLECMA_BText> texts = {}) {
    GLECMA_BSpec spec;
    std::memset(&spec, 0, sizeof spec);
    spec.kind = kind;
    spec.flags = flags;
    spec.op = op;
    spec.start = start;
    spec.end = end;
    spec.children = children.data();
    spec.child_count = children.size();
    for (size_t i = 0; i < texts.size(); i++) {
      spec.text[i] = texts[i];
    }
    spec.text_count = static_cast<uint32_t>(texts.size());
    GLECMA_BNode node = nullptr;
    EXPECT_EQ(builder->ops->node(builder, &spec, &node), GLECMA_OK);
    return node;
  }
};

}  // namespace

TEST(Tree, NodesAreReadThroughTheWalker) {
  Built b(GLECMA_BUILDER_TREE);
  GLECMA_BNode a = b.Node(GLECMA_NODE_IDENTIFIER, 0, 1, {}, 0, 0, {b.Name("a")});
  GLECMA_BNode bb = b.Node(GLECMA_NODE_IDENTIFIER, 4, 5, {}, 0, 0, {b.Name("b")});
  GLECMA_BNode sum = b.Node(GLECMA_NODE_BINARY_EXPRESSION, 0, 5, {a, bb}, 0,
      GLECMA_OP_ADD);
  GLECMA_BNode statement = b.Node(GLECMA_NODE_EXPRESSION_STATEMENT, 0, 6, {sum});
  GLECMA_BNode root = b.Node(GLECMA_NODE_SCRIPT, 0, 25, {statement, nullptr},
      GLECMA_NODEF_STRICT);
  glecma_tree_finish(b.tree, root, GLECMA_PARSE_SCRIPT, true);

  const GLECMA_Node * script = glecma_tree_root(b.tree);
  ASSERT_NE(script, nullptr);
  EXPECT_EQ(glecma_node_kind(script), GLECMA_NODE_SCRIPT);
  EXPECT_EQ(glecma_node_flags(script), GLECMA_NODEF_STRICT);
  EXPECT_EQ(glecma_node_child_count(script), 2u);
  EXPECT_EQ(glecma_node_child(script, 1), nullptr);  // an empty slot
  EXPECT_EQ(glecma_node_child(script, 2), nullptr);  // past the end
  EXPECT_TRUE(glecma_tree_strict(b.tree));
  EXPECT_EQ(glecma_tree_goal(b.tree), GLECMA_PARSE_SCRIPT);
  const GLECMA_Node * add = glecma_node_child(glecma_node_child(script, 0), 0);
  EXPECT_EQ(glecma_node_kind(add), GLECMA_NODE_BINARY_EXPRESSION);
  EXPECT_EQ(glecma_node_operator(add), GLECMA_OP_ADD);
  EXPECT_STREQ(glecma_operator_text(GLECMA_OP_ADD), "+");
  const GLECMA_Node * right = glecma_node_child(add, 1);
  const uint16_t * units = nullptr;
  size_t length = 0;
  ASSERT_TRUE(glecma_node_text(right, 0, &units, &length));
  EXPECT_EQ(glecma_test::Narrow(units, length), "b");
  EXPECT_FALSE(glecma_node_text(right, 1, &units, &length));
  EXPECT_EQ(glecma_node_start(right), 4u);
  EXPECT_EQ(glecma_node_end(right), 5u);
  GLECMA_Location start, end;
  ASSERT_EQ(glecma_node_location(b.tree, right, &start, &end), GLECMA_OK);
  EXPECT_EQ(start.offset, 4u);
  EXPECT_EQ(start.line, 1u);
  EXPECT_EQ(start.column, 4u);
  EXPECT_EQ(end.offset, 5u);
  EXPECT_STREQ(glecma_node_kind_name(GLECMA_NODE_BINARY_EXPRESSION), "BinaryExpression");
}

TEST(Tree, TheWalkerAnswersForNullAndForAKindItDoesNotKnow) {
  EXPECT_EQ(glecma_node_kind(nullptr), GLECMA_NODE_NONE);
  EXPECT_EQ(glecma_node_child_count(nullptr), 0u);
  EXPECT_EQ(glecma_node_child(nullptr, 0), nullptr);
  EXPECT_FALSE(glecma_node_text(nullptr, 0, nullptr, nullptr));
  EXPECT_EQ(glecma_tree_root(nullptr), nullptr);
  EXPECT_EQ(glecma_node_location(nullptr, nullptr, nullptr, nullptr), GLECMA_ERR_INVALID);
  EXPECT_STREQ(glecma_node_kind_name(GLECMA_NODE_KIND_COUNT), "Unknown");
  EXPECT_STREQ(glecma_node_kind_name(0xFFFFFFFFu), "Unknown");
  EXPECT_STREQ(glecma_operator_text(GLECMA_OP_COUNT), "");
  // Every kind below the count has a name of its own.
  std::vector<std::string> names;
  for (uint32_t kind = 0; kind < GLECMA_NODE_KIND_COUNT; kind++) {
    std::string name = glecma_node_kind_name(kind);
    EXPECT_NE(name, "Unknown") << kind;
    names.push_back(name);
  }
  std::sort(names.begin(), names.end());
  EXPECT_EQ(std::adjacent_find(names.begin(), names.end()), names.end());

  // A kind from a newer library is walked and dumped by its number.
  Built b(GLECMA_BUILDER_TREE, "x");
  GLECMA_BNode inner = b.Node(GLECMA_NODE_IDENTIFIER, 0, 1, {}, 0, 0, {b.Name("x")});
  GLECMA_BNode future = b.Node(9999, 0, 1, {inner});
  GLECMA_BNode root = b.Node(GLECMA_NODE_SCRIPT, 0, 1, {future});
  glecma_tree_finish(b.tree, root, GLECMA_PARSE_SCRIPT, false);
  const GLECMA_Node * unknown = glecma_node_child(glecma_tree_root(b.tree), 0);
  EXPECT_EQ(glecma_node_kind(unknown), 9999u);
  EXPECT_EQ(glecma_node_child_count(unknown), 1u);
  std::string dump;
  char * buffer = nullptr;
  size_t size = 0;
  FILE * out = open_memstream(&buffer, &size);
  ASSERT_NE(out, nullptr);
  ASSERT_EQ(glecma_tree_dump(b.tree, out), GLECMA_OK);
  std::fclose(out);
  dump.assign(buffer, size);
  std::free(buffer);
  EXPECT_EQ(dump, "Script [0,1)\n  Unknown(9999) [0,1)\n    Identifier \"x\" [0,1)\n");
}

TEST(Tree, NamesAreInternedAndStringsAreNotAndBothSurviveARewind) {
  Built b(GLECMA_BUILDER_TREE);
  GLECMA_BText first = b.Name("name");
  GLECMA_BMark mark;
  b.builder->ops->mark(b.builder, &mark);
  GLECMA_BText second = b.Name("name");
  GLECMA_BText third = b.Name("other");
  GLECMA_BText plain_one = b.Name("name", false);
  GLECMA_BText plain_two = b.Name("name", false);
  EXPECT_EQ(first.units, second.units);   // the same name is one copy
  EXPECT_NE(first.units, third.units);
  EXPECT_NE(plain_one.units, first.units);  // a string is its own copy
  EXPECT_NE(plain_one.units, plain_two.units);
  b.builder->ops->rewind(b.builder, &mark);
  // A name made before the mark, and one made after it, are both still there:
  // an event recorded earlier keeps a valid name.
  EXPECT_EQ(glecma_test::Narrow(first.units, first.length), "name");
  EXPECT_EQ(glecma_test::Narrow(third.units, third.length), "other");
  EXPECT_EQ(b.Name("other").units, third.units);
  GLECMA_BText empty = b.Name("");
  EXPECT_EQ(empty.length, 0u);
  EXPECT_NE(empty.units, nullptr);
}

TEST(Tree, ARewindDiscardsTheNodesAndEventsSinceTheMarkAndNothingElse) {
  Built b(GLECMA_BUILDER_TREE);
  GLECMA_BNode kept = b.Node(GLECMA_NODE_THIS_EXPRESSION, 0, 1);
  GLECMA_BEvent event;
  std::memset(&event, 0, sizeof event);
  event.type = GLECMA_BEV_USE;
  event.what = GLECMA_BUSE_THIS;
  ASSERT_EQ(b.builder->ops->event(b.builder, &event), GLECMA_OK);
  GLECMA_BMark mark;
  b.builder->ops->mark(b.builder, &mark);
  for (int i = 0; i < 5000; i++) {  // enough to need more than one chunk
    b.Node(GLECMA_NODE_NULL_LITERAL, 0, 1);
    event.what = GLECMA_BUSE_SUPER_CALL;
    ASSERT_EQ(b.builder->ops->event(b.builder, &event), GLECMA_OK);
  }
  EXPECT_EQ(glecma_tree_event_count(b.tree), 5001u);
  b.builder->ops->rewind(b.builder, &mark);
  EXPECT_EQ(glecma_tree_event_count(b.tree), 1u);
  EXPECT_EQ(glecma_tree_event(b.tree, 0)->what, GLECMA_BUSE_THIS);
  EXPECT_EQ(glecma_tree_event(b.tree, 1), nullptr);
  // The node made before the mark is intact, and building goes on.
  GLECMA_BNode after = b.Node(GLECMA_NODE_NULL_LITERAL, 2, 3);
  GLECMA_BNode root = b.Node(GLECMA_NODE_SCRIPT, 0, 3, {kept, after});
  glecma_tree_finish(b.tree, root, GLECMA_PARSE_SCRIPT, false);
  const GLECMA_Node * script = glecma_tree_root(b.tree);
  EXPECT_EQ(glecma_node_kind(glecma_node_child(script, 0)), GLECMA_NODE_THIS_EXPRESSION);
  EXPECT_EQ(glecma_node_start(glecma_node_child(script, 1)), 2u);
}

TEST(Tree, TheCheckingBuilderBuildsNoBodyAndMakesTheReservedKindInPlaceOfOne) {
  Built b(GLECMA_BUILDER_CHECKING);
  EXPECT_TRUE(glecma_tree_is_checking(b.tree));
  EXPECT_TRUE(b.builder->ops->materializing(b.builder));
  GLECMA_BNode id = b.Node(GLECMA_NODE_IDENTIFIER, 9, 10, {}, 0, 0, {b.Name("f")});
  ASSERT_NE(id, GLECMA_BNODE_ELIDED);
  ASSERT_EQ(b.builder->ops->body_begin(b.builder), GLECMA_OK);
  EXPECT_FALSE(b.builder->ops->materializing(b.builder));
  GLECMA_BNode skipped = b.Node(GLECMA_NODE_IDENTIFIER, 20, 21, {}, 0, 0, {b.Name("x")});
  EXPECT_EQ(skipped, GLECMA_BNODE_ELIDED);
  // A body inside a body is skipped too, and its end makes nothing.
  ASSERT_EQ(b.builder->ops->body_begin(b.builder), GLECMA_OK);
  GLECMA_BSpec spec;
  std::memset(&spec, 0, sizeof spec);
  spec.kind = GLECMA_NODE_FUNCTION_BODY;
  spec.start = 15;
  spec.end = 25;
  GLECMA_BNode inner = nullptr;
  ASSERT_EQ(b.builder->ops->body_end(b.builder, &spec, &inner), GLECMA_OK);
  EXPECT_EQ(inner, GLECMA_BNODE_ELIDED);
  GLECMA_BNode body = nullptr;
  ASSERT_EQ(b.builder->ops->body_end(b.builder, &spec, &body), GLECMA_OK);
  ASSERT_NE(body, GLECMA_BNODE_ELIDED);
  ASSERT_NE(body, nullptr);
  EXPECT_TRUE(b.builder->ops->materializing(b.builder));
  GLECMA_BNode params = b.Node(GLECMA_NODE_PARAMETER_LIST, 10, 12);
  GLECMA_BNode function = b.Node(GLECMA_NODE_FUNCTION_DECLARATION, 0, 25,
      {id, params, body});
  GLECMA_BNode root = b.Node(GLECMA_NODE_SCRIPT, 0, 25, {function});
  glecma_tree_finish(b.tree, root, GLECMA_PARSE_SCRIPT, false);

  // The walker and the dump handle the reserved kind as they handle any other.
  const GLECMA_Node * f = glecma_node_child(glecma_tree_root(b.tree), 0);
  const GLECMA_Node * walked = glecma_node_child(f, 2);
  EXPECT_EQ(glecma_node_kind(walked), GLECMA_NODE_UNMATERIALIZED_BODY);
  EXPECT_EQ(glecma_node_child_count(walked), 0u);
  EXPECT_EQ(glecma_node_start(walked), 15u);
  EXPECT_EQ(glecma_node_end(walked), 25u);
  EXPECT_STREQ(glecma_node_kind_name(GLECMA_NODE_UNMATERIALIZED_BODY), "UnmaterializedBody");
  char * buffer = nullptr;
  size_t size = 0;
  FILE * out = open_memstream(&buffer, &size);
  ASSERT_EQ(glecma_tree_dump(b.tree, out), GLECMA_OK);
  std::fclose(out);
  std::string dump(buffer, size);
  std::free(buffer);
  EXPECT_NE(dump.find("UnmaterializedBody [15,25)"), std::string::npos) << dump;
  EXPECT_EQ(b.builder->ops->body_end(b.builder, &spec, &body), GLECMA_ERR_INTERNAL);
}

TEST(Tree, TheTreeBuilderMakesTheBodyItselfAndAnExpressionBodyIsTheExpression) {
  Built b(GLECMA_BUILDER_TREE);
  ASSERT_EQ(b.builder->ops->body_begin(b.builder), GLECMA_OK);
  EXPECT_TRUE(b.builder->ops->materializing(b.builder));
  GLECMA_BNode x = b.Node(GLECMA_NODE_IDENTIFIER, 20, 21, {}, 0, 0, {b.Name("x")});
  EXPECT_NE(x, GLECMA_BNODE_ELIDED);
  GLECMA_BSpec spec;
  std::memset(&spec, 0, sizeof spec);
  spec.kind = GLECMA_NODE_FUNCTION_BODY;
  spec.children = &x;
  spec.child_count = 1;
  GLECMA_BNode body = nullptr;
  ASSERT_EQ(b.builder->ops->body_end(b.builder, &spec, &body), GLECMA_OK);
  EXPECT_NE(body, x);
  spec.kind = 0;  // an expression body
  GLECMA_BNode expression = nullptr;
  ASSERT_EQ(b.builder->ops->body_end(b.builder, &spec, &expression), GLECMA_OK);
  EXPECT_EQ(expression, x);
}

TEST(Tree, AnElidedChildOfABuiltNodeIsTheBuildersOwnError) {
  Built b(GLECMA_BUILDER_TREE);
  GLECMA_BNode children[1] = {GLECMA_BNODE_ELIDED};
  GLECMA_BSpec spec;
  std::memset(&spec, 0, sizeof spec);
  spec.kind = GLECMA_NODE_BLOCK;
  spec.children = children;
  spec.child_count = 1;
  GLECMA_BNode node = nullptr;
  EXPECT_EQ(b.builder->ops->node(b.builder, &spec, &node), GLECMA_ERR_INTERNAL);
}

TEST(Tree, TheTreeHoldsOneReferenceToItsSourceAndReleasesItWithTheTree) {
  glecma_test::CountingAllocator source_counting;
  std::string text = "a; b;";
  GLECMA_Source * source = nullptr;
  ASSERT_EQ(glecma_source_create_utf8(text.data(), text.size(), nullptr,
                source_counting.get(), nullptr, &source),
      GLECMA_OK);
  GLECMA_Tree * tree = nullptr;
  ASSERT_EQ(glecma_tree_open(GLECMA_BUILDER_TREE, source, nullptr, &tree), GLECMA_OK);
  glecma_source_release(source);  // the caller's reference
  EXPECT_GT(source_counting.live(), 0u);
  ASSERT_NE(glecma_tree_source(tree), nullptr);
  EXPECT_EQ(glecma_source_units(glecma_tree_source(tree)), 5u);
  glecma_tree_destroy(tree);
  EXPECT_EQ(source_counting.live(), 0u);  // the last reference was the tree's
  glecma_tree_destroy(nullptr);           // does nothing
}

TEST(Tree, EveryAllocationOfABuildMayFailWithNothingLeaked) {
  size_t total = 0;
  auto build = [](const GLECMA_Allocator * allocator, GLECMA_Result * result) {
    struct Owner {
      GLECMA_Source * source = MakeSource("a + b");
      GLECMA_Tree * tree = nullptr;
      GLECMA_TreeBuilder * builder = nullptr;
      ~Owner() {
        glecma_tree_destroy(tree);
        glecma_source_release(source);
      }
    } b;
    *result = glecma_tree_open(GLECMA_BUILDER_TREE, b.source, allocator, &b.tree);
    if (*result != GLECMA_OK) {
      return;
    }
    b.builder = glecma_tree_builder(b.tree);
    for (int i = 0; i < 3000 && *result == GLECMA_OK; i++) {
      std::vector<uint16_t> units = {'a', static_cast<uint16_t>('0' + i % 7),
          static_cast<uint16_t>('a' + i % 26)};
      GLECMA_BText text;
      *result = b.builder->ops->text(b.builder, units.data(), units.size(),
          i % 2 == 0, &text);
      if (*result != GLECMA_OK) {
        return;
      }
      GLECMA_BSpec spec;
      std::memset(&spec, 0, sizeof spec);
      spec.kind = GLECMA_NODE_IDENTIFIER;
      spec.text[0] = text;
      spec.text_count = 1;
      GLECMA_BNode node;
      *result = b.builder->ops->node(b.builder, &spec, &node);
      if (*result != GLECMA_OK) {
        return;
      }
      GLECMA_BEvent event;
      std::memset(&event, 0, sizeof event);
      event.type = GLECMA_BEV_REFERENCE;
      event.name = text;
      *result = b.builder->ops->event(b.builder, &event);
    }
  };
  {
    glecma_test::CountingAllocator counting;
    GLECMA_Result result = GLECMA_ERR_INTERNAL;
    build(counting.get(), &result);
    ASSERT_EQ(result, GLECMA_OK);
    total = counting.calls();
  }
  ASSERT_GT(total, 5u);
  for (size_t i = 0; i < total; i++) {
    glecma_test::CountingAllocator counting;
    counting.fail_at(i);
    GLECMA_Result result = GLECMA_OK;
    build(counting.get(), &result);
    EXPECT_EQ(result, GLECMA_ERR_OOM) << i;
    EXPECT_EQ(counting.live(), 0u) << i;
  }
}

TEST(Tree, AnEntryRefusesAnArgumentThatIsWrong) {
  GLECMA_Source * source = MakeSource("a");
  GLECMA_Tree * tree = nullptr;
  EXPECT_EQ(glecma_tree_open(GLECMA_BUILDER_TREE, nullptr, nullptr, &tree), GLECMA_ERR_INVALID);
  EXPECT_EQ(glecma_tree_open(GLECMA_BUILDER_TREE, source, nullptr, nullptr), GLECMA_ERR_INVALID);
  EXPECT_EQ(glecma_tree_open(7, source, nullptr, &tree), GLECMA_ERR_INVALID);
  GLECMA_Allocator broken = *glecma_allocator_default();
  broken.free_fn = nullptr;
  EXPECT_EQ(glecma_tree_open(GLECMA_BUILDER_TREE, source, &broken, &tree), GLECMA_ERR_INVALID);
  EXPECT_EQ(tree, nullptr);
  glecma_source_release(source);
  // A tree that was never finished has no root, and is not dumped.
  Built b(GLECMA_BUILDER_TREE);
  EXPECT_EQ(glecma_tree_root(b.tree), nullptr);
  EXPECT_EQ(glecma_tree_dump(b.tree, stdout), GLECMA_ERR_INVALID);
  EXPECT_EQ(glecma_tree_dump(nullptr, stdout), GLECMA_ERR_INVALID);
  EXPECT_EQ(glecma_tree_dump(b.tree, nullptr), GLECMA_ERR_INVALID);
}

int main(int argc, char * * argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

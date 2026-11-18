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

/**
 * @file parse_tree.c
 *
 * Parsing a program into a tree and reading it.
 *
 * `glecma_parse` takes a source and returns a read-only tree that the caller
 * owns. The tree is read through the walker: a node has a kind, flags, an
 * operator, a source range, children (positional: an empty slot is NULL) and
 * up to two texts. This example prints the tree of a small program, finds each
 * function's exact source text from its range (what
 * `Function.prototype.toString` would return), and shows what a syntax error
 * carries: the rule that was broken and where. A caller that parses on a
 * thread with little stack lowers `native_stack_bytes`, and a deep input is
 * then refused with `GLECMA_ERR_LIMIT` naming the field, never a fault.
 */

#include <ghoti.io/lang-ecmascript/lang-ecmascript.h>

#include <stdio.h>
#include <string.h>

// Prints the source text of every function and class in the tree.
static void print_functions(const GLECMA_Node * node, const char * text) {
  if (node == NULL) {
    return;
  }
  switch (glecma_node_kind(node)) {
    case GLECMA_NODE_FUNCTION_DECLARATION:
    case GLECMA_NODE_FUNCTION_EXPRESSION:
    case GLECMA_NODE_ARROW_FUNCTION:
    case GLECMA_NODE_CLASS_DECLARATION:
    case GLECMA_NODE_CLASS_EXPRESSION: {
      size_t start = glecma_node_start(node);
      size_t end = glecma_node_end(node);
      printf("  %-20s [%2zu,%2zu)  %.*s\n",
          glecma_node_kind_name(glecma_node_kind(node)), start, end,
          (int)(end - start), text + start);
      break;
    }
    default:
      break;
  }
  for (size_t i = 0; i < glecma_node_child_count(node); i++) {
    print_functions(glecma_node_child(node, i), text);
  }
}

// Parses `text`; prints the tree, or the reason it was refused.
static int parse(const char * text) {
  GLECMA_Source * source = NULL;
  GLECMA_Failure failure = GLECMA_FAILURE_INIT;
  GLECMA_Result result = glecma_source_create_utf8(text, strlen(text), NULL,
      NULL, &failure, &source);
  if (result != GLECMA_OK) {
    fprintf(stderr, "%s\n", glecma_result_string(result));
    return 1;
  }
  GLECMA_Tree * tree = NULL;
  result = glecma_parse(source, NULL, NULL, &failure, &tree);
  int status = 0;
  if (result == GLECMA_OK) {
    printf("%s\n", text);
    glecma_tree_dump(tree, stdout);
    printf("functions and classes:\n");
    print_functions(glecma_tree_root(tree), text);
    glecma_tree_destroy(tree);
  } else if (result == GLECMA_ERR_SYNTAX) {
    printf("%s\n  refused: %s (rule %s, clause %s) at line %zu, column %zu\n",
        text, failure.message, glecma_rule_name(failure.rule),
        glecma_rule_clause(failure.rule), failure.location.line,
        failure.location.column);
  } else {
    fprintf(stderr, "%s\n", glecma_result_string(result));
    status = 1;
  }
  glecma_source_release(source);
  return status;
}

int main(void) {
  int status = parse("const add = (a, b = 1) => a + b;\n"
                     "class Counter { #n = 0; inc() { return ++this.#n; } }");
  // A program the grammar refuses: `**` cannot take a unary operand.
  status |= parse("x = -a ** 2;");
  return status;
}

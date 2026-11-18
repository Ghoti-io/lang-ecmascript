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

/*
 * The function re-parse equality check, shared by the test262 gate's driver, the
 * fuzz harness and the unit tests (C, and C++ where it is included).
 *
 * Given the tree a parse returned, it finds every function in it (a function
 * declaration or expression, an arrow function, a method with its accessors and
 * constructor, a class field's initializer, a class static block), derives the
 * context each one had from the tree alone, reads the function again with
 * glecma_parse_function from its range of the source, and requires the tree
 * that gives to equal the first parse's subtree: the same kinds, flags,
 * operators, ranges (in the whole source), texts and children.
 *
 * The context is derived by walking the tree with the rules of the grammar
 * and nothing of the parser: what a function is given (a function's own `new.target`,
 * a generator's `yield`, an async function's `await`, a method's `super.x`, a derived
 * constructor's `super()`), what an arrow function inherits, that class code is
 * strict, which private names the classes around declare. So this is a second
 * statement of the context and not the parser's own flags read back, and a
 * wrong flag in either shows as a function that does not read again as it did.
 *
 * Every function is read twice: once with the strictness it inherited, and once
 * with the strictness its code has (the directive of its own body, if any,
 * included), which must give the same tree.
 */

#ifndef GHOTI_IO_GLECMA_TESTS_REPARSE_SUPPORT_H
#define GHOTI_IO_GLECMA_TESTS_REPARSE_SUPPORT_H

#include <ghoti.io/lang-ecmascript/lang-ecmascript.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RpStats {
  uint64_t functions;                     /* functions read again */
  uint64_t kinds[GLECMA_FNKIND_COUNT];    /* by kind */
} RpStats;

/* The context the code at a point of the tree is in: the flags and how many private names are visible. */
typedef struct RpCtx {
  uint32_t flags;
  size_t names;
} RpCtx;

/* What a child is for, as its parent says. */
enum {
  RP_NORMAL = 0,
  RP_METHOD,      /* the function of a method, accessor or constructor */
  RP_FIELD_INIT,  /* the initializer of a class field */
  RP_CLASS_BODY
};

/* A function to be read again, recorded instead of read (the benchmark times the reads alone). */
typedef struct RpJob {
  GLECMA_FunctionContext context;
  GLECMA_PrivateName * names; /* owned: a copy of the names in scope */
} RpJob;

typedef struct RpFrame {
  const GLECMA_Node * node;
  size_t next;
  RpCtx ctx;          /* what the children are read in */
  size_t names_base;  /* the names stack before this frame pushed its own */
  bool derived;       /* a class body of a class with `extends`, or a member of one */
} RpFrame;

typedef struct RpState {
  GLECMA_Source * source;
  const GLECMA_Tree * tree;
  const GLECMA_ParseOptions * options;
  GLECMA_PrivateName * names;
  size_t names_len, names_cap;
  RpFrame * frames;
  size_t frames_len, frames_cap;
  RpStats * stats;
  char * message;
  size_t message_size;
  bool failed;
  RpJob * jobs;        /* when recording: where the functions go */
  size_t jobs_len, jobs_cap;
  bool recording;
} RpState;

static inline void rp_fail(RpState * s, const char * what, const GLECMA_Node * node, uint32_t kind,
    const char * detail) {
  if (s->failed) {
    return;
  }
  s->failed = true;
  snprintf(s->message, s->message_size, "%s: kind %u at [%zu,%zu): %s", what,
      (unsigned)kind, glecma_node_start(node), glecma_node_end(node), detail);
}

/* ---- Equality of two trees (iterative: a tree can be as deep as its source is long) ---- */

typedef struct RpPair {
  const GLECMA_Node * a;
  const GLECMA_Node * b;
} RpPair;

static inline bool rp_same_texts(const GLECMA_Node * a, const GLECMA_Node * b) {
  for (size_t i = 0; i < 2; i++) {
    const uint16_t * ua = NULL;
    const uint16_t * ub = NULL;
    size_t la = 0, lb = 0;
    bool ha = glecma_node_text(a, i, &ua, &la);
    bool hb = glecma_node_text(b, i, &ub, &lb);
    if (ha != hb || la != lb || (la > 0 && memcmp(ua, ub, la * sizeof(uint16_t)) != 0)) {
      return false;
    }
  }
  return true;
}

/* Returns true when equal; otherwise describes the first difference. */
static inline bool rp_equal(const GLECMA_Node * a, const GLECMA_Node * b, char * why, size_t why_size) {
  size_t capacity = 64, length = 0;
  RpPair * stack = (RpPair *)malloc(capacity * sizeof(RpPair));
  if (stack == NULL) {
    snprintf(why, why_size, "out of memory");
    return false;
  }
  stack[length].a = a;
  stack[length].b = b;
  length++;
  bool equal = true;
  while (length > 0 && equal) {
    RpPair pair = stack[--length];
    if (pair.a == NULL || pair.b == NULL) {
      if (pair.a != pair.b) {
        snprintf(why, why_size, "an empty slot against a node");
        equal = false;
      }
      continue;
    }
    if (glecma_node_kind(pair.a) != glecma_node_kind(pair.b)) {
      snprintf(why, why_size, "kind %s against %s at [%zu,%zu)",
          glecma_node_kind_name(glecma_node_kind(pair.a)),
          glecma_node_kind_name(glecma_node_kind(pair.b)),
          glecma_node_start(pair.a), glecma_node_end(pair.a));
      equal = false;
    } else if (glecma_node_flags(pair.a) != glecma_node_flags(pair.b) ||
        glecma_node_operator(pair.a) != glecma_node_operator(pair.b)) {
      snprintf(why, why_size, "%s flags %x/%u against %x/%u at [%zu,%zu)",
          glecma_node_kind_name(glecma_node_kind(pair.a)),
          (unsigned)glecma_node_flags(pair.a), (unsigned)glecma_node_operator(pair.a),
          (unsigned)glecma_node_flags(pair.b), (unsigned)glecma_node_operator(pair.b),
          glecma_node_start(pair.a), glecma_node_end(pair.a));
      equal = false;
    } else if (glecma_node_start(pair.a) != glecma_node_start(pair.b) ||
        glecma_node_end(pair.a) != glecma_node_end(pair.b)) {
      snprintf(why, why_size, "%s range [%zu,%zu) against [%zu,%zu)",
          glecma_node_kind_name(glecma_node_kind(pair.a)), glecma_node_start(pair.a),
          glecma_node_end(pair.a), glecma_node_start(pair.b), glecma_node_end(pair.b));
      equal = false;
    } else if (!rp_same_texts(pair.a, pair.b)) {
      snprintf(why, why_size, "%s text differs at [%zu,%zu)",
          glecma_node_kind_name(glecma_node_kind(pair.a)), glecma_node_start(pair.a),
          glecma_node_end(pair.a));
      equal = false;
    } else if (glecma_node_child_count(pair.a) != glecma_node_child_count(pair.b)) {
      snprintf(why, why_size, "%s has %zu children against %zu at [%zu,%zu)",
          glecma_node_kind_name(glecma_node_kind(pair.a)), glecma_node_child_count(pair.a),
          glecma_node_child_count(pair.b), glecma_node_start(pair.a), glecma_node_end(pair.a));
      equal = false;
    } else {
      size_t count = glecma_node_child_count(pair.a);
      if (length + count > capacity) {
        while (length + count > capacity) {
          capacity *= 2;
        }
        RpPair * grown = (RpPair *)realloc(stack, capacity * sizeof(RpPair));
        if (grown == NULL) {
          snprintf(why, why_size, "out of memory");
          equal = false;
          break;
        }
        stack = grown;
      }
      for (size_t i = count; i > 0; i--) {
        stack[length].a = glecma_node_child(pair.a, i - 1);
        stack[length].b = glecma_node_child(pair.b, i - 1);
        length++;
      }
    }
  }
  free(stack);
  return equal;
}

/* ---- The context a function was in, derived from the tree ---- */

#define RP_STRICT GLECMA_CONTEXT_STRICT
#define RP_MODULE GLECMA_CONTEXT_MODULE
#define RP_INHERITED_BY_ARROW (GLECMA_CONTEXT_NEW_TARGET | GLECMA_CONTEXT_SUPER_PROPERTY | \
    GLECMA_CONTEXT_SUPER_CALL | GLECMA_CONTEXT_FIELD_INITIALIZER | GLECMA_CONTEXT_STATIC_BLOCK)

/* Whether a function node's body, if it has one, is strict by its own directive. */
static inline bool rp_body_strict(const GLECMA_Node * function) {
  const GLECMA_Node * body = glecma_node_child(function, 2);
  return body != NULL && glecma_node_kind(body) == GLECMA_NODE_FUNCTION_BODY &&
      (glecma_node_flags(body) & GLECMA_NODEF_STRICT) != 0;
}

/* Reads the function at `node` again, in `ctx`, and compares. `own_strict` is the strictness its code has. */
static inline void rp_check(RpState * s, const GLECMA_Node * node, uint32_t kind, RpCtx ctx,
    bool declaration, bool own_strict) {
  if (s->failed) {
    return;
  }
  GLECMA_FunctionContext context = GLECMA_FUNCTION_CONTEXT_INIT;
  context.kind = kind;
  context.start = glecma_node_start(node);
  context.end = glecma_node_end(node);
  context.private_names = ctx.names > 0 ? s->names : NULL;
  context.private_name_count = (uint32_t)ctx.names;
  if (s->recording) {
    if (s->jobs_len == s->jobs_cap) {
      size_t capacity = s->jobs_cap == 0 ? 256 : s->jobs_cap * 2;
      RpJob * grown = (RpJob *)realloc(s->jobs, capacity * sizeof(*grown));
      if (grown == NULL) {
        s->failed = true;
        snprintf(s->message, s->message_size, "out of memory");
        return;
      }
      s->jobs = grown;
      s->jobs_cap = capacity;
    }
    RpJob * job = &s->jobs[s->jobs_len++];
    job->context = context;
    job->context.flags = (ctx.flags & GLECMA_CONTEXT_FUNCTION_MASK) | (declaration ? GLECMA_FNCTX_DECLARATION : 0u);
    job->names = NULL;
    if (ctx.names > 0) {
      job->names = (GLECMA_PrivateName *)malloc(ctx.names * sizeof(GLECMA_PrivateName));
      if (job->names == NULL) {
        s->failed = true;
        snprintf(s->message, s->message_size, "out of memory");
        return;
      }
      memcpy(job->names, s->names, ctx.names * sizeof(GLECMA_PrivateName));
      job->context.private_names = job->names;
    }
    (void)own_strict;
    return;
  }
  for (int pass = 0; pass < 2 && !s->failed; pass++) {
    uint32_t flags = ctx.flags & GLECMA_CONTEXT_FUNCTION_MASK;
    if (pass == 1 && own_strict) {
      flags |= GLECMA_CONTEXT_STRICT;
    }
    context.flags = flags | (declaration ? GLECMA_FNCTX_DECLARATION : 0u);
    GLECMA_Tree * again = NULL;
    GLECMA_Failure failure = GLECMA_FAILURE_INIT;
    GLECMA_Result result = glecma_parse_function(s->source, &context, s->options, NULL,
        &failure, &again);
    if (result != GLECMA_OK) {
      char detail[200];
      snprintf(detail, sizeof detail, "glecma_parse_function answered %u (%s) at %zu, flags %x",
          (unsigned)result, failure.message != NULL ? failure.message : "", failure.location.offset,
          (unsigned)context.flags);
      rp_fail(s, "a function does not read again", node, kind, detail);
      return;
    }
    char why[240];
    if (!rp_equal(node, glecma_tree_root(again), why, sizeof why)) {
      rp_fail(s, "the tree read again differs", node, kind, why);
    } else if (glecma_tree_goal(again) != GLECMA_PARSE_FUNCTION) {
      rp_fail(s, "the tree read again records another goal", node, kind, "goal");
    } else if (glecma_tree_strict(again) != ((ctx.flags & GLECMA_CONTEXT_STRICT) != 0 || own_strict)) {
      rp_fail(s, "the tree read again records another strictness", node, kind,
          glecma_tree_strict(again) ? "strict" : "sloppy");
    }
    glecma_tree_destroy(again);
  }
  s->stats->functions++;
  s->stats->kinds[kind]++;
}

static inline bool rp_push_name(RpState * s, const uint16_t * units, size_t length) {
  if (s->names_len == s->names_cap) {
    size_t capacity = s->names_cap == 0 ? 16 : s->names_cap * 2;
    GLECMA_PrivateName * grown = (GLECMA_PrivateName *)realloc(s->names, capacity * sizeof(*grown));
    if (grown == NULL) {
      return false;
    }
    s->names = grown;
    s->names_cap = capacity;
  }
  s->names[s->names_len].units = units;
  s->names[s->names_len].length = (uint32_t)length;
  s->names[s->names_len].reserved = 0;
  s->names_len++;
  return true;
}

/* The private names a class body declares, pushed on the names stack. */
static inline void rp_push_class_names(RpState * s, const GLECMA_Node * body) {
  for (size_t i = 0; i < glecma_node_child_count(body); i++) {
    const GLECMA_Node * member = glecma_node_child(body, i);
    uint32_t kind = glecma_node_kind(member);
    if (kind != GLECMA_NODE_METHOD_DEFINITION && kind != GLECMA_NODE_FIELD_DEFINITION) {
      continue;
    }
    const GLECMA_Node * key = glecma_node_child(member, 0);
    const uint16_t * units = NULL;
    size_t length = 0;
    if (key != NULL && glecma_node_kind(key) == GLECMA_NODE_PRIVATE_NAME &&
        glecma_node_text(key, 0, &units, &length)) {
      if (!rp_push_name(s, units, length)) {
        s->failed = true;
        snprintf(s->message, s->message_size, "out of memory");
        return;
      }
    }
  }
}

static inline bool rp_push_frame(RpState * s, const GLECMA_Node * node, RpCtx ctx, size_t names_base) {
  if (s->frames_len == s->frames_cap) {
    size_t capacity = s->frames_cap == 0 ? 64 : s->frames_cap * 2;
    RpFrame * grown = (RpFrame *)realloc(s->frames, capacity * sizeof(*grown));
    if (grown == NULL) {
      s->failed = true;
      snprintf(s->message, s->message_size, "out of memory");
      return false;
    }
    s->frames = grown;
    s->frames_cap = capacity;
  }
  RpFrame * frame = &s->frames[s->frames_len++];
  frame->node = node;
  frame->next = 0;
  frame->ctx = ctx;
  frame->names_base = names_base;
  frame->derived = false;
  return true;
}

/*
 * Enters a node: finds whether it is a function that is read again, checks it,
 * and pushes the frame its children are visited in.
 * `enclosing` is the context the node sits in; `role` is what its parent says it is;
 * `derived` says, for a class body and the members of one, that the class has an
 * `extends` clause; `method_kind` is the kind of a method's function.
 */
static inline void rp_enter(RpState * s, const GLECMA_Node * node, RpCtx enclosing, int role,
    bool derived, uint32_t method_kind) {
  if (node == NULL || s->failed) {
    return;
  }
  const uint32_t kind = glecma_node_kind(node);
  const uint32_t node_flags = glecma_node_flags(node);
  const size_t names_base = s->names_len;
  if (role == RP_FIELD_INIT) {
    /* The initializer is a function of its own, read in the class's context; what is
       inside it is in the context it makes. */
    rp_check(s, node, GLECMA_FNKIND_FIELD_INITIALIZER, enclosing, false, true);
    enclosing.flags = RP_STRICT | (enclosing.flags & RP_MODULE) | GLECMA_CONTEXT_NEW_TARGET |
        GLECMA_CONTEXT_SUPER_PROPERTY | GLECMA_CONTEXT_FIELD_INITIALIZER;
    role = RP_NORMAL;
  }
  RpCtx own = enclosing;
  switch (kind) {
    case GLECMA_NODE_FUNCTION_DECLARATION:
    case GLECMA_NODE_FUNCTION_EXPRESSION: {
      const bool is_async = (node_flags & GLECMA_NODEF_ASYNC) != 0;
      const bool is_generator = (node_flags & GLECMA_NODEF_GENERATOR) != 0;
      uint32_t fkind;
      bool declaration = false;
      if (role == RP_METHOD) {
        fkind = method_kind;
        own.flags = (enclosing.flags & (RP_STRICT | RP_MODULE)) | GLECMA_CONTEXT_NEW_TARGET |
            GLECMA_CONTEXT_SUPER_PROPERTY |
            (method_kind == GLECMA_FNKIND_DERIVED_CONSTRUCTOR ? GLECMA_CONTEXT_SUPER_CALL : 0u);
      } else {
        fkind = is_async ? (is_generator ? GLECMA_FNKIND_ASYNC_GENERATOR : GLECMA_FNKIND_ASYNC_FUNCTION)
                         : (is_generator ? GLECMA_FNKIND_GENERATOR : GLECMA_FNKIND_FUNCTION);
        declaration = kind == GLECMA_NODE_FUNCTION_DECLARATION;
        own.flags = (enclosing.flags & (RP_STRICT | RP_MODULE)) | GLECMA_CONTEXT_NEW_TARGET;
      }
      own.flags |= (is_generator ? GLECMA_CONTEXT_YIELD : 0u) | (is_async ? GLECMA_CONTEXT_AWAIT : 0u);
      const bool body_strict = rp_body_strict(node);
      rp_check(s, node, fkind, enclosing, declaration, (enclosing.flags & RP_STRICT) != 0 || body_strict);
      if (body_strict) {
        own.flags |= RP_STRICT;
      }
      break;
    }
    case GLECMA_NODE_ARROW_FUNCTION: {
      const bool is_async = (node_flags & GLECMA_NODEF_ASYNC) != 0;
      /* An arrow takes `arguments` from a static block's code but not its reserved
         `await`: its body is not the block's own code. */
      own.flags = (enclosing.flags & (RP_STRICT | RP_MODULE | RP_INHERITED_BY_ARROW)) |
          ((enclosing.flags & GLECMA_CONTEXT_STATIC_BLOCK) != 0 ? GLECMA_CONTEXT_FIELD_INITIALIZER : 0u) |
          (is_async ? GLECMA_CONTEXT_AWAIT : 0u);
      own.flags &= ~(uint32_t)GLECMA_CONTEXT_STATIC_BLOCK;
      const bool body_strict = rp_body_strict(node);
      rp_check(s, node, is_async ? GLECMA_FNKIND_ASYNC_ARROW : GLECMA_FNKIND_ARROW, enclosing,
          false, (enclosing.flags & RP_STRICT) != 0 || body_strict);
      if (body_strict) {
        own.flags |= RP_STRICT;
      }
      break;
    }
    case GLECMA_NODE_STATIC_BLOCK:
      own.flags = RP_STRICT | (enclosing.flags & RP_MODULE) | GLECMA_CONTEXT_NEW_TARGET |
          GLECMA_CONTEXT_SUPER_PROPERTY | GLECMA_CONTEXT_STATIC_BLOCK;
      rp_check(s, node, GLECMA_FNKIND_STATIC_BLOCK, enclosing, false, true);
      break;
    case GLECMA_NODE_CLASS_DECLARATION:
    case GLECMA_NODE_CLASS_EXPRESSION:
      /* Every part of a class is strict code. */
      own.flags = enclosing.flags | RP_STRICT;
      break;
    case GLECMA_NODE_CLASS_BODY:
      /* The members are strict and see the names the class declares. */
      own.flags = enclosing.flags | RP_STRICT;
      rp_push_class_names(s, node);
      own.names = s->names_len;
      break;
    default:
      break;
  }
  if (s->failed || !rp_push_frame(s, node, own, names_base)) {
    return;
  }
  s->frames[s->frames_len - 1].derived = derived;
}

/* How a child is entered, as its parent frame says. */
static inline void rp_enter_child(RpState * s, size_t frame_index, const GLECMA_Node * child, size_t index) {
  RpFrame frame = s->frames[frame_index];
  const uint32_t kind = glecma_node_kind(frame.node);
  const uint32_t flags = glecma_node_flags(frame.node);
  switch (kind) {
    case GLECMA_NODE_CLASS_DECLARATION:
    case GLECMA_NODE_CLASS_EXPRESSION:
      /* [id, heritage, body]: the body is the one that is the class's own. */
      rp_enter(s, child, frame.ctx, RP_NORMAL, index == 2 && (flags & GLECMA_NODEF_DERIVED) != 0, 0);
      return;
    case GLECMA_NODE_CLASS_BODY:
      rp_enter(s, child, frame.ctx, RP_NORMAL, frame.derived, 0);
      return;
    case GLECMA_NODE_METHOD_DEFINITION:
      if (index == 1) {
        uint32_t method_kind = GLECMA_FNKIND_METHOD;
        if ((flags & GLECMA_NODEF_GETTER) != 0) {
          method_kind = GLECMA_FNKIND_GETTER;
        } else if ((flags & GLECMA_NODEF_SETTER) != 0) {
          method_kind = GLECMA_FNKIND_SETTER;
        } else if ((flags & GLECMA_NODEF_CONSTRUCTOR) != 0) {
          method_kind = frame.derived ? GLECMA_FNKIND_DERIVED_CONSTRUCTOR : GLECMA_FNKIND_CONSTRUCTOR;
        }
        rp_enter(s, child, frame.ctx, RP_METHOD, false, method_kind);
        return;
      }
      break;
    case GLECMA_NODE_PROPERTY:
      if (index == 1 && (flags & (GLECMA_NODEF_GETTER | GLECMA_NODEF_SETTER | GLECMA_NODEF_METHOD)) != 0) {
        uint32_t method_kind = (flags & GLECMA_NODEF_GETTER) != 0 ? GLECMA_FNKIND_GETTER
            : (flags & GLECMA_NODEF_SETTER) != 0 ? GLECMA_FNKIND_SETTER : GLECMA_FNKIND_METHOD;
        rp_enter(s, child, frame.ctx, RP_METHOD, false, method_kind);
        return;
      }
      break;
    case GLECMA_NODE_FIELD_DEFINITION:
      if (index == 1) {
        rp_enter(s, child, frame.ctx, RP_FIELD_INIT, false, 0);
        return;
      }
      break;
    default:
      break;
  }
  rp_enter(s, child, frame.ctx, RP_NORMAL, false, 0);
}

/*
 * Reads every function of the tree again and compares. Returns 0 when each reads again
 * as it was; otherwise 1 with `message` describing the first that did not. `options` is
 * the options the tree was parsed with (its limits and features), or NULL.
 */
static inline int rp_walk(GLECMA_Source * source, const GLECMA_Tree * tree,
    const GLECMA_ParseOptions * options, RpStats * stats, char * message, size_t message_size,
    RpJob ** out_jobs, size_t * out_count) {
  RpState state;
  memset(&state, 0, sizeof state);
  state.recording = out_jobs != NULL;
  /* The function is read with the tree's limits and feature set, and no goal of its own. */
  GLECMA_ParseOptions reread = GLECMA_PARSE_OPTIONS_INIT;
  if (options != NULL) {
    reread.limits = options->limits;
    reread.features = options->features;
  }
  state.source = source;
  state.tree = tree;
  state.options = &reread;
  state.stats = stats;
  state.message = message;
  state.message_size = message_size;
  message[0] = '\0';
  const GLECMA_Node * root = glecma_tree_root(tree);
  if (root == NULL) {
    return 0;
  }
  RpCtx top;
  top.names = 0;
  top.flags = 0;
  if (glecma_node_kind(root) == GLECMA_NODE_MODULE) {
    top.flags = RP_STRICT | RP_MODULE | GLECMA_CONTEXT_AWAIT;
  } else if ((glecma_node_flags(root) & GLECMA_NODEF_STRICT) != 0) {
    top.flags = RP_STRICT;
  }
  /* Direct eval code is read in the context of its caller, which the tree records. */
  const GLECMA_EvalContext * eval = glecma_tree_eval_context(tree);
  if (eval != NULL) {
    top.flags |= eval->flags & GLECMA_CONTEXT_EVAL_MASK;
    for (uint32_t i = 0; i < eval->private_name_count; i++) {
      if (!rp_push_name(&state, eval->private_names[i].units, eval->private_names[i].length)) {
        free(state.names);
        return 1;
      }
    }
    top.names = state.names_len;
  }
  const uint32_t root_kind = glecma_node_kind(root);
  if (root_kind != GLECMA_NODE_SCRIPT && root_kind != GLECMA_NODE_MODULE) {
    /* The tree of a dynamic function is the function itself. */
    rp_enter(&state, root, top, RP_NORMAL, false, 0);
  } else if (!rp_push_frame(&state, root, top, 0)) {
    free(state.names);
    free(state.frames);
    return 1;
  }
  while (state.frames_len > 0 && !state.failed) {
    size_t top_index = state.frames_len - 1;
    RpFrame * frame = &state.frames[top_index];
    if (frame->next >= glecma_node_child_count(frame->node)) {
      state.names_len = frame->names_base;
      state.frames_len--;
      continue;
    }
    size_t index = frame->next++;
    rp_enter_child(&state, top_index, glecma_node_child(frame->node, index), index);
  }
  free(state.names);
  free(state.frames);
  if (out_jobs != NULL) {
    *out_jobs = state.jobs;
    *out_count = state.jobs_len;
  }
  return state.failed ? 1 : 0;
}

static inline int rp_check_tree(GLECMA_Source * source, const GLECMA_Tree * tree,
    const GLECMA_ParseOptions * options, RpStats * stats, char * message, size_t message_size) {
  return rp_walk(source, tree, options, stats, message, message_size, NULL, NULL);
}

/* The functions of a tree, as the contexts to read them again with, without reading them. */
static inline int rp_collect(GLECMA_Source * source, const GLECMA_Tree * tree, RpJob ** jobs, size_t * count,
    char * message, size_t message_size) {
  RpStats stats;
  memset(&stats, 0, sizeof stats);
  return rp_walk(source, tree, NULL, &stats, message, message_size, jobs, count);
}

static inline void rp_free_jobs(RpJob * jobs, size_t count) {
  for (size_t i = 0; i < count; i++) {
    free(jobs[i].names);
  }
  free(jobs);
}

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GLECMA_TESTS_REPARSE_SUPPORT_H */

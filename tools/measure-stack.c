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
 * @file
 *
 * Measures what one level of nesting costs the parser in native stack, for
 * each shape of input, and prints it. It is how the defaults of
 * `GLECMA_Limits::nesting` and `GLECMA_Limits::native_stack_bytes` were found
 * (documentation/design.md, "Recursive descent and the stack"), and it is
 * run again on each target the library is built for.
 *
 * For each shape it parses the shape at two depths and, by bisection, finds
 * the least `nesting` and the least `native_stack_bytes` that admit each; the
 * difference in bytes over the difference in levels is the cost of a level.
 * The parse is given a huge budget in the field it is not measuring.
 *
 * Build and run it by hand against the built library, for example:
 *
 *   cc -std=c17 -O2 -I include -I src -I build/<target>/generated \
 *      $(pkg-config --cflags ghoti.io-cutil-0 ghoti.io-runtime-core-0 \
 *        ghoti.io-unicode-0) tools/measure-stack.c \
 *      -Wl,--whole-archive build/<target>/apps/libghoti.io-lang-ecmascript-0.a \
 *      -Wl,--no-whole-archive $(pkg-config --libs ghoti.io-runtime-core-0 \
 *        ghoti.io-unicode-0 ghoti.io-cutil-0) -lm -pthread
 */

#include <ghoti.io/lang-ecmascript/lang-ecmascript.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
typedef char * (*Gen)(size_t n);
static char * rep(const char * a, const char * mid, const char * b, size_t n) {
  size_t la = strlen(a), lb = strlen(b), lm = strlen(mid);
  char * s = malloc(n * (la + lb) + lm + 8); char * p = s;
  for (size_t i = 0; i < n; i++) { memcpy(p, a, la); p += la; }
  memcpy(p, mid, lm); p += lm;
  for (size_t i = 0; i < n; i++) { memcpy(p, b, lb); p += lb; }
  *p = 0; return s;
}
static char * g_paren(size_t n) { return rep("(", "x", ")", n); }
static char * g_array(size_t n) { return rep("[", "x", "]", n); }
static char * g_object(size_t n) { return rep("({a:", "x", "})", n); }
static char * g_block(size_t n) { return rep("{", "", "}", n); }
static char * g_func(size_t n) { return rep("function f(){", "", "}", n); }
static char * g_arrowbody(size_t n) { return rep("()=>", "x", "", n); }
static char * g_call(size_t n) { return rep("f(", "x", ")", n); }
static char * g_unary(size_t n) { return rep("!", "x", "", n); }
static char * g_assign(size_t n) { return rep("a=", "x", "", n); }
static char * g_exp(size_t n) { return rep("a**", "x", "", n); }
static char * g_cond(size_t n) { return rep("a?", "x", ":b", n); }
static char * g_template(size_t n) { return rep("`${", "x", "}`", n); }
static char * g_if(size_t n) { return rep("if(a)", "x;", "", n); }
static char * g_class(size_t n) { return rep("class A{m(){", "", "}}", n); }
static char * g_pattern(size_t n) { char * a = rep("[", "x", "]", n); char * s = malloc(strlen(a) + 8); sprintf(s, "%s=y", a); free(a); return s; }
static char * g_arrowparam(size_t n) { return rep("(a=", "x", ")=>0", n); }
static char * g_new(size_t n) { return rep("new ", "x", "", n); }
/* Each label is its own: a label cannot be declared inside itself. */
static char * g_label(size_t n) {
  char * s = malloc(n * 12 + 8); char * p = s;
  for (size_t i = 0; i < n; i++) { p += sprintf(p, "l%zu:", i); }
  strcpy(p, "x;"); return s;
}
static char * g_await(size_t n) { return rep("async function f(){await ", "x", "}", n); }
static int try_parse(const char * src, uint64_t nesting, uint64_t stack) {
  GLECMA_Source * s; if (glecma_source_create_utf8(src, strlen(src), NULL, NULL, NULL, &s)) return -1;
  GLECMA_Limits l = GLECMA_LIMITS_INIT; l.nesting = nesting; l.native_stack_bytes = stack;
  GLECMA_ParseOptions o = GLECMA_PARSE_OPTIONS_INIT; o.limits = &l;
  GLECMA_Tree * t = NULL; GLECMA_Failure f = GLECMA_FAILURE_INIT;
  GLECMA_Result r = glecma_parse(s, &o, NULL, &f, &t);
  glecma_tree_destroy(t); glecma_source_release(s);
  if (r == GLECMA_OK) return 1;
  if (r == GLECMA_ERR_LIMIT) return 0;
  fprintf(stderr, "unexpected %d %s\n", r, f.message ? f.message : ""); return -1;
}
static uint64_t minimize(const char * src, int which) {
  uint64_t lo = 0, hi = which ? (uint64_t)1 << 30 : 1000000;
  while (lo + 1 < hi) { uint64_t mid = (lo + hi) / 2;
    int ok = which ? try_parse(src, 1000000, mid) : try_parse(src, mid, (uint64_t)1 << 30);
    if (ok == 1) hi = mid; else lo = mid; }
  return hi;
}
int main(void) {
  struct { const char * name; Gen g; } shapes[] = {
    {"paren", g_paren}, {"array", g_array}, {"object", g_object}, {"block", g_block}, {"function", g_func},
    {"arrow-body", g_arrowbody}, {"call", g_call}, {"unary", g_unary}, {"assign", g_assign}, {"exp", g_exp},
    {"conditional", g_cond}, {"template", g_template}, {"if", g_if}, {"class", g_class}, {"pattern", g_pattern},
    {"arrow-param", g_arrowparam}, {"new", g_new}, {"label", g_label}, {"await", g_await}};
  double worst = 0;
  for (size_t i = 0; i < sizeof shapes / sizeof shapes[0]; i++) {
    char * a = shapes[i].g(50); char * b = shapes[i].g(150);
    uint64_t na = minimize(a, 0), nb = minimize(b, 0), ba = minimize(a, 1), bb = minimize(b, 1);
    double per = (double)(bb - ba) / (double)(nb - na);
    printf("%-12s levels %llu -> %llu  bytes %llu -> %llu  per level %.0f\n", shapes[i].name,
        (unsigned long long)na, (unsigned long long)nb, (unsigned long long)ba, (unsigned long long)bb, per);
    if (per > worst) worst = per;
    free(a); free(b);
  }
  printf("worst per level %.0f\n", worst);
  return 0;
}

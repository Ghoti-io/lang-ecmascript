/*
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * Copyright (C) 2024-2026 Corey Pennycuff
 *
 * This file is part of Ghoti.io Lang-ecmascript.
 *
 * Ghoti.io Lang-ecmascript is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License version
 * 3 as published by the Free Software Foundation.
 *
 * Ghoti.io Lang-ecmascript is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file
 *
 * The benchmark harness.
 *
 * A way to check "performant" from the first commit. It holds a calibration
 * case, the decoder-throughput cases (creating a source from UTF-8 and from
 * UTF-16 text, which decodes every code point and indexes its lines), the
 * tokenizer-throughput cases (reading a source to the end, token by token)
 * and the parser cases (parsing a source into a tree, with each builder, and
 * destroying a tree).
 *
 * The calibration case is a fixed amount of integer work that touches no
 * library code, run the same way every real case is, so a figure from a real
 * case can be read against the machine it was taken on. A figure recorded
 * without the calibration beside it cannot be compared across hosts or
 * compilers.
 *
 * The cases run over the seed files in bench/seeds (named in
 * documentation/benchmarks.md). Each seed is repeated up to a fixed size at
 * setup, so a unit of work is long enough to time and the seed set stays
 * small. The decoder cases use the first three seeds and the tokenizer and
 * parser cases all five, so that adding a seed for one does not change the
 * other's workload. The decoder clock covers the creation and release of the
 * source and not the setup; the tokenizer clock covers reading the tokens of
 * sources already made; the parser clock covers a parse and not the release
 * of its tree, and the destruction case's covers only the release, which is
 * the whole of what it measures.
 *
 * Usage:
 *   bench                 run every case: several repeats, report best, median
 *                         and spread
 *   bench --smoke         run every case once on a tiny workload (what `make
 *                         test` does): proves the harness builds, links and
 *                         runs
 *   bench --only PREFIX   run the cases whose name begins with PREFIX
 *   bench --seeds DIR     the seed directory (default bench/seeds)
 *
 * No numeric budget is asserted here: a budget needs a first measurement of
 * its case, and the figures are recorded in documentation/benchmarks.md.
 */

/* clock_gettime(CLOCK_MONOTONIC) is POSIX, and -std=c17 hides it. */
#define _POSIX_C_SOURCE 200809L

#include <ghoti.io/lang-ecmascript/lang-ecmascript.h>

#include "lex/lexer.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define REPEATS 9
#define SEED_TARGET_BYTES (256u * 1024u)
#define MAX_SEEDS 16

typedef struct {
  const char * name;
  /** Performs @p iterations units of work; returns a value derived from all
   *  of it so the compiler cannot discard the loop, and stores in @p elapsed
   *  the nanoseconds the measured part took (setup is not in it). */
  uint64_t (*run)(uint64_t iterations, double * elapsed);
  /** Bytes of input one unit processes; 0 when not a throughput case. */
  uint64_t (*bytes_per_iter)(void);
  /** Tokens one unit processes; NULL when the case does not tokenize. */
  uint64_t (*tokens_per_iter)(void);
  uint64_t iterations;       /* per repeat, full run */
  uint64_t smoke_iterations; /* per repeat, --smoke */
} Case;

/* A case that cannot set itself up must not report a short, fast run as a
 * measurement. */
static _Noreturn void setup_failed(const char * what) {
  fprintf(stderr, "bench: setup failed: %s\n", what);
  abort();
}

static double now_ns(void) {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    return 0.0;
  }
  return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

/* xorshift64: one dependent chain of shifts and xors per step. The chain is
 * serial on purpose, so the figure is latency-bound and does not move with
 * how wide the host's execution units are. */
static uint64_t calibration_run(uint64_t iterations, double * elapsed) {
  uint64_t x = 0x9E3779B97F4A7C15ull;
  double start = now_ns();
  for (uint64_t i = 0; i < iterations; i++) {
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
  }
  *elapsed = now_ns() - start;
  return x;
}

/* ---- The seed set ------------------------------------------------------- */

static const char * seed_dir = "bench/seeds";

typedef struct {
  char name[64];
  char * utf8;        /* the seed repeated to about SEED_TARGET_BYTES */
  size_t utf8_bytes;
  uint16_t * utf16;   /* the same text, transcoded */
  size_t utf16_units;
  GLECMA_Source * source8;  /* made at setup, for the tokenizer cases */
  GLECMA_Source * source16;
  uint64_t tokens;    /* tokens in one reading of the text */
} Seed;

static Seed seeds[MAX_SEEDS];
static size_t seed_count = 0;
static bool seeds_loaded = false;

/* The first DECODE_SEEDS are the decoder cases' workload; every one is the
 * tokenizer cases'. */
#define DECODE_SEEDS 3
static const char * const seed_names[] = {"ascii.js", "multibyte.js", "terminators.js", "tokens.js", "minified.js"};

/* A complete seed is whole code points, so repeating it keeps the text valid. */
static void load_seed(Seed * s, const char * name) {
  char path[512];
  if (snprintf(path, sizeof(path), "%s/%s", seed_dir, name) >= (int)sizeof(path)) {
    setup_failed("seed path too long");
  }
  FILE * f = fopen(path, "rb");
  if (!f) {
    fprintf(stderr, "bench: cannot open seed %s (run from the library root or pass --seeds)\n", path);
    setup_failed("seed missing");
  }
  char raw[4096];
  size_t n = fread(raw, 1, sizeof(raw), f);
  if (ferror(f) || n == 0 || !feof(f)) {
    setup_failed("seed unreadable, empty or larger than 4 KiB");
  }
  fclose(f);

  /* Each copy is a block of its own: a seed that declares a let, a const, a
   * class or (in strict code) a function would otherwise declare it again in
   * every copy, which is an early error. */
  static const char open_block[] = "{\n";
  static const char close_block[] = "\n}\n";
  const size_t unit = n + (sizeof(open_block) - 1) + (sizeof(close_block) - 1);
  size_t copies = (SEED_TARGET_BYTES + unit - 1) / unit;
  s->utf8_bytes = copies * unit;
  s->utf8 = malloc(s->utf8_bytes);
  if (!s->utf8) {
    setup_failed("out of memory");
  }
  for (size_t i = 0; i < copies; i++) {
    char * at = s->utf8 + i * unit;
    memcpy(at, open_block, sizeof(open_block) - 1);
    memcpy(at + sizeof(open_block) - 1, raw, n);
    memcpy(at + sizeof(open_block) - 1 + n, close_block, sizeof(close_block) - 1);
  }

  /* Transcode, so the UTF-16 case measures the same text. */
  s->utf16 = malloc(s->utf8_bytes * sizeof(uint16_t));
  if (!s->utf16) {
    setup_failed("out of memory");
  }
  size_t out = 0;
  for (size_t i = 0; i < s->utf8_bytes;) {
    unsigned char c = (unsigned char)s->utf8[i];
    uint32_t cp;
    size_t len;
    if (c < 0x80) { cp = c; len = 1; }
    else if ((c & 0xE0) == 0xC0) { cp = c & 0x1Fu; len = 2; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0Fu; len = 3; }
    else { cp = c & 0x07u; len = 4; }
    if (i + len > s->utf8_bytes) {
      setup_failed("seed is not whole UTF-8");
    }
    for (size_t k = 1; k < len; k++) {
      cp = (cp << 6) | ((unsigned char)s->utf8[i + k] & 0x3Fu);
    }
    if (cp >= 0x10000u) {
      cp -= 0x10000u;
      s->utf16[out++] = (uint16_t)(0xD800u + (cp >> 10));
      s->utf16[out++] = (uint16_t)(0xDC00u + (cp & 0x3FFu));
    }
    else {
      s->utf16[out++] = (uint16_t)cp;
    }
    i += len;
  }
  s->utf16_units = out;
  snprintf(s->name, sizeof(s->name), "%s", name);

  /* The tokenizer cases read sources that already exist. */
  if (glecma_source_create_utf8(s->utf8, s->utf8_bytes, NULL, NULL, NULL, &s->source8) != GLECMA_OK ||
      glecma_source_create_utf16(s->utf16, s->utf16_units, NULL, NULL, NULL, &s->source16) != GLECMA_OK) {
    setup_failed("a seed was refused by the library");
  }
}

static void load_seeds(void) {
  if (seeds_loaded) {
    return;
  }
  for (size_t i = 0; i < sizeof(seed_names) / sizeof(seed_names[0]); i++) {
    load_seed(&seeds[seed_count++], seed_names[i]);
  }
  seeds_loaded = true;
}

/* One unit of work is one source created from every seed, then released. */
static uint64_t decode_run(uint64_t iterations, double * elapsed, bool utf16) {
  load_seeds();
  uint64_t sink = 0;
  double start = now_ns();
  for (uint64_t i = 0; i < iterations; i++) {
    for (size_t s = 0; s < DECODE_SEEDS; s++) {
      GLECMA_Source * src = NULL;
      GLECMA_Result r = utf16
          ? glecma_source_create_utf16(seeds[s].utf16, seeds[s].utf16_units, NULL, NULL, NULL, &src)
          : glecma_source_create_utf8(seeds[s].utf8, seeds[s].utf8_bytes, NULL, NULL, NULL, &src);
      if (r != GLECMA_OK) {
        setup_failed("a seed was refused by the library");
      }
      sink += glecma_source_code_points(src) + glecma_source_lines(src);
      glecma_source_release(src);
    }
  }
  *elapsed = now_ns() - start;
  return sink;
}

static uint64_t decode_utf8_run(uint64_t iterations, double * elapsed) {
  return decode_run(iterations, elapsed, false);
}

static uint64_t decode_utf16_run(uint64_t iterations, double * elapsed) {
  return decode_run(iterations, elapsed, true);
}

static uint64_t decode_utf8_bytes(void) {
  load_seeds();
  uint64_t total = 0;
  for (size_t s = 0; s < DECODE_SEEDS; s++) {
    total += seeds[s].utf8_bytes;
  }
  return total;
}

static uint64_t decode_utf16_bytes(void) {
  load_seeds();
  uint64_t total = 0;
  for (size_t s = 0; s < DECODE_SEEDS; s++) {
    total += seeds[s].utf16_units * sizeof(uint16_t);
  }
  return total;
}


/* ---- The tokenizer ------------------------------------------------------- */

static bool count_token(void * context, const GLECMA_Token * token) {
  (void)token;
  ++*(uint64_t *)context;
  return true;
}

/* One reading of a source to its end, in the division goal after names and
 * the regular expression goal elsewhere (the library's test driver). Returns
 * the number of tokens. */
static uint64_t read_tokens(const GLECMA_Source * source) {
  GLECMA_Lexer lexer;
  if (glecma_lexer_open(&lexer, source, NULL, 0, NULL) != GLECMA_OK) {
    setup_failed("the tokenizer would not open");
  }
  uint64_t tokens = 0;
  if (glecma_lexer_run(&lexer, count_token, &tokens) != GLECMA_OK) {
    setup_failed("a seed was refused by the tokenizer");
  }
  return tokens;
}

/* One unit of work is every seed read to its end. */
static uint64_t tokenize_run(uint64_t iterations, double * elapsed, bool utf16) {
  load_seeds();
  uint64_t sink = 0;
  double start = now_ns();
  for (uint64_t i = 0; i < iterations; i++) {
    for (size_t s = 0; s < seed_count; s++) {
      sink += read_tokens(utf16 ? seeds[s].source16 : seeds[s].source8);
    }
  }
  *elapsed = now_ns() - start;
  return sink;
}

static uint64_t tokenize_utf8_run(uint64_t iterations, double * elapsed) {
  return tokenize_run(iterations, elapsed, false);
}

static uint64_t tokenize_utf16_run(uint64_t iterations, double * elapsed) {
  return tokenize_run(iterations, elapsed, true);
}

static uint64_t tokenize_utf8_bytes(void) {
  load_seeds();
  uint64_t total = 0;
  for (size_t s = 0; s < seed_count; s++) {
    total += seeds[s].utf8_bytes;
  }
  return total;
}

static uint64_t tokenize_utf16_bytes(void) {
  load_seeds();
  uint64_t total = 0;
  for (size_t s = 0; s < seed_count; s++) {
    total += seeds[s].utf16_units * sizeof(uint16_t);
  }
  return total;
}

static uint64_t tokenize_tokens(void) {
  load_seeds();
  uint64_t total = 0;
  for (size_t s = 0; s < seed_count; s++) {
    total += read_tokens(seeds[s].source8);
  }
  return total;
}

/* ---- The parser ---------------------------------------------------------- */

#include "parse/parse_internal.h"

/* One unit of work is every seed parsed to a tree. The trees are released
 * after the clock stops; the destruction case is the other way round. */
static uint64_t parse_run(uint64_t iterations, double * elapsed, bool utf16,
    uint32_t internal) {
  load_seeds();
  uint64_t sink = 0;
  double total = 0.0;
  GLECMA_Tree * trees[MAX_SEEDS];
  for (uint64_t i = 0; i < iterations; i++) {
    double start = now_ns();
    for (size_t s = 0; s < seed_count; s++) {
      GLECMA_Source * source = utf16 ? seeds[s].source16 : seeds[s].source8;
      GLECMA_Failure failure = GLECMA_FAILURE_INIT;
      if (glecma_parse_internal(source, NULL, NULL, internal, &failure, &trees[s]) != GLECMA_OK) {
        setup_failed("a seed was refused by the parser");
      }
    }
    total += now_ns() - start;
    for (size_t s = 0; s < seed_count; s++) {
      sink += glecma_node_end(glecma_tree_root(trees[s]));
      glecma_tree_destroy(trees[s]);
    }
  }
  *elapsed = total;
  return sink;
}

static uint64_t parse_utf8_run(uint64_t iterations, double * elapsed) {
  return parse_run(iterations, elapsed, false, 0);
}

static uint64_t parse_utf16_run(uint64_t iterations, double * elapsed) {
  return parse_run(iterations, elapsed, true, 0);
}

static uint64_t parse_checking_utf8_run(uint64_t iterations, double * elapsed) {
  return parse_run(iterations, elapsed, false, GLECMA_PARSE_INTERNAL_CHECKING);
}

/* One unit of work is every seed's tree released; the parse that made it is
 * not in the clock. */
static uint64_t tree_destroy_run(uint64_t iterations, double * elapsed) {
  load_seeds();
  uint64_t sink = 0;
  double total = 0.0;
  GLECMA_Tree * trees[MAX_SEEDS];
  for (uint64_t i = 0; i < iterations; i++) {
    for (size_t s = 0; s < seed_count; s++) {
      if (glecma_parse(seeds[s].source8, NULL, NULL, NULL, &trees[s]) != GLECMA_OK) {
        setup_failed("a seed was refused by the parser");
      }
      sink += glecma_node_end(glecma_tree_root(trees[s]));
    }
    double start = now_ns();
    for (size_t s = 0; s < seed_count; s++) {
      glecma_tree_destroy(trees[s]);
    }
    total += now_ns() - start;
  }
  *elapsed = total;
  return sink;
}

/* ---- The Module goal and the function re-parse -------------------------- */

#include "../tests/reparse_support.h"

/* A module of about SEED_TARGET_BYTES: imports with attributes, exports of
 * each form, top-level await and import.meta, each copy with names of its own
 * (a module's declarations are unique). */
static GLECMA_Source * module_source = NULL;
static size_t module_bytes = 0;

/* Copies `text`, writing the number `index` where it has an `@`. */
static size_t expand(char * out, size_t room, const char * text, unsigned index) {
  size_t length = 0;
  for (; *text != '\0'; text++) {
    if (*text == '@') {
      int n = snprintf(out + length, room - length, "%u", index);
      if (n < 0 || (size_t)n >= room - length) {
        setup_failed("the module seed overflowed");
      }
      length += (size_t)n;
    } else {
      if (length + 1 >= room) {
        setup_failed("the module seed overflowed");
      }
      out[length++] = *text;
    }
  }
  out[length] = '\0';
  return length;
}

static void load_module(void) {
  if (module_source != NULL) {
    return;
  }
  static const char * const lines[] = {
    "import d@, {a@ as b@, 'str name' as c@} from 'm@.js' with {type: 'json', kind@: 'x'};\n",
    "import * as ns@ from 'n@.js';\n",
    "export function f@(x, y = b@) { return x + y + c@ + d@; }\n",
    "export const k@ = await ns@.load(import.meta.url, `t${f@(1)}`);\n",
    "export {b@ as e@, c@ as 'q r@'};\n",
    "export * as w@ from 'o@.js';\n",
    "export class K@ extends d@ { #p = 1; static { this.s = f@; } get p() { return this.#p; } }\n",
  };
  size_t capacity = SEED_TARGET_BYTES + 4096, length = 0;
  char * text = malloc(capacity);
  if (text == NULL) {
    setup_failed("out of memory");
  }
  for (unsigned i = 0; length + 1024 < capacity; i++) {
    for (size_t k = 0; k < sizeof(lines) / sizeof(lines[0]); k++) {
      length += expand(text + length, capacity - length, lines[k], i);
    }
  }
  /* A module exports `default` once. */
  length += expand(text + length, capacity - length, "export default class Z extends d0 {}\n", 0);
  module_bytes = length;
  if (glecma_source_create_utf8(text, length, NULL, NULL, NULL, &module_source) != GLECMA_OK) {
    setup_failed("the module was refused by the library");
  }
  free(text);
}

static uint64_t parse_module_run(uint64_t iterations, double * elapsed) {
  load_module();
  GLECMA_ParseOptions options = GLECMA_PARSE_OPTIONS_INIT;
  options.goal = GLECMA_PARSE_MODULE;
  uint64_t sink = 0;
  double total = 0.0;
  for (uint64_t i = 0; i < iterations; i++) {
    GLECMA_Tree * tree = NULL;
    double start = now_ns();
    GLECMA_Failure failure = GLECMA_FAILURE_INIT;
    if (glecma_parse(module_source, &options, NULL, &failure, &tree) != GLECMA_OK) {
      fprintf(stderr, "bench: %s at %zu\n", failure.message ? failure.message : "?", failure.location.offset);
      setup_failed("the module was refused by the parser");
    }
    total += now_ns() - start;
    sink += glecma_node_end(glecma_tree_root(tree));
    glecma_tree_destroy(tree);
  }
  *elapsed = total;
  return sink;
}

static uint64_t module_bytes_per_iter(void) {
  load_module();
  return module_bytes;
}

/* One unit of work is every function of every seed read again from its range, in the context it had.
 * The functions are found once, outside the clock, by the walk the re-parse gate uses. */
static RpJob * reparse_jobs[MAX_SEEDS];
static size_t reparse_counts[MAX_SEEDS];
static uint64_t reparse_total_bytes = 0;
static bool reparse_ready = false;

static void load_reparse(void) {
  if (reparse_ready) {
    return;
  }
  load_seeds();
  for (size_t s = 0; s < seed_count; s++) {
    GLECMA_Tree * tree = NULL;
    char message[200];
    if (glecma_parse(seeds[s].source8, NULL, NULL, NULL, &tree) != GLECMA_OK ||
        rp_collect(seeds[s].source8, tree, &reparse_jobs[s], &reparse_counts[s], message, sizeof message) != 0) {
      setup_failed("a seed's functions could not be found");
    }
    for (size_t j = 0; j < reparse_counts[s]; j++) {
      reparse_total_bytes += reparse_jobs[s][j].context.end - reparse_jobs[s][j].context.start;
    }
    /* The private names of a job point into this tree's texts: it lives as long as the process. */
    (void)tree;
  }
  uint64_t functions = 0;
  for (size_t s = 0; s < seed_count; s++) {
    functions += reparse_counts[s];
  }
  fprintf(stderr, "bench: the re-parse case reads %llu functions, %llu bytes of function text, per unit\n",
      (unsigned long long)functions, (unsigned long long)reparse_total_bytes);
  reparse_ready = true;
}

static uint64_t reparse_run(uint64_t iterations, double * elapsed) {
  load_reparse();
  uint64_t sink = 0;
  double total = 0.0;
  GLECMA_Tree ** trees = NULL;
  size_t room = 0;
  for (size_t s = 0; s < seed_count; s++) {
    room += reparse_counts[s];
  }
  trees = (GLECMA_Tree **)malloc((room == 0 ? 1 : room) * sizeof(*trees));
  if (trees == NULL || room == 0) {
    setup_failed("no function to read again, or out of memory");
  }
  for (uint64_t i = 0; i < iterations; i++) {
    size_t at = 0;
    double start = now_ns();
    for (size_t s = 0; s < seed_count; s++) {
      for (size_t j = 0; j < reparse_counts[s]; j++) {
        GLECMA_Failure failure = GLECMA_FAILURE_INIT;
        if (glecma_parse_function(seeds[s].source8, &reparse_jobs[s][j].context, NULL, NULL, &failure,
                &trees[at]) != GLECMA_OK) {
          fprintf(stderr, "bench: %s (kind %u, flags %x, [%llu,%llu)) in %s\n",
              failure.message ? failure.message : "?", (unsigned)reparse_jobs[s][j].context.kind,
              (unsigned)reparse_jobs[s][j].context.flags,
              (unsigned long long)reparse_jobs[s][j].context.start,
              (unsigned long long)reparse_jobs[s][j].context.end, seeds[s].name);
          setup_failed("a function was refused when read again");
        }
        at++;
      }
    }
    total += now_ns() - start;
    for (size_t k = 0; k < at; k++) {
      sink += glecma_node_end(glecma_tree_root(trees[k]));
      glecma_tree_destroy(trees[k]);
    }
  }
  free(trees);
  *elapsed = total;
  return sink;
}

static uint64_t reparse_bytes_per_iter(void) {
  load_reparse();
  return reparse_total_bytes;
}

static const Case cases[] = {
  {"calibration-xorshift", calibration_run, NULL, NULL, 200000000ull, 1000000ull},
  {"source-decode-utf8", decode_utf8_run, decode_utf8_bytes, NULL, 200ull, 1ull},
  {"source-decode-utf16", decode_utf16_run, decode_utf16_bytes, NULL, 200ull, 1ull},
  {"tokenize-utf8", tokenize_utf8_run, tokenize_utf8_bytes, tokenize_tokens, 100ull, 1ull},
  {"tokenize-utf16", tokenize_utf16_run, tokenize_utf16_bytes, tokenize_tokens, 100ull, 1ull},
  {"parse-utf8", parse_utf8_run, tokenize_utf8_bytes, tokenize_tokens, 50ull, 1ull},
  {"parse-utf16", parse_utf16_run, tokenize_utf16_bytes, tokenize_tokens, 50ull, 1ull},
  {"parse-checking-utf8", parse_checking_utf8_run, tokenize_utf8_bytes, tokenize_tokens, 50ull, 1ull},
  {"tree-destroy", tree_destroy_run, tokenize_utf8_bytes, NULL, 50ull, 1ull},
  {"parse-module-utf8", parse_module_run, module_bytes_per_iter, NULL, 50ull, 1ull},
  {"reparse-functions-utf8", reparse_run, reparse_bytes_per_iter, NULL, 50ull, 1ull},
};

static int compare_double(const void * a, const void * b) {
  double x = *(const double *)a;
  double y = *(const double *)b;
  return (x > y) - (x < y);
}

int main(int argc, char ** argv) {
  int smoke = 0;
  const char * only = NULL; /* run the cases whose name begins with this */
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--only") == 0 && i + 1 < argc) {
      only = argv[++i];
    }
    else if (strcmp(argv[i], "--seeds") == 0 && i + 1 < argc) {
      seed_dir = argv[++i];
    }
    else if (strcmp(argv[i], "--smoke") == 0) {
      smoke = 1;
    }
    else {
      fprintf(stderr, "bench: unknown argument '%s'\n", argv[i]);
      return 2;
    }
  }

  /* Naming the library's version proves the harness linked the library it
   * claims to measure, and ties every figure to the build that produced it. */
  printf("lang-ecmascript %s, %s run\n", glecma_version_string(), smoke ? "smoke" : "full");

  int repeats = smoke ? 1 : REPEATS;
  int ran = 0;
  for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
    if (only && strncmp(cases[c].name, only, strlen(only)) != 0) {
      continue;
    }
    ran++;
    uint64_t n = smoke ? cases[c].smoke_iterations : cases[c].iterations;
    double ns[REPEATS];
    uint64_t sink = 0;
    for (int r = 0; r < repeats; r++) {
      sink ^= cases[c].run(n, &ns[r]);
    }
    qsort(ns, (size_t)repeats, sizeof(ns[0]), compare_double);
    double best = ns[0] / (double)n;
    double median = ns[repeats / 2] / (double)n;
    double spread = (ns[repeats - 1] - ns[0]) / ns[repeats / 2] * 100.0;
    if (!(best > 0.0)) {
      fprintf(stderr, "bench: %s measured no time; the clock is unusable\n", cases[c].name);
      return 1;
    }
    printf("%-24s best %14.2f ns/iter   median %14.2f ns/iter   spread %5.1f%%   (n=%llu, sink=%llx)\n",
        cases[c].name, best, median, spread, (unsigned long long)n, (unsigned long long)sink);
    if (cases[c].bytes_per_iter) {
      double bytes = (double)cases[c].bytes_per_iter();
      printf("  %-22s %.2f MiB/s at the best run   (%.0f bytes per iteration)\n", "",
          bytes / best * 1e9 / (1024.0 * 1024.0), bytes);
    }
    if (cases[c].tokens_per_iter) {
      double tokens = (double)cases[c].tokens_per_iter();
      printf("  %-22s %.2f Mtokens/s at the best run   (%.0f tokens per iteration, %.3f per byte)\n", "",
          tokens / best * 1e3, tokens, tokens / (double)cases[c].bytes_per_iter());
    }
  }
  if (ran == 0) {
    fprintf(stderr, "bench: no case matched\n");
    return 2;
  }
  return 0;
}

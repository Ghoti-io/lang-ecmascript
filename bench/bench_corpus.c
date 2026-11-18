/*
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * Copyright (C) 2026 Corey Pennycuff
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
 * The benchmark cases over the real-world corpus (tools/corpus/CORPUS.tsv,
 * fetched by tools/corpus/fetch.sh and never committed): the decoder, the
 * tokenizer, the parser with each builder and tree destruction, each over every
 * file of the corpus, each with its peak memory.
 *
 * Time is measured as bench.c measures it (several repeats of the whole
 * workload; the best, the median and the spread are reported, beside a
 * calibration case that touches no library code). Peak memory is measured two
 * ways. The library's own: every allocation a unit of work makes goes through a
 * metering allocator that records the most bytes held at once, deterministic and
 * the same on every machine of one word size, so it is the figure to compare; it
 * is reported for the file that held the most, and as bytes per byte of source
 * at the file with the worst ratio. The process's: before anything is loaded,
 * a fresh child process (where `fork` exists) for each case reads the largest
 * file of the corpus alone, then runs the case's work over it once, and
 * reports how far its resident-set high-water mark rose above what holding
 * that file's text had made it. That includes the allocator's own overhead and
 * the work's stack and depends on the machine; it is a fresh process because a
 * high-water mark never falls, so one process that had held the whole corpus
 * could not show the work of a case that needs less.
 *
 * Usage:
 *   bench_corpus                run every case
 *   bench_corpus --smoke        run every case once over the smallest file (what
 *                               `make test` does)
 *   bench_corpus --only PREFIX  the cases whose name begins with PREFIX
 *   bench_corpus --corpus DIR   the corpus directory (default third_party/corpus)
 *   bench_corpus --table FILE   the corpus table (default tools/corpus/CORPUS.tsv)
 *
 * It fails, naming the script that fetches it, when the corpus is not there.
 * No numeric budget is asserted: the figures are recorded in
 * documentation/benchmarks.md.
 */

/* clock_gettime and fork are POSIX, and -std=c17 hides them. */
#define _POSIX_C_SOURCE 200809L

#include <ghoti.io/lang-ecmascript/lang-ecmascript.h>

#include "lex/lexer.h"
#include "parse/parse_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if !defined(_WIN32)
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#define HAVE_FORK 1
#endif

#define REPEATS 5
#define MAX_FILES 64

static _Noreturn void fail(const char * what) {
  fprintf(stderr, "bench_corpus: %s\n", what);
  exit(1);
}

static double now_ns(void) {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    return 0.0;
  }
  return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

/* ---- A metering allocator ------------------------------------------------ */

typedef struct {
  GLECMA_Allocator allocator;
  size_t live;
  size_t peak;
} Meter;

typedef union {
  size_t size;
  max_align_t align;
} MeterHeader;

static void meter_add(Meter * m, size_t bytes) {
  m->live += bytes;
  if (m->live > m->peak) {
    m->peak = m->live;
  }
}

static void * meter_malloc(void * ctx, size_t size) {
  Meter * m = (Meter *)ctx;
  MeterHeader * h = (MeterHeader *)malloc(sizeof(MeterHeader) + size);
  if (h == NULL) {
    return NULL;
  }
  h->size = size;
  meter_add(m, size);
  return h + 1;
}

static void * meter_calloc(void * ctx, size_t count, size_t size) {
  if (size != 0 && count > SIZE_MAX / size) {
    return NULL;
  }
  void * p = meter_malloc(ctx, count * size);
  if (p != NULL) {
    memset(p, 0, count * size);
  }
  return p;
}

static void * meter_realloc(void * ctx, void * old, size_t size) {
  Meter * m = (Meter *)ctx;
  if (old == NULL) {
    return meter_malloc(ctx, size);
  }
  MeterHeader * h = (MeterHeader *)old - 1;
  const size_t before = h->size;
  MeterHeader * grown = (MeterHeader *)realloc(h, sizeof(MeterHeader) + size);
  if (grown == NULL) {
    return NULL;
  }
  grown->size = size;
  m->live -= before;
  meter_add(m, size);
  return grown + 1;
}

static void meter_free(void * ctx, void * p) {
  Meter * m = (Meter *)ctx;
  if (p == NULL) {
    return;
  }
  MeterHeader * h = (MeterHeader *)p - 1;
  m->live -= h->size;
  free(h);
}

static void meter_init(Meter * m) {
  m->allocator.ctx = m;
  m->allocator.malloc_fn = meter_malloc;
  m->allocator.calloc_fn = meter_calloc;
  m->allocator.realloc_fn = meter_realloc;
  m->allocator.free_fn = meter_free;
  m->live = 0;
  m->peak = 0;
}

/* ---- The corpus ------------------------------------------------------------ */

typedef struct {
  char name[160];   /* package/member, for the report */
  char * utf8;
  size_t bytes;
  uint16_t * utf16;
  size_t units;
  bool module;
} File;

static File files[MAX_FILES];
static size_t file_count = 0;
static const char * corpus_dir = "third_party/corpus";
static const char * table_path = "tools/corpus/CORPUS.tsv";
static bool smoke = false;

static char * read_file(const char * path, size_t * length) {
  FILE * f = fopen(path, "rb");
  if (f == NULL) {
    return NULL;
  }
  size_t capacity = 1u << 20, used = 0;
  char * buffer = (char *)malloc(capacity);
  while (buffer != NULL) {
    if (used == capacity) {
      capacity *= 2;
      char * grown = (char *)realloc(buffer, capacity);
      if (grown == NULL) {
        free(buffer);
        buffer = NULL;
        break;
      }
      buffer = grown;
    }
    size_t got = fread(buffer + used, 1, capacity - used, f);
    used += got;
    if (got == 0) {
      break;
    }
  }
  fclose(f);
  *length = used;
  return buffer;
}

static void to_utf16(File * f) {
  f->utf16 = (uint16_t *)malloc((f->bytes + 1) * sizeof(uint16_t));
  if (f->utf16 == NULL) {
    fail("out of memory");
  }
  size_t out = 0;
  for (size_t i = 0; i < f->bytes;) {
    const unsigned char c = (unsigned char)f->utf8[i];
    uint32_t cp;
    size_t n;
    if (c < 0x80) { cp = c; n = 1; }
    else if ((c & 0xE0) == 0xC0) { cp = c & 0x1Fu; n = 2; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0Fu; n = 3; }
    else { cp = c & 0x07u; n = 4; }
    if (i + n > f->bytes) {
      fail("a corpus file is not whole UTF-8");
    }
    for (size_t k = 1; k < n; k++) {
      cp = (cp << 6) | ((unsigned char)f->utf8[i + k] & 0x3Fu);
    }
    if (cp >= 0x10000u) {
      cp -= 0x10000u;
      f->utf16[out++] = (uint16_t)(0xD800u + (cp >> 10));
      f->utf16[out++] = (uint16_t)(0xDC00u + (cp & 0x3FFu));
    } else {
      f->utf16[out++] = (uint16_t)cp;
    }
    i += n;
  }
  f->units = out;
}

static void load_corpus(void) {
  if (file_count > 0) {
    return;
  }
  FILE * table = fopen(table_path, "r");
  if (table == NULL) {
    fprintf(stderr, "bench_corpus: cannot open %s (run from the library root, or pass --table)\n", table_path);
    exit(1);
  }
  char line[2048];
  while (fgets(line, sizeof line, table) != NULL) {
    if (line[0] == '#' || line[0] == '\n') {
      continue;
    }
    char * fields[10];
    size_t n = 0;
    for (char * c = line; n < 10;) {
      fields[n++] = c;
      char * tab = strchr(c, '\t');
      if (tab == NULL) {
        break;
      }
      *tab = '\0';
      c = tab + 1;
    }
    if (n < 6) {
      fail("a row of the corpus table has too few fields");
    }
    if (file_count == MAX_FILES) {
      fail("too many corpus files");
    }
    char directory[160];
    snprintf(directory, sizeof directory, "%s-%s", fields[0], fields[1]);
    for (char * c = directory; *c != '\0'; c++) {
      if (*c == '/') {
        *c = '_';
      }
    }
    char path[512];
    if (snprintf(path, sizeof path, "%s/%s/%s", corpus_dir, directory, fields[3]) >= (int)sizeof path) {
      fail("a corpus path is too long");
    }
    File * f = &files[file_count];
    f->utf8 = read_file(path, &f->bytes);
    if (f->utf8 == NULL || f->bytes == 0) {
      fprintf(stderr, "bench_corpus: cannot read %s: fetch the corpus with `make fetch-corpus` "
          "(tools/corpus/fetch.sh)\n", path);
      exit(1);
    }
    snprintf(f->name, sizeof f->name, "%s/%s", fields[0], fields[3]);
    f->module = strcmp(fields[5], "module") == 0;
    to_utf16(f);
    file_count++;
  }
  fclose(table);
  if (file_count == 0) {
    fail("the corpus table names no file");
  }
  if (smoke) {
    /* The smallest file only. */
    size_t smallest = 0;
    for (size_t i = 1; i < file_count; i++) {
      if (files[i].bytes < files[smallest].bytes) {
        smallest = i;
      }
    }
    files[0] = files[smallest];
    file_count = 1;
  }
}

/* ---- The work ----------------------------------------------------------------- */

typedef enum {
  W_DECODE8,
  W_DECODE16,
  W_TOKENS,
  W_PARSE,
  W_PARSE16,
  W_CHECKING,
  W_DESTROY,
} Work;

static bool count_token(void * context, const GLECMA_Token * token) {
  (void)token;
  ++*(uint64_t *)context;
  return true;
}

/* One file's unit of work through `allocator`; the time of the part the case
 * measures is added to *ns. Returns a value derived from the work, and sets
 * *held to the bytes the work still held when it was done (the peak of a unit
 * is read from the meter). */
static uint64_t work_one(Work w, const File * f, const GLECMA_Allocator * allocator, double * ns) {
  GLECMA_Source * source = NULL;
  GLECMA_Failure failure = GLECMA_FAILURE_INIT;
  uint64_t sink = 0;
  double start = now_ns();
  GLECMA_Result r = w == W_DECODE16 || w == W_PARSE16
      ? glecma_source_create_utf16(f->utf16, f->units, NULL, allocator, &failure, &source)
      : glecma_source_create_utf8(f->utf8, f->bytes, NULL, allocator, &failure, &source);
  double after_source = now_ns();
  if (r != GLECMA_OK) {
    fail("a corpus file was refused by the decoder");
  }
  if (w == W_DECODE8 || w == W_DECODE16) {
    *ns += after_source - start;
    sink += glecma_source_code_points(source) + glecma_source_lines(source);
    glecma_source_release(source);
    return sink;
  }
  if (w == W_TOKENS) {
    GLECMA_Lexer lexer;
    uint64_t tokens = 0;
    double t = now_ns();
    if (glecma_lexer_open(&lexer, source, NULL, 0, NULL) != GLECMA_OK ||
        glecma_lexer_run(&lexer, count_token, &tokens) != GLECMA_OK) {
      fail("a corpus file was refused by the tokenizer");
    }
    *ns += now_ns() - t;
    glecma_source_release(source);
    return tokens;
  }
  GLECMA_ParseOptions options = GLECMA_PARSE_OPTIONS_INIT;
  options.goal = f->module ? GLECMA_PARSE_MODULE : GLECMA_PARSE_SCRIPT;
  GLECMA_Tree * tree = NULL;
  double t = now_ns();
  r = glecma_parse_internal(source, &options, allocator, w == W_CHECKING ? GLECMA_PARSE_INTERNAL_CHECKING : 0u,
      &failure, &tree);
  double parsed = now_ns();
  if (r != GLECMA_OK) {
    fprintf(stderr, "bench_corpus: %s: %s at %zu\n", f->name, failure.message ? failure.message : "?",
        failure.location.offset);
    fail("a corpus file was refused by the parser");
  }
  sink += glecma_node_end(glecma_tree_root(tree));
  if (w == W_DESTROY) {
    double d = now_ns();
    glecma_tree_destroy(tree);
    *ns += now_ns() - d;
  } else {
    *ns += parsed - t;
    glecma_tree_destroy(tree);
  }
  glecma_source_release(source);
  return sink;
}

static uint64_t run_work(Work w, uint64_t iterations, double * elapsed) {
  uint64_t sink = 0;
  double ns = 0;
  for (uint64_t i = 0; i < iterations; i++) {
    for (size_t k = 0; k < file_count; k++) {
      sink += work_one(w, &files[k], NULL, &ns);
    }
  }
  *elapsed = ns;
  return sink;
}

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

typedef struct {
  const char * name;
  Work work;
  bool utf16;
  const char * what;
} Case;

static const Case cases[] = {
  {"corpus-decode-utf8", W_DECODE8, false, "source created from UTF-8 and released"},
  {"corpus-decode-utf16", W_DECODE16, true, "source created from UTF-16 and released"},
  {"corpus-tokenize-utf8", W_TOKENS, false, "every token of the source read"},
  {"corpus-parse-utf8", W_PARSE, false, "parsed to a tree (tree builder); the clock stops before the release"},
  {"corpus-parse-utf16", W_PARSE16, true, "the same, from UTF-16"},
  {"corpus-parse-checking-utf8", W_CHECKING, false, "parsed with the checking builder"},
  {"corpus-tree-destroy", W_DESTROY, false, "the tree released (the parse that made it is not in the clock)"},
};

static uint64_t corpus_bytes(bool utf16) {
  uint64_t total = 0;
  for (size_t k = 0; k < file_count; k++) {
    total += utf16 ? files[k].units * sizeof(uint16_t) : files[k].bytes;
  }
  return total;
}

static int compare_double(const void * a, const void * b) {
  double x = *(const double *)a, y = *(const double *)b;
  return (x > y) - (x < y);
}

/* The library's own peak: the largest, over the files, of the most bytes the
 * work on one file held at once; and the worst ratio to the file's size. */
static void report_peak(const Case * c) {
  size_t worst = 0, worst_file = 0;
  double worst_ratio = 0;
  size_t ratio_file = 0;
  for (size_t k = 0; k < file_count; k++) {
    Meter m;
    meter_init(&m);
    double ns = 0;
    (void)work_one(c->work, &files[k], &m.allocator, &ns);
    if (m.live != 0) {
      fail("the work left memory held");
    }
    const double size = (double)(c->utf16 ? files[k].units * sizeof(uint16_t) : files[k].bytes);
    if (m.peak > worst) {
      worst = m.peak;
      worst_file = k;
    }
    if ((double)m.peak / size > worst_ratio) {
      worst_ratio = (double)m.peak / size;
      ratio_file = k;
    }
  }
  printf("  %-24s peak memory (the library's, metered) %zu bytes (%.1f MiB) at %s (%zu source bytes);"
         " worst %.1f bytes per source byte at %s\n", "", worst, (double)worst / (1024.0 * 1024.0),
      files[worst_file].name, files[worst_file].bytes, worst_ratio, files[ratio_file].name);
}

#ifdef HAVE_FORK
static long max_rss_kib(void) {
  struct rusage usage;
  if (getrusage(RUSAGE_SELF, &usage) != 0) {
    return -1;
  }
  return usage.ru_maxrss;
}

/* The process's own, in a fresh child (see the file comment). */
static void report_rss(const Case * c) {
  fflush(stdout);
  pid_t pid = fork();
  if (pid < 0) {
    printf("  %-28s peak resident size: not measured (fork failed)\n", c->name);
    return;
  }
  if (pid == 0) {
    /* Only this file is loaded: the others are read and dropped one by one, which leaves a high-water
     * mark of the whole corpus, so this reads the table and then the one file it wants. */
    FILE * table = fopen(table_path, "r");
    if (table == NULL) {
      _exit(1);
    }
    char line[2048];
    size_t best_bytes = 0;
    char best_path[512] = "";
    char best_name[160] = "";
    bool best_module = false;
    while (fgets(line, sizeof line, table) != NULL) {
      if (line[0] == '#' || line[0] == '\n') {
        continue;
      }
      char * fields[10];
      size_t n = 0;
      for (char * p = line; n < 10;) {
        fields[n++] = p;
        char * tab = strchr(p, '\t');
        if (tab == NULL) {
          break;
        }
        *tab = '\0';
        p = tab + 1;
      }
      char directory[160];
      snprintf(directory, sizeof directory, "%s-%s", fields[0], fields[1]);
      for (char * p = directory; *p != '\0'; p++) {
        if (*p == '/') {
          *p = '_';
        }
      }
      char path[512];
      snprintf(path, sizeof path, "%s/%s/%s", corpus_dir, directory, fields[3]);
      FILE * f = fopen(path, "rb");
      if (f == NULL) {
        continue;
      }
      fseek(f, 0, SEEK_END);
      long size = ftell(f);
      fclose(f);
      if (size > 0 && (size_t)size > best_bytes) {
        best_bytes = (size_t)size;
        snprintf(best_path, sizeof best_path, "%s", path);
        snprintf(best_name, sizeof best_name, "%s/%s", fields[0], fields[3]);
        best_module = strcmp(fields[5], "module") == 0;
      }
    }
    fclose(table);
    File f;
    memset(&f, 0, sizeof f);
    f.utf8 = read_file(best_path, &f.bytes);
    if (f.utf8 == NULL) {
      _exit(1);
    }
    snprintf(f.name, sizeof f.name, "%s", best_name);
    f.module = best_module;
    if (c->utf16) {
      to_utf16(&f);
    }
    const long before = max_rss_kib();
    double ns = 0;
    const uint64_t sink = work_one(c->work, &f, NULL, &ns);
    const long after = max_rss_kib();
    printf("  %-28s peak resident size %ld KiB above the %.1f MiB of %s it holds (high-water mark %ld KiB, from %ld KiB)%s\n",
        c->name, after - before, (double)(c->utf16 ? f.units * 2 : f.bytes) / (1024.0 * 1024.0), f.name, after, before,
        sink == 1 ? "." : "");
    fflush(stdout);
    _exit(0);
  }
  int status = 0;
  waitpid(pid, &status, 0);
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    printf("  %-28s peak resident size: the measuring process failed\n", c->name);
  }
}
#else
static void report_rss(const Case * c) {
  printf("  %-28s peak resident size: not measured on this platform\n", c->name);
}
#endif

int main(int argc, char ** argv) {
  const char * only = NULL;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--only") == 0 && i + 1 < argc) {
      only = argv[++i];
    } else if (strcmp(argv[i], "--corpus") == 0 && i + 1 < argc) {
      corpus_dir = argv[++i];
    } else if (strcmp(argv[i], "--table") == 0 && i + 1 < argc) {
      table_path = argv[++i];
    } else if (strcmp(argv[i], "--smoke") == 0) {
      smoke = true;
    } else {
      fprintf(stderr, "bench_corpus: unknown argument '%s'\n", argv[i]);
      return 2;
    }
  }
  /* The process measurements first, in children of a process that has loaded nothing. */
  printf("lang-ecmascript %s, process peak memory, a fresh process per case over the largest file of the corpus\n",
      glecma_version_string());
  for (size_t c = 0; c < sizeof cases / sizeof cases[0]; c++) {
    if (only == NULL || strncmp(cases[c].name, only, strlen(only)) == 0) {
      report_rss(&cases[c]);
    }
  }
  load_corpus();
  printf("lang-ecmascript %s, %s run over %zu corpus file%s (%.2f MiB of UTF-8)\n", glecma_version_string(),
      smoke ? "smoke" : "full", file_count, file_count == 1 ? "" : "s", (double)corpus_bytes(false) / (1024.0 * 1024.0));
  const int repeats = smoke ? 1 : REPEATS;

  /* The calibration, taken in this run, beside the figures. */
  {
    const uint64_t n = smoke ? 1000000ull : 200000000ull;
    double ns[REPEATS];
    uint64_t sink = 0;
    for (int r = 0; r < repeats; r++) {
      sink ^= calibration_run(n, &ns[r]);
    }
    qsort(ns, (size_t)repeats, sizeof ns[0], compare_double);
    printf("%-28s best %14.2f ns/iter   median %14.2f ns/iter   spread %5.1f%%   (n=%llu, sink=%llx)\n",
        "calibration-xorshift", ns[0] / (double)n, ns[repeats / 2] / (double)n,
        (ns[repeats - 1] - ns[0]) / ns[repeats / 2] * 100.0, (unsigned long long)n, (unsigned long long)sink);
  }

  int ran = 0;
  for (size_t c = 0; c < sizeof cases / sizeof cases[0]; c++) {
    if (only != NULL && strncmp(cases[c].name, only, strlen(only)) != 0) {
      continue;
    }
    ran++;
    const uint64_t n = 1;
    double ns[REPEATS];
    uint64_t sink = 0;
    for (int r = 0; r < repeats; r++) {
      sink ^= run_work(cases[c].work, n, &ns[r]);
    }
    qsort(ns, (size_t)repeats, sizeof ns[0], compare_double);
    const double best = ns[0] / (double)n;
    const double median = ns[repeats / 2] / (double)n;
    const double spread = (ns[repeats - 1] - ns[0]) / ns[repeats / 2] * 100.0;
    if (!(best > 0.0)) {
      fprintf(stderr, "bench_corpus: %s measured no time; the clock is unusable\n", cases[c].name);
      return 1;
    }
    const double bytes = (double)corpus_bytes(cases[c].utf16);
    printf("%-28s best %14.2f ns/iter   median %14.2f ns/iter   spread %5.1f%%   (%s; sink=%llx)\n", cases[c].name,
        best, median, spread, cases[c].what, (unsigned long long)sink);
    printf("  %-24s %.2f MiB/s at the best run   (%.0f bytes per iteration, %.2f ns per byte)\n", "",
        bytes / best * 1e9 / (1024.0 * 1024.0), bytes, best / bytes);
    report_peak(&cases[c]);
  }
  if (ran == 0) {
    fprintf(stderr, "bench_corpus: no case matched\n");
    return 2;
  }
  return 0;
}

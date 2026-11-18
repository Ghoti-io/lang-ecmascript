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

// Hostile input: what the cost of a parse is as the input grows, and what a
// parse does when memory runs out. Doubling an input must double the time and
// the memory, within the factor stated below (never a power of the size), the
// depth of a nest must not change the cost per level, a parse that is refused
// for a limit must hold memory in proportion to the limit and not to the input,
// every allocation of every goal failing in turn must be an out-of-memory
// result with nothing leaked, and parses on separate threads of every goal
// must agree (meaningful under TSan, which `make test-tsan` runs).

#include <ghoti.io/lang-ecmascript/lang-ecmascript.h>

#include <gtest/gtest.h>

#include <pthread.h>
#include <time.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "../parse_test_support.h"
#include "../test_deadline.h"
#include "../test_helpers.h"

namespace {

// ---- The factor the sweeps stay linear within -------------------------------------------

// The cost per unit of input, at the largest size of a sweep divided by the
// cost per unit at its smallest, must not pass these. 1.0 would be exactly
// linear; a quadratic cost grows by the ratio of the largest size to the
// smallest (64 here), so these are two orders of magnitude from the failure
// they are there to see, and one order from what a quiet machine measures
// (documentation/design.md, "Hostile input", gives the figures taken). Time
// is the least of several runs, and is measured with a deadline over the
// whole sweep, so that a cost that does not finish fails and does not hang.
constexpr double kTimeFactor = 3.0;
// Memory is counted exactly (every allocation the library makes, through a
// metering allocator), so the bound is on the library's arenas, which double
// as they grow: a table that has just doubled holds up to twice what it
// needs, and a sweep that lands on one holds that more per unit.
constexpr double kMemoryFactor = 2.5;
// The sizes of a sweep: the smallest, doubled this many times.
constexpr int kDoublings = 5;

double Now() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1e-9;
}

// Valgrind and ThreadSanitizer make a run much slower, and so does an emulator;
// the sweeps then use smaller sizes (the cost per byte is a ratio, so it still
// says what it says). `make test-valgrind` and `make test-tsan` set
// GLECMA_TEST_SLOW, a run under Valgrind that did not is noticed by its
// preload, and a cross gate under qemu sets it.
bool Slow() {
  const char * preload = std::getenv("LD_PRELOAD");
  if (preload != nullptr && (std::strstr(preload, "vgpreload") != nullptr || std::strstr(preload, "valgrind") != nullptr)) {
    return true;
  }
  return std::getenv("GLECMA_TEST_SLOW") != nullptr;
}

bool Verbose() { return std::getenv("GLECMA_TEST_VERBOSE") != nullptr; }

struct Measure {
  size_t n = 0;
  size_t bytes = 0;
  double seconds = 0;
  size_t peak = 0;
  size_t calls = 0;
  GLECMA_Result result = GLECMA_OK;
};

enum class What { Parse, DecodeUtf8, DecodeUtf16, Stream };

struct Family {
  const char * name;
  size_t base;  // n at the smallest size
  std::function<std::string(size_t)> make;
  uint32_t goal = GLECMA_PARSE_SCRIPT;
  What what = What::Parse;
};

struct OneByteReader {
  const std::string * text;
  size_t at;
};

GLECMA_Result ReadOneByte(void * context, void * buffer, size_t capacity, size_t * out_read) {
  auto * reader = static_cast<OneByteReader *>(context);
  size_t n = reader->at < reader->text->size() && capacity > 0 ? 1 : 0;
  if (n != 0) {
    std::memcpy(buffer, reader->text->data() + reader->at, 1);
    reader->at++;
  }
  *out_read = n;
  return GLECMA_OK;
}

// One run of the work: returns false when the result was not what a valid
// input gives.
Measure Run(const Family & family, size_t n, int repeats) {
  const std::string text = family.make(n);
  Measure m;
  m.n = n;
  m.bytes = text.size();
  m.seconds = 1e300;
  std::vector<uint16_t> wide;
  if (family.what == What::DecodeUtf16) {
    for (unsigned char c : text) {
      wide.push_back(c);  // the families are ASCII, and the UTF-16 decoder is measured on it
    }
  }
  for (int r = 0; r < repeats; r++) {
    glecma_test::MeteringAllocator meter;
    GLECMA_Source * source = nullptr;
    GLECMA_Failure failure = GLECMA_FAILURE_INIT;
    double begin = 0, end = 0;
    GLECMA_Result result = GLECMA_OK;
    if (family.what == What::DecodeUtf8) {
      begin = Now();
      result = glecma_source_create_utf8(text.data(), text.size(), nullptr, meter.get(), &failure, &source);
      end = Now();
    } else if (family.what == What::DecodeUtf16) {
      begin = Now();
      result = glecma_source_create_utf16(wide.data(), wide.size(), nullptr, meter.get(), &failure, &source);
      end = Now();
    } else if (family.what == What::Stream) {
      OneByteReader reader{&text, 0};
      GLECMA_Stream * stream = nullptr;
      EXPECT_EQ(glecma_stream_create_callback(&ReadOneByte, nullptr, &reader, meter.get(), &stream), GLECMA_OK);
      begin = Now();
      result = glecma_source_create_stream(stream, GLECMA_ENCODING_UTF8, nullptr, meter.get(), &failure, &source);
      end = Now();
      glecma_stream_destroy(stream);
    } else {
      EXPECT_EQ(glecma_source_create_utf8(text.data(), text.size(), nullptr, meter.get(), &failure, &source), GLECMA_OK);
      // A sanitizer's frames are twice as large, and the families that nest at the cap would meet the
      // default grant of stack before the cap; the grant here is what the main thread has to give.
      GLECMA_Limits limits = GLECMA_LIMITS_INIT;
      limits.native_stack_bytes = uint64_t{6} << 20;
      GLECMA_ParseOptions options = GLECMA_PARSE_OPTIONS_INIT;
      options.goal = family.goal;
      options.limits = &limits;
      GLECMA_Tree * tree = nullptr;
      begin = Now();
      result = glecma_parse(source, &options, meter.get(), &failure, &tree);
      end = Now();
      glecma_tree_destroy(tree);
    }
    m.result = result;
    if (result != GLECMA_OK) {
      ADD_FAILURE() << family.name << " n=" << n << ": " << (failure.message != nullptr ? failure.message : "?")
                    << " at " << failure.location.offset;
      glecma_source_release(source);
      return m;
    }
    glecma_source_release(source);
    EXPECT_EQ(meter.live_bytes(), 0u) << family.name;
    m.seconds = std::min(m.seconds, end - begin);
    m.peak = meter.peak_bytes();
    m.calls = meter.calls();
  }
  return m;
}

// A sweep of one family: the sizes, the cost per unit of the text at each, and
// the factor of the largest over the smallest.
void Sweep(const Family & family, double * worst_time, double * worst_memory) {
  const int doublings = Slow() ? kDoublings - 2 : kDoublings;
  std::vector<Measure> runs;
  for (int k = 0; k <= doublings; k++) {
    const size_t n = std::max<size_t>(family.base / (Slow() ? 8 : 2), 1) << k;
    // The smaller runs repeat more, since they are the ones a timer disturbs.
    runs.push_back(Run(family, n, k < 3 ? 5 : 3));
    if (runs.back().result != GLECMA_OK) {
      return;
    }
  }
  auto per_byte_time = [&](const Measure & m) { return m.seconds / static_cast<double>(m.bytes); };
  auto per_byte_memory = [&](const Measure & m) { return static_cast<double>(m.peak) / static_cast<double>(m.bytes); };
  // The least per-unit cost at the three smallest sizes is the baseline: the
  // smallest alone carries the fixed cost of the tables a parse starts with.
  double base_time = 1e300, base_memory = 1e300;
  for (int k = 0; k < 3; k++) {
    base_time = std::min(base_time, per_byte_time(runs[static_cast<size_t>(k)]));
    base_memory = std::min(base_memory, per_byte_memory(runs[static_cast<size_t>(k)]));
  }
  double time_factor = 0, memory_factor = 0;
  for (size_t k = 3; k < runs.size(); k++) {
    time_factor = std::max(time_factor, per_byte_time(runs[k]) / base_time);
    memory_factor = std::max(memory_factor, per_byte_memory(runs[k]) / base_memory);
  }
  if (Verbose()) {
    std::printf("%-26s %8zu..%-9zu bytes  time x%.2f  memory x%.2f  (%.1f ns/byte, %.1f bytes/byte at the smallest)\n",
        family.name, runs.front().bytes, runs.back().bytes, time_factor, memory_factor,
        base_time * 1e9, base_memory);
  }
  *worst_time = std::max(*worst_time, time_factor);
  *worst_memory = std::max(*worst_memory, memory_factor);
  EXPECT_LE(time_factor, kTimeFactor) << family.name << ": the cost per byte grew by this factor as the input grew "
                                      << (1 << doublings) << " times";
  EXPECT_LE(memory_factor, kMemoryFactor) << family.name << ": the memory per byte grew by this factor";
}

std::string Repeat(const std::string & unit, size_t n) {
  std::string s;
  s.reserve(unit.size() * n);
  for (size_t i = 0; i < n; i++) {
    s += unit;
  }
  return s;
}

// `unit` with each `@` replaced by the number i, repeated for i below n.
std::string Numbered(const std::string & unit, size_t n) {
  std::string s;
  for (size_t i = 0; i < n; i++) {
    for (char c : unit) {
      if (c == '@') {
        s += std::to_string(i);
      } else {
        s += c;
      }
    }
  }
  return s;
}

// The first and last of a nest that is `depth` deep around `middle`.
std::string Nest(const std::string & open, const std::string & middle, const std::string & close, size_t depth) {
  std::string s;
  for (size_t i = 0; i < depth; i++) {
    s += open;
  }
  s += middle;
  for (size_t i = 0; i < depth; i++) {
    s += close;
  }
  return s;
}

std::vector<Family> Families() {
  std::vector<Family> f;
  // Many of one thing.
  f.push_back({"statements", 4096, [](size_t n) { return Repeat("x = 1;\n", n); }});
  f.push_back({"distinct-var-names", 2048, [](size_t n) { return Numbered("var v@;\n", n); }});
  f.push_back({"distinct-let-names", 2048, [](size_t n) { return Numbered("let v@;\n", n); }});
  f.push_back({"one-name-redeclared", 4096, [](size_t n) { return Repeat("var a;\n", n); }});
  f.push_back({"distinct-references", 2048, [](size_t n) { return Numbered("r@;\n", n); }});
  f.push_back({"functions", 1024, [](size_t n) { return Numbered("function f@(a, b) { return a + b; }\n", n); }});
  f.push_back({"duplicate-parameters", 2048, [](size_t n) {
    std::string s = "function f(";
    for (size_t i = 0; i < n; i++) {
      s += i == 0 ? "a" : ", a";
    }
    return s + ") {}";
  }});
  f.push_back({"many-parameters", 2048, [](size_t n) {
    std::string s = "function f(";
    for (size_t i = 0; i < n; i++) {
      s += (i == 0 ? "p" : ", p") + std::to_string(i);
    }
    return s + ") {}";
  }});
  f.push_back({"labels-and-jumps", 1024, [](size_t n) { return Numbered("l@: for (;;) { break l@; continue l@; }\n", n); }});
  f.push_back({"switch-cases", 2048, [](size_t n) {
    std::string s = "switch (x) {\n";
    for (size_t i = 0; i < n; i++) {
      s += "case " + std::to_string(i) + ": f(); break;\n";
    }
    return s + "}";
  }});
  // One long thing.
  f.push_back({"binary-chain", 8192, [](size_t n) { return "x = a" + Repeat(" + a", n) + ";"; }});
  f.push_back({"member-chain", 8192, [](size_t n) { return "x = a" + Repeat(".b", n) + ";"; }});
  f.push_back({"call-chain", 8192, [](size_t n) { return "x = f" + Repeat("()", n) + ";"; }});
  f.push_back({"optional-chain", 8192, [](size_t n) { return "x = a" + Repeat("?.b", n) + ";"; }});
  f.push_back({"comma-sequence", 8192, [](size_t n) { return "x = (a" + Repeat(", a", n) + ");"; }});
  f.push_back({"conditional-chains", 2048, [](size_t n) { return Repeat("y = a ? b : c ? d : e;\n", n); }});
  f.push_back({"array-literal", 8192, [](size_t n) { return "x = [" + Repeat("1, ", n) + "];"; }});
  f.push_back({"array-holes", 8192, [](size_t n) { return "x = [" + Repeat(",", n) + "];"; }});
  f.push_back({"object-distinct-keys", 2048, [](size_t n) { return "x = {" + Numbered("k@: 1, ", n) + "};"; }});
  f.push_back({"object-repeated-key", 2048, [](size_t n) { return "x = {" + Repeat("k: 1, ", n) + "};"; }});
  f.push_back({"object-shorthand", 2048, [](size_t n) { return "x = {" + Numbered("s@, ", n) + "};"; }});
  f.push_back({"spread-arguments", 4096, [](size_t n) { return "f(" + Repeat("...a, ", n) + ");"; }});
  // The cover grammars, where a parenthesis is not known to be an arrow function's head until its end.
  f.push_back({"arrow-parameter-list", 2048, [](size_t n) {
    std::string s = "x = (";
    for (size_t i = 0; i < n; i++) {
      s += (i == 0 ? "a" : ", a") + std::to_string(i);
    }
    return s + ") => 1;";
  }});
  f.push_back({"parenthesized-list-not-an-arrow", 2048, [](size_t n) {
    std::string s = "x = (";
    for (size_t i = 0; i < n; i++) {
      s += (i == 0 ? "a" : ", a") + std::to_string(i);
    }
    return s + ");";
  }});
  f.push_back({"arrow-candidates-in-sequence", 2048, [](size_t n) { return Repeat("(a, b = 1, ...c) => a;\n(a, b);\nasync (a, b);\nasync (a, b) => a;\n", n); }});
  f.push_back({"parenthesized-pattern-candidates", 2048, [](size_t n) { return Repeat("([a, {b = 1}], c = [d]) => a;\n[(a), b] = c;\n({e = 1} = f);\n", n); }});
  f.push_back({"destructuring-pattern", 2048, [](size_t n) { return "[" + Numbered("p@, ", n) + "] = x;"; }});
  f.push_back({"destructuring-object-pattern", 2048, [](size_t n) { return "({" + Numbered("p@, ", n) + "} = x);"; }});
  f.push_back({"nested-arrow-bodies-in-sequence", 1024, [](size_t n) { return Repeat("a => b => c => d => e;\n", n); }});
  // Templates, strings, comments, regular expressions.
  f.push_back({"template-substitutions", 4096, [](size_t n) { return "x = `" + Repeat("${a}", n) + "`;"; }});
  f.push_back({"template-text", 65536, [](size_t n) { return "x = `" + Repeat("t", n) + "`;"; }});
  f.push_back({"tagged-templates", 2048, [](size_t n) { return Repeat("tag`a${b}c${d}`;\n", n); }});
  f.push_back({"template-in-template", 2048, [](size_t n) { return Repeat("x = `a${`b${`c`}`}`;\n", n); }});
  f.push_back({"string-literal", 65536, [](size_t n) { return "x = '" + Repeat("s", n) + "';"; }});
  f.push_back({"string-escapes", 2048, [](size_t n) { return "x = '" + Repeat("\\n\\x41\\u0042\\u{43}", n) + "';"; }});
  f.push_back({"line-comment", 65536, [](size_t n) { return "// " + Repeat("c", n) + "\nx;"; }});
  f.push_back({"block-comment", 65536, [](size_t n) { return "/* " + Repeat("c\n", n) + "*/ x;"; }});
  f.push_back({"many-comments", 4096, [](size_t n) { return Repeat("/* c */ x; // d\n", n); }});
  f.push_back({"long-identifier", 65536, [](size_t n) { return Repeat("i", n) + ";"; }});
  f.push_back({"escaped-identifier", 8192, [](size_t n) { return Repeat("\\u0069", n) + ";"; }});
  f.push_back({"regexp-literals", 2048, [](size_t n) { return Repeat("x = /a(b|c)*[d-f]+\\d{2,3}/gi;\n", n); }});
  f.push_back({"long-regexp-literal", 16384, [](size_t n) { return "x = /" + Repeat("a", n) + "/;"; }});
  f.push_back({"regexp-alternation", 8192, [](size_t n) { return "x = /a" + Repeat("|a", n) + "/;"; }});
  f.push_back({"regexp-groups", 4096, [](size_t n) { return "x = /" + Repeat("(a)", n) + "/;"; }});
  f.push_back({"numeric-literals", 4096, [](size_t n) { return Repeat("x = 0x1F + 1_000.5e3 + 0b101n + 077;\n", n); }});
  // Lines, and the source's own work.
  f.push_back({"lines-lf", 16384, [](size_t n) { return Repeat("\n", n) + "x;"; }});
  f.push_back({"lines-crlf", 16384, [](size_t n) { return Repeat("\r\n", n) + "x;"; }});
  f.push_back({"decode-utf8", 65536, [](size_t n) { return Repeat("a", n); }, GLECMA_PARSE_SCRIPT, What::DecodeUtf8});
  f.push_back({"decode-utf16", 65536, [](size_t n) { return Repeat("a", n); }, GLECMA_PARSE_SCRIPT, What::DecodeUtf16});
  f.push_back({"decode-from-one-byte-reads", 16384, [](size_t n) { return Repeat("a", n); }, GLECMA_PARSE_SCRIPT, What::Stream});
  // Classes, private names and modules.
  f.push_back({"class-members", 1024, [](size_t n) { return "class C {" + Numbered("m@() {} static s@ = 1; #p@ = 1; get g@() { return this.#p@; }\n", n) + "}"; }});
  f.push_back({"private-name-references", 1024, [](size_t n) { return "class C { #p; m() {" + Repeat("this.#p; #p in this;", n) + "} }"; }});
  f.push_back({"classes-in-sequence", 512, [](size_t n) { return Numbered("class C@ extends B { #p = 1; static { s; } m(a = 1) { return super.m(a) + this.#p; } }\n", n); }});
  f.push_back({"module-exports", 1024, [](size_t n) { return Numbered("export const e@ = 1;\n", n); }, GLECMA_PARSE_MODULE});
  f.push_back({"module-export-list", 1024, [](size_t n) { return "const a = 1;\nexport {" + Numbered("a as x@, ", n) + "};"; }, GLECMA_PARSE_MODULE});
  f.push_back({"module-imports", 1024, [](size_t n) { return Numbered("import i@ from 'm@';\n", n) + "export {};"; }, GLECMA_PARSE_MODULE});
  f.push_back({"module-import-names", 1024, [](size_t n) { return "import {" + Numbered("n@ as l@, ", n) + "} from 'm';"; }, GLECMA_PARSE_MODULE});
  f.push_back({"module-top-level-await", 2048, [](size_t n) { return Repeat("await x;\n", n); }, GLECMA_PARSE_MODULE});
  // Many nests, each at (near) the nesting cap.
  f.push_back({"parenthesis-nests-at-the-cap", 16, [](size_t n) { return Repeat(Nest("(", "x", ")", 240) + ";\n", n); }});
  f.push_back({"array-nests-at-the-cap", 16, [](size_t n) { return Repeat(Nest("[", "x", "]", 240) + ";\n", n); }});
  f.push_back({"function-nests-at-the-cap", 16, [](size_t n) { return Repeat(Nest("function f() {", "", "}", 200) + "\n", n); }});
  f.push_back({"arrow-nests-at-the-cap", 16, [](size_t n) { return Repeat(Nest("(a) => ", "x", "", 240) + ";\n", n); }});
  f.push_back({"arrow-parameter-nests-at-the-cap", 16, [](size_t n) { return Repeat(Nest("(a = ", "x", ") => 0", 120) + ";\n", n); }});
  f.push_back({"template-nests-at-the-cap", 16, [](size_t n) { return Repeat(Nest("`${", "x", "}`", 100) + ";\n", n); }});
  f.push_back({"call-nests-at-the-cap", 16, [](size_t n) { return Repeat(Nest("f(", "x", ")", 240) + ";\n", n); }});
  f.push_back({"conditional-nests-at-the-cap", 16, [](size_t n) { return Repeat(Nest("a ? b : ", "x", "", 240) + ";\n", n); }});
  f.push_back({"pattern-nests-at-the-cap", 16, [](size_t n) { return Repeat(Nest("[", "x", "]", 240) + " = y;\n", n); }});
  f.push_back({"block-nests-at-the-cap", 16, [](size_t n) { return Repeat(Nest("{", "", "}", 240) + "\n", n); }});
  return f;
}

}  // namespace

TEST(Hostile, DoublingTheInputKeepsTimeAndMemoryLinear) {
  glecma_test::Deadline deadline(std::chrono::seconds(Slow() ? 1500 : 300), "the doubling sweeps did not finish");
  double worst_time = 0, worst_memory = 0;
  const std::vector<Family> families = Families();
  for (const Family & family : families) {
    Sweep(family, &worst_time, &worst_memory);
  }
  if (Verbose()) {
    std::printf("worst over %zu families: time x%.2f (limit %.1f), memory x%.2f (limit %.1f)\n", families.size(),
        worst_time, kTimeFactor, worst_memory, kMemoryFactor);
  }
}

namespace {

// The cost per byte of a nest as its depth grows, the total levels being the
// same in all: a cost that depends on the depth of a nest, and not only on how
// many there are, is what changes. A nest of constructs the grammar cannot
// classify until its end (a parenthesis that turns out to be arrow parameters,
// an array literal that turns out to be a pattern) is read as one thing and
// read again as the other (clause 13.15.5, and the builder's rewind), so its
// cost per byte grows in proportion to its depth: quadratic in the depth of one
// nest, and bounded by the nesting cap. Every other nest costs the same per
// byte at any depth. The sweep says which is which, and a cost that grows
// faster than the depth, or any growth at all in a nest that is not one of the
// two, fails it.
struct Shape {
  const char * name;
  bool cover;  // read twice (arrow parameters or a pattern): cost per byte may grow with the depth
  std::function<std::string(size_t depth, size_t repeats)> make;
};

}  // namespace

TEST(Hostile, DoublingTheDepthWithinTheCapKeepsTheCostPerLevelFlatOrLinearForTheCoverGrammars) {
  glecma_test::Deadline deadline(std::chrono::seconds(Slow() ? 600 : 120), "the depth sweeps did not finish");
  const size_t levels = Slow() ? 2048 : 16384;
  const Shape shapes[] = {
    {"parenthesis", false, [](size_t d, size_t r) { return Repeat(Nest("(", "x", ")", d) + ";\n", r); }},
    {"arrow-candidate", true, [](size_t d, size_t r) { return Repeat(Nest("(a, b = ", "x", ", c) => 0", d) + ";\n", r); }},
    {"arrow-body", false, [](size_t d, size_t r) { return Repeat(Nest("a => ", "x", "", d) + ";\n", r); }},
    {"async-arrow-candidate", true, [](size_t d, size_t r) { return Repeat(Nest("async (a = ", "x", ") => 0", d) + ";\n", r); }},
    {"array", false, [](size_t d, size_t r) { return Repeat(Nest("[", "x", "]", d) + ";\n", r); }},
    {"array-pattern", true, [](size_t d, size_t r) { return Repeat(Nest("[", "x", "]", d) + " = y;\n", r); }},
    {"object-pattern", true, [](size_t d, size_t r) { return Repeat(Nest("[{a: ", "x", "}]", d) + " = y;\n", r); }},
    {"call", false, [](size_t d, size_t r) { return Repeat(Nest("f(", "x", ")", d) + ";\n", r); }},
    {"template", false, [](size_t d, size_t r) { return Repeat(Nest("`${", "x", "}`", d) + ";\n", r); }},
    {"function", false, [](size_t d, size_t r) { return Repeat("{" + Nest("function f() {", "", "}", d) + "}\n", r); }},
    {"class", false, [](size_t d, size_t r) { return Repeat("{" + Nest("class A { m() {", "", "}}", d / 2) + "}\n", r); }},
    {"block-scopes", false, [](size_t d, size_t r) { return Repeat("{" + Nest("{ let a; ", "", "}", d) + "}\n", r); }},
  };
  for (const Shape & shape : shapes) {
    // The deepest nest the default limits accept, found by bisection.
    size_t low = 6, high = 1024;
    while (low + 1 < high) {
      const size_t middle = (low + high) / 2;
      const std::string text = shape.make(middle, 1);
      GLECMA_Source * source = nullptr;
      ASSERT_EQ(glecma_source_create_utf8(text.data(), text.size(), nullptr, nullptr, nullptr, &source), GLECMA_OK);
      GLECMA_Tree * tree = nullptr;
      GLECMA_Result r = glecma_parse(source, nullptr, nullptr, nullptr, &tree);
      (r == GLECMA_OK ? low : high) = middle;
      glecma_tree_destroy(tree);
      glecma_source_release(source);
    }
    std::vector<size_t> depths = {6, 12, 25, 50};
    depths.push_back(low);
    std::vector<double> per_byte;
    for (size_t depth : depths) {
      const std::string text = shape.make(depth, std::max<size_t>(levels / depth, 1));
      double best = 1e300;
      for (int r = 0; r < 3; r++) {
        GLECMA_Source * source = nullptr;
        ASSERT_EQ(glecma_source_create_utf8(text.data(), text.size(), nullptr, nullptr, nullptr, &source), GLECMA_OK);
        GLECMA_Failure failure = GLECMA_FAILURE_INIT;
        GLECMA_Tree * tree = nullptr;
        const double begin = Now();
        GLECMA_Result result = glecma_parse(source, nullptr, nullptr, &failure, &tree);
        const double end = Now();
        ASSERT_EQ(result, GLECMA_OK) << shape.name << " at depth " << depth << ": "
                                     << (failure.message != nullptr ? failure.message : "?") << " at "
                                     << failure.location.offset;
        glecma_tree_destroy(tree);
        glecma_source_release(source);
        best = std::min(best, end - begin);
      }
      per_byte.push_back(best / static_cast<double>(text.size()));
    }
    const double growth = per_byte.back() / per_byte.front();
    const double depth_growth = static_cast<double>(depths.back()) / static_cast<double>(depths.front());
    // The cost per byte divided by the depth, at the deepest over the shallowest: 1 when the cost per
    // byte is proportional to the depth, and more when it is worse than that.
    const double beyond_linear = growth / depth_growth;
    if (Verbose()) {
      std::printf("depth %-22s deepest %3zu: cost per byte x%.2f of the depth-6 cost (depth x%.1f)%s\n", shape.name,
          depths.back(), growth, depth_growth, shape.cover ? "  [cover grammar]" : "");
    }
    if (shape.cover) {
      EXPECT_LE(beyond_linear, kTimeFactor) << shape.name << ": the cost per byte grew faster than the depth";
    } else {
      EXPECT_LE(growth, kTimeFactor) << shape.name << ": the cost per byte grew with the depth of the nest";
    }
  }
}

// ---- Memory held in proportion to the limit, not to the input -----------------------------

TEST(Hostile, AParseRefusedForALimitHoldsMemoryInProportionToTheLimitAndNotTheInput) {
  glecma_test::Deadline deadline(std::chrono::seconds(Slow() ? 600 : 120), "the limit sweeps did not finish");
  struct Case {
    const char * name;
    const char * limit;
    std::function<void(GLECMA_Limits *, uint64_t)> set;
    std::function<std::string(size_t)> make;  // an input that a cap of `cap` refuses, of a size that grows
  };
  const Case cases[] = {
    {"nodes", "nodes", [](GLECMA_Limits * l, uint64_t cap) { l->nodes = cap; },
        [](size_t n) { return Repeat("a;", n); }},
    {"tokens", "tokens", [](GLECMA_Limits * l, uint64_t cap) { l->tokens = cap; },
        [](size_t n) { return Repeat("a;", n); }},
    {"nesting", "nesting", [](GLECMA_Limits * l, uint64_t cap) { l->nesting = cap; },
        [](size_t n) { return Nest("(", "x", ")", n); }},
    {"literal_length", "literal_length", [](GLECMA_Limits * l, uint64_t cap) { l->literal_length = cap; },
        [](size_t n) { return "x = '" + Repeat("s", n) + "';"; }},
  };
  for (const Case & c : cases) {
    // The cap is fixed; the input grows 64 times past it. The memory a refused
    // parse held at its peak must not grow with the input.
    const uint64_t cap = c.limit == std::string("nesting") ? 50 : 4096;
    size_t smallest_peak = 0, largest_peak = 0;
    for (size_t factor : {size_t{4}, size_t{256}}) {
      const std::string text = c.make(static_cast<size_t>(cap) * factor);
      glecma_test::MeteringAllocator meter;
      GLECMA_Source * source = nullptr;
      GLECMA_Limits limits = GLECMA_LIMITS_INIT;
      // The source itself is not capped here (it is the input); the parse's cap is.
      ASSERT_EQ(glecma_source_create_utf8(text.data(), text.size(), nullptr, nullptr, nullptr, &source), GLECMA_OK);
      c.set(&limits, cap);
      GLECMA_ParseOptions options = GLECMA_PARSE_OPTIONS_INIT;
      options.limits = &limits;
      GLECMA_Failure failure = GLECMA_FAILURE_INIT;
      GLECMA_Tree * tree = nullptr;
      ASSERT_EQ(glecma_parse(source, &options, meter.get(), &failure, &tree), GLECMA_ERR_LIMIT) << c.name;
      EXPECT_STREQ(failure.limit, c.limit);
      EXPECT_EQ(tree, nullptr);
      EXPECT_EQ(meter.live_bytes(), 0u) << c.name;
      (factor == 4 ? smallest_peak : largest_peak) = meter.peak_bytes();
      glecma_source_release(source);
    }
    // A parse stops where the cap is: the input 64 times larger holds the same memory (within the
    // slack of a table that has doubled).
    EXPECT_LE(largest_peak, smallest_peak * 2 + 4096) << c.name << ": " << smallest_peak << " then " << largest_peak;
  }
}

// ---- Every allocation of every goal failing in turn --------------------------------------------

namespace {

struct Goal {
  const char * name;
  std::string text;
  uint32_t goal;
  uint32_t context;
  bool eval_context;
};

// Parses `text` for each way a source is made (UTF-8, UTF-16, a stream read in
// short pieces) through one allocator, and returns the result.
GLECMA_Result ParseWith(const Goal & g, int how, uint32_t internal, glecma_test::CountingAllocator * counting,
    GLECMA_Failure * failure) {
  GLECMA_Source * source = nullptr;
  GLECMA_Result made;
  std::vector<uint16_t> wide;
  GLECMA_Stream * stream = nullptr;
  OneByteReader reader{&g.text, 0};
  if (how == 0) {
    made = glecma_source_create_utf8(g.text.data(), g.text.size(), nullptr, counting->get(), failure, &source);
  } else if (how == 1) {
    for (unsigned char c : g.text) {
      wide.push_back(c);
    }
    made = glecma_source_create_utf16(wide.data(), wide.size(), nullptr, counting->get(), failure, &source);
  } else {
    made = glecma_stream_create_callback(&ReadOneByte, nullptr, &reader, counting->get(), &stream);
    if (made == GLECMA_OK) {
      made = glecma_source_create_stream(stream, GLECMA_ENCODING_UTF8, nullptr, counting->get(), failure, &source);
      glecma_stream_destroy(stream);
    }
  }
  if (made != GLECMA_OK) {
    EXPECT_EQ(source, nullptr);
    return made;
  }
  GLECMA_ParseOptions options = GLECMA_PARSE_OPTIONS_INIT;
  options.goal = g.goal;
  options.context = g.context;
  GLECMA_EvalContext eval = GLECMA_EVAL_CONTEXT_INIT;
  GLECMA_PrivateName name;
  static const uint16_t kName[] = {'p'};
  name.units = kName;
  name.length = 1;
  name.reserved = 0;
  if (g.eval_context) {
    eval.flags = GLECMA_CONTEXT_NEW_TARGET | GLECMA_CONTEXT_SUPER_PROPERTY;
    eval.private_names = &name;
    eval.private_name_count = 1;
    options.eval_context = &eval;
  }
  GLECMA_Tree * tree = nullptr;
  GLECMA_Result r = glecma_parse_internal(source, &options, counting->get(), internal, failure, &tree);
  EXPECT_EQ(r == GLECMA_OK, tree != nullptr);
  glecma_tree_destroy(tree);
  glecma_source_release(source);
  return r;
}

}  // namespace

TEST(Hostile, EveryAllocationOfEveryGoalMayFailWithNothingLeaked) {
  glecma_test::Deadline deadline(std::chrono::seconds(Slow() ? 900 : 180), "the allocation sweeps did not finish");
  // A program for each goal, valid, and for each a refused one: failing an
  // allocation before the refusal must still be an out-of-memory result.
  const std::string valid_script =
      "'use strict';\nlet [a, {b = 1, ...c}] = d;\nclass K extends L { static #p = 1; get x() { return this.#p; } static { q; } *m(...r) { for (const s of r) yield s; } }\n"
      "const f = async (u = 1, [v]) => { await u; return `t${v}u`; };\nx = (a, b) => a ? /re+(?<n>x)\\k<n>/giu.test(b) : { y, z: [1, 2], ...w };\n"
      "label: for (var m in n) { if (o) continue label; else break label; }\ntry { p } catch ({ q }) { r } finally { s }\n";
  const std::string valid_module =
      "import d, {a as b, 'c d' as e} from 'm' with {type: 'json'};\nimport * as ns from 'n';\nexport const k = await ns.load(import.meta.url);\n"
      "export {b as f, e as 'g h'};\nexport * as w from 'o';\nexport default class Z extends d { #p = 1; static { this.s = 1; } }\nexport function fn(x) { return x + b; }\n";
  const std::string valid_eval = "new.target; super.x; this.#p; var y = () => new.target; function g() { return arguments; }\n";
  const Goal goals[] = {
    {"script", valid_script, GLECMA_PARSE_SCRIPT, 0, false},
    {"script-refused", valid_script + "let a;\n", GLECMA_PARSE_SCRIPT, 0, false},
    {"strict-script", "function f(a) { return a + 1; } var x = f(2);\n", GLECMA_PARSE_SCRIPT, GLECMA_CONTEXT_STRICT, false},
    {"strict-script-refused", "var x = 010; with (x) {}\n", GLECMA_PARSE_SCRIPT, GLECMA_CONTEXT_STRICT, false},
    {"module", valid_module, GLECMA_PARSE_MODULE, 0, false},
    {"module-refused", valid_module + "export {nothing};\n", GLECMA_PARSE_MODULE, 0, false},
    {"direct-eval", valid_eval, GLECMA_PARSE_DIRECT_EVAL, 0, true},
    {"direct-eval-refused", valid_eval + "super();\n", GLECMA_PARSE_DIRECT_EVAL, 0, true},
    {"indirect-eval", "var a = 1; function f() { return a; }\n", GLECMA_PARSE_INDIRECT_EVAL, 0, false},
    {"indirect-eval-refused", "new.target;\n", GLECMA_PARSE_INDIRECT_EVAL, 0, false},
  };
  for (const Goal & g : goals) {
    for (uint32_t internal : {0u, static_cast<uint32_t>(GLECMA_PARSE_INTERNAL_CHECKING)}) {
      for (int how = 0; how < 3; how++) {
        size_t total = 0;
        GLECMA_Result expected;
        {
          glecma_test::CountingAllocator counting;
          GLECMA_Failure failure = GLECMA_FAILURE_INIT;
          expected = ParseWith(g, how, internal, &counting, &failure);
          total = counting.calls();
          EXPECT_EQ(counting.live(), 0u) << g.name;
          EXPECT_TRUE(expected == GLECMA_OK || expected == GLECMA_ERR_SYNTAX) << g.name << " " << failure.message;
        }
        ASSERT_GT(total, 4u) << g.name;
        for (size_t i = 0; i < total; i++) {
          glecma_test::CountingAllocator counting;
          counting.fail_at(i);
          GLECMA_Failure failure = GLECMA_FAILURE_INIT;
          GLECMA_Result r = ParseWith(g, how, internal, &counting, &failure);
          EXPECT_EQ(r, GLECMA_ERR_OOM) << g.name << " (source " << how << ", builder " << internal << "), allocation "
                                       << i << " of " << total;
          EXPECT_EQ(counting.live(), 0u) << g.name << " allocation " << i;
        }
        // Past the last allocation nothing fails and the answer is the same.
        glecma_test::CountingAllocator counting;
        counting.fail_at(total);
        GLECMA_Failure failure = GLECMA_FAILURE_INIT;
        EXPECT_EQ(ParseWith(g, how, internal, &counting, &failure), expected) << g.name;
      }
    }
  }
}

TEST(Hostile, EveryAllocationOfADynamicFunctionMayFailWithNothingLeaked) {
  glecma_test::Deadline deadline(std::chrono::seconds(Slow() ? 600 : 120), "the dynamic-function sweep did not finish");
  const struct {
    uint32_t kind;
    const char * parameters;
    const char * body;
  } cases[] = {
    {GLECMA_FNKIND_FUNCTION, "a, b = 1, ...c", "return a + b + c.length;"},
    {GLECMA_FNKIND_GENERATOR, "a", "yield a; yield* [1, 2];"},
    {GLECMA_FNKIND_ASYNC_FUNCTION, "{a}, [b]", "await a; return b;"},
    {GLECMA_FNKIND_ASYNC_GENERATOR, "", "for await (const x of y) yield x;"},
    {GLECMA_FNKIND_FUNCTION, "a, a", "'use strict';"},  // refused
  };
  for (const auto & c : cases) {
    auto run = [&](glecma_test::CountingAllocator * counting, GLECMA_Failure * failure) {
      GLECMA_Source * parameters = nullptr;
      GLECMA_Source * body = nullptr;
      GLECMA_Result r = glecma_source_create_utf8(c.parameters, std::strlen(c.parameters), nullptr, counting->get(), failure, &parameters);
      if (r == GLECMA_OK) {
        r = glecma_source_create_utf8(c.body, std::strlen(c.body), nullptr, counting->get(), failure, &body);
      }
      if (r == GLECMA_OK) {
        GLECMA_DynamicFunction function = GLECMA_DYNAMIC_FUNCTION_INIT;
        function.kind = c.kind;
        function.parameters = parameters;
        function.body = body;
        GLECMA_Tree * tree = nullptr;
        r = glecma_parse_dynamic_function(&function, nullptr, counting->get(), failure, &tree);
        EXPECT_EQ(r == GLECMA_OK, tree != nullptr);
        glecma_tree_destroy(tree);
      }
      glecma_source_release(parameters);
      glecma_source_release(body);
      return r;
    };
    size_t total = 0;
    GLECMA_Result expected;
    {
      glecma_test::CountingAllocator counting;
      GLECMA_Failure failure = GLECMA_FAILURE_INIT;
      expected = run(&counting, &failure);
      total = counting.calls();
      EXPECT_EQ(counting.live(), 0u);
    }
    ASSERT_GT(total, 4u);
    for (size_t i = 0; i < total; i++) {
      glecma_test::CountingAllocator counting;
      counting.fail_at(i);
      GLECMA_Failure failure = GLECMA_FAILURE_INIT;
      EXPECT_EQ(run(&counting, &failure), GLECMA_ERR_OOM) << c.parameters << " | " << c.body << " allocation " << i;
      EXPECT_EQ(counting.live(), 0u) << i;
    }
    glecma_test::CountingAllocator counting;
    counting.fail_at(total);
    GLECMA_Failure failure = GLECMA_FAILURE_INIT;
    EXPECT_EQ(run(&counting, &failure), expected);
  }
}

// ---- Parses on separate threads ------------------------------------------------------------------

TEST(Hostile, ParsesOfEveryGoalOnSeparateThreadsOfSharedSourcesAgree) {
  glecma_test::Deadline deadline(std::chrono::seconds(Slow() ? 600 : 120), "the threaded parses did not finish");
  const std::string script = "class A { #p = 1; m() { return this.#p; } } let [a, b] = [1, 2]; x = (a, b) => a + b; `t${a}`;\n";
  const std::string module = "import a from 'm'; export const b = await a; export default class {}\n";
  GLECMA_Source * script_source = nullptr;
  GLECMA_Source * module_source = nullptr;
  ASSERT_EQ(glecma_source_create_utf8(script.data(), script.size(), nullptr, nullptr, nullptr, &script_source), GLECMA_OK);
  ASSERT_EQ(glecma_source_create_utf8(module.data(), module.size(), nullptr, nullptr, nullptr, &module_source), GLECMA_OK);
  // The options and limits are shared and only read.
  GLECMA_Limits limits = GLECMA_LIMITS_INIT;
  GLECMA_ParseOptions script_options = GLECMA_PARSE_OPTIONS_INIT;
  script_options.limits = &limits;
  GLECMA_ParseOptions module_options = script_options;
  module_options.goal = GLECMA_PARSE_MODULE;
  std::atomic<int> wrong{0};
  std::atomic<uint64_t> nodes{0};
  std::vector<std::thread> threads;
  for (int t = 0; t < 8; t++) {
    threads.emplace_back([&, t] {
      for (int i = 0; i < 200; i++) {
        const bool is_module = ((t + i) & 1) != 0;
        GLECMA_Source * source = is_module ? module_source : script_source;
        GLECMA_Tree * tree = nullptr;
        GLECMA_Failure failure = GLECMA_FAILURE_INIT;
        GLECMA_Result r = glecma_parse_internal(source, is_module ? &module_options : &script_options, nullptr,
            (i & 2) != 0 ? GLECMA_PARSE_INTERNAL_CHECKING : 0u, &failure, &tree);
        if (r != GLECMA_OK || tree == nullptr) {
          wrong++;
        } else {
          nodes += glecma_node_end(glecma_tree_root(tree));
        }
        glecma_tree_destroy(tree);
      }
    });
  }
  for (std::thread & th : threads) {
    th.join();
  }
  EXPECT_EQ(wrong.load(), 0);
  EXPECT_GT(nodes.load(), 0u);
  glecma_source_release(script_source);
  glecma_source_release(module_source);
}

int main(int argc, char * * argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

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
 * The allocation-failure harness: an input the fuzzer chose is parsed once with
 * an allocator that counts, and then again with the allocation the input picks
 * made to fail. Every allocation the work makes, from the source's to the
 * tree's, goes through that allocator, so the input reaches the error arms
 * that a working allocator never does.
 *
 * The first byte chooses what the rest is read as and how:
 *
 *   bits 0-2    the goal: 0 a Script, 1 a Module, 2 direct eval (with a caller
 *               context that holds `new.target`, `super.x` and one private
 *               name), 3 indirect eval, 4 to 7 a dynamic function of the kind
 *               (goal - 4)
 *   bit 3       UTF-16 code units in host order (else UTF-8)
 *   bit 4       the checking builder (else the tree builder)
 *   bit 5       strict code (a Script only)
 *   bit 6       the source is made from the library's input stream, read in
 *               short pieces
 *   bit 7       the function re-parse is run, with the failing allocator, over
 *               every function of an accepted program (a tree builder only)
 *
 * The next two bytes, as a 16-bit number, pick the allocation that fails: the
 * number taken modulo one more than the allocations the whole work made. The
 * rest is the program (for a dynamic function its first byte is where it is
 * split into the parameters and the body).
 *
 * It requires what an allocation failure must be: the result is out of memory
 * and nothing else (not a syntax error that a later allocation would have
 * found, not a success, not a different failure), no tree or source is
 * returned, and every block the work took has been freed. One more than the
 * last allocation fails nothing, and gives the answer the working run gave.
 * An invariant that does not hold aborts.
 *
 * Built twice: by `make fuzz-alloc` under libFuzzer, and by `make fuzz-replay`
 * as a plain program over the seed corpus.
 */

#include <ghoti.io/lang-ecmascript/lang-ecmascript.h>

#include "fuzz_common.h"
#include "../reparse_support.h"
#include "parse/parse_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  uint32_t goal;       // 0 to 7, as the first byte says
  int wide;
  int checking;
  int strict;
  int stream;
  int reparse;
  const uint8_t * data;
  size_t size;
} Job;

static const uint16_t kPrivateName[] = {'p'};

// Every function of an accepted tree read again through the failing allocator. The
// walk that finds them uses the default allocator (it is the harness's own); the
// reads are the library's.
static GLECMA_Result reparse_all(GLECMA_Source * source, const GLECMA_Tree * tree,
    const GLECMA_Allocator * allocator) {
  RpJob * jobs = NULL;
  size_t count = 0;
  char message[200];
  if (rp_collect(source, tree, &jobs, &count, message, sizeof message) != 0) {
    // The harness's own walk could not place a function: not a library failure to judge here.
    return GLECMA_OK;
  }
  GLECMA_Result result = GLECMA_OK;
  for (size_t i = 0; i < count && result == GLECMA_OK; i++) {
    GLECMA_Failure failure = GLECMA_FAILURE_INIT;
    GLECMA_Tree * again = NULL;
    result = glecma_parse_function(source, &jobs[i].context, NULL, allocator, &failure, &again);
    // A function of an accepted program reads again, or memory ran out.
    check(result == GLECMA_OK || result == GLECMA_ERR_OOM);
    check((result == GLECMA_OK) == (again != NULL));
    glecma_tree_destroy(again);
  }
  rp_free_jobs(jobs, count);
  return result;
}

// One whole run of the work through `alloc`. Returns what it returned (the first
// result that was not success), and releases everything it made.
static GLECMA_Result run(const Job * job, FuzzAlloc * alloc) {
  GLECMA_Failure failure = GLECMA_FAILURE_INIT;
  GLECMA_Limits limits = GLECMA_LIMITS_INIT;
  GLECMA_Source * source = NULL;
  GLECMA_Source * second = NULL;
  const uint8_t * data = job->data;
  size_t size = job->size;
  GLECMA_Result r;
  if (job->goal >= 4) {
    const size_t split = size == 0 ? 0 : (size_t)data[0] % size;
    if (size > 0) {
      data++;
      size--;
    }
    const size_t cut = split > size ? size : split;
    r = fuzz_make_source_with(job->wide, job->stream, data, cut, &limits, &alloc->allocator, &failure, &source);
    if (r == GLECMA_OK) {
      r = fuzz_make_source_with(job->wide, job->stream, data + cut, size - cut, &limits, &alloc->allocator, &failure,
          &second);
    }
    if (r == GLECMA_OK) {
      GLECMA_DynamicFunction function = GLECMA_DYNAMIC_FUNCTION_INIT;
      function.kind = job->goal - 4u;
      function.parameters = source;
      function.body = second;
      GLECMA_Tree * tree = NULL;
      r = glecma_parse_dynamic_function(&function, NULL, &alloc->allocator, &failure, &tree);
      check((r == GLECMA_OK) == (tree != NULL));
      glecma_tree_destroy(tree);
    }
    glecma_source_release(source);
    glecma_source_release(second);
    return r;
  }
  r = fuzz_make_source_with(job->wide, job->stream, data, size, &limits, &alloc->allocator, &failure, &source);
  if (r != GLECMA_OK) {
    check(source == NULL);
    return r;
  }
  static const uint32_t goals[] = {GLECMA_PARSE_SCRIPT, GLECMA_PARSE_MODULE, GLECMA_PARSE_DIRECT_EVAL,
      GLECMA_PARSE_INDIRECT_EVAL};
  GLECMA_ParseOptions options = GLECMA_PARSE_OPTIONS_INIT;
  options.limits = &limits;
  options.goal = goals[job->goal];
  options.context = job->strict && job->goal == 0 ? GLECMA_CONTEXT_STRICT : 0u;
  GLECMA_EvalContext context = GLECMA_EVAL_CONTEXT_INIT;
  GLECMA_PrivateName name;
  if (job->goal == 2) {
    name.units = kPrivateName;
    name.length = 1;
    name.reserved = 0;
    context.flags = GLECMA_CONTEXT_NEW_TARGET | GLECMA_CONTEXT_SUPER_PROPERTY;
    context.private_names = &name;
    context.private_name_count = 1;
    options.eval_context = &context;
  }
  GLECMA_Tree * tree = NULL;
  r = glecma_parse_internal(source, &options, &alloc->allocator,
      job->checking ? GLECMA_PARSE_INTERNAL_CHECKING : 0u, &failure, &tree);
  check((r == GLECMA_OK) == (tree != NULL));
  if (r != GLECMA_OK) {
    check(r == GLECMA_ERR_SYNTAX || r == GLECMA_ERR_LIMIT || r == GLECMA_ERR_OOM);
    check_failure(r, &failure, glecma_source_units(source));
  }
  if (r == GLECMA_OK && job->reparse && !job->checking) {
    r = reparse_all(source, tree, &alloc->allocator);
  }
  glecma_tree_destroy(tree);
  glecma_source_release(source);
  return r;
}

int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
  if (size < 3) {
    return 0;
  }
  Job job;
  job.goal = data[0] & 7u;
  job.wide = (data[0] & 8) != 0;
  job.checking = (data[0] & 16) != 0;
  job.strict = (data[0] & 32) != 0;
  job.stream = (data[0] & 64) != 0;
  job.reparse = (data[0] & 128) != 0;
  const size_t pick = (size_t)data[1] | ((size_t)data[2] << 8);
  job.data = data + 3;
  job.size = size - 3;

  FuzzAlloc counting;
  fuzz_alloc_init(&counting, SIZE_MAX);
  const GLECMA_Result expected = run(&job, &counting);
  check(counting.live == 0);
  const size_t total = counting.calls;
  if (total == 0) {
    return 0;
  }
  const size_t fail_at = pick % (total + 1);
  FuzzAlloc failing;
  fuzz_alloc_init(&failing, fail_at);
  const GLECMA_Result got = run(&job, &failing);
  check(failing.live == 0);
  if (fail_at < total) {
    // An allocation that was asked for and refused is out of memory, whatever else the input is.
    check(got == GLECMA_ERR_OOM);
  } else {
    check(got == expected);
  }
  return 0;
}

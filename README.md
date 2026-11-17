# Ghoti.io Lang-ecmascript

An ECMAScript (JavaScript) engine for embedding (`GLECMA`), built on the
Ghoti.io language runtime: `runtime-core` for the execution context, its
budgets and the guest stack, and later `runtime-heap` for the collected heap,
`runtime-jit` for compiled code and `runtime-debug` for debugging. It targets
**ECMA-262, 17th edition (ECMAScript 2026)**, with Annex B. It is part of the
[Ghoti.io suite](https://github.com/Ghoti-io/suite).

The library is the engine and nothing more: the language and its built-in
objects. A web browser, a desktop application shell or a standalone runtime
supplies the event loop, timers, I/O and the DOM through host hooks. The
design, the target and the plan are in
[documentation/design.md](documentation/design.md).

## Status

**Scaffolding, source text and the tokenizer.** The library registers an
ECMAScript engine with a runtime-core context, reports its version and target
edition, takes source text (UTF-8 or UTF-16, validated and copied, with exact
line and column locations, under a size cap) and reads it into tokens by the
whole lexical grammar of the edition, Annex B included, with identifiers from
`unicode` at one pinned Unicode version. It does not yet parse or run script;
the parser comes next, then the bytecode compiler and interpreter. No interface is stable: every header is labelled
`@stability free` until the suite's first release.

## Example

```c
#include <ghoti.io/lang-ecmascript/lang-ecmascript.h>
#include <ghoti.io/runtime-core/b/group.h>
#include <stdio.h>

int main(void) {
  GRCORE_Group * group = NULL;
  GRCORE_Context * context = NULL;
  GRCORE_EngineId id = 0;

  if (grcore_group_create(NULL, NULL, &group) != GRCORE_OK ||
      grcore_context_create(group, NULL, &context) != GRCORE_OK) {
    return 1;
  }
  GLECMA_Result result = glecma_engine_register(context, &id);
  if (result != GLECMA_OK) {
    fprintf(stderr, "%s\n", glecma_result_string(result));
    return 1;
  }
  printf("ECMAScript %d is engine %u\n", GLECMA_ECMA262_YEAR, (unsigned)id);
  grcore_context_destroy(context);
  grcore_group_destroy(group);
  return 0;
}
```

`examples/register_engine.c` is the same program with its error handling;
`examples/source_locations.c` creates a source and asks where things are in
it. `make examples` builds and runs both.

## The API so far

- `core.h`: `GLECMA_Result` and `glecma_result_string`; the version
  (`glecma_version_string`, `glecma_version_number` and the `GLECMA_VERSION_*`
  macros); the target edition (`GLECMA_ECMA262_EDITION`,
  `GLECMA_ECMA262_YEAR`, `glecma_ecma262_edition`).
- `allocator.h`: `GLECMA_Allocator`, which is cutil's `GCU_Allocator`, and
  `glecma_allocator_default`.
- `engine.h`: `glecma_engine_descriptor`, the static descriptor that
  identifies an ECMAScript frame to runtime-core and its consumers;
  `glecma_engine_register`, which registers it with a context and is
  idempotent; and `glecma_engine_find`.
- `core.h` also holds what every parse call reports through:
  `GLECMA_ERR_SYNTAX`, appended after the shared nine results for source
  text that is not valid ECMAScript; `GLECMA_Location` (offset in input
  units, 1-based line, 0-based column in UTF-16 code units); and
  `GLECMA_Failure` (size first, set with `GLECMA_FAILURE_INIT`; result, static
  message, location, the limit's name for `GLECMA_ERR_LIMIT`, the broken
  rule's code for `GLECMA_ERR_SYNTAX` and the feature's name for a refused
  feature set), written on every return when the caller passes one;
  `GLECMA_Rule`, the append-only rule codes, with `glecma_rule_name` and
  `glecma_rule_clause`; and the Unicode version the engine is pinned to
  (`GLECMA_UNICODE_VERSION`, `glecma_unicode_version`).
- `limits.h`: `GLECMA_Limits` (size first, set with `GLECMA_LIMITS_INIT`, so a
  field can be added without breaking a caller built earlier) and
  `glecma_limits_default`. Its fields are `source_units` (default 64 Mi
  units), `tokens` (48 Mi) and `literal_length` (16 Mi code points); zero is a
  cap of zero.
- `options.h`: `GLECMA_ParseOptions` (size first: the limits and a feature
  set; `glecma_parse_options_default`) and `GLECMA_FeatureSet`
  (`glecma_feature_set_add`, `glecma_feature_set_has`). The pinned edition is
  what an empty feature set accepts; this build defines no feature, so every
  entry that takes the options refuses a non-empty set with
  `GLECMA_ERR_UNSUPPORTED`, naming the feature, before it reads anything.
- `source.h`: `glecma_source_create_utf8` and `glecma_source_create_utf16`
  copy and validate source text (ill-formed UTF-8 is `GLECMA_ERR_CORRUPT` at
  the first bad byte; a lone UTF-16 surrogate is a code point of its own;
  nothing is stripped, a leading BOM included); `glecma_source_location`
  maps an offset to its line and column, with lines split at ECMA-262's five
  line terminators; `glecma_source_encoding`, `_units`, `_code_points`,
  `_lines`, `_dump`, `_retain` and `_release`. A source is immutable and atomically counted, and any number of
  threads may read one at once.

```c
GLECMA_Source * source = NULL;
GLECMA_Failure failure = GLECMA_FAILURE_INIT;
GLECMA_Result result =
    glecma_source_create_utf8(text, length, NULL, NULL, &failure, &source);
if (result == GLECMA_ERR_CORRUPT) {
  // The location names the first ill-formed byte.
  fprintf(stderr, "%s at line %zu, column %zu\n",
      failure.message != NULL ? failure.message : "ill-formed UTF-8",
      failure.location.line, failure.location.column);
  return 1;
} else if (result == GLECMA_ERR_LIMIT) {
  fprintf(stderr, "over the %s limit\n", failure.limit);
  return 1;
} else if (result != GLECMA_OK) {
  fprintf(stderr, "%s\n", glecma_result_string(result));
  return 1;
}
GLECMA_Location location;
if (glecma_source_location(source, offset, &location) == GLECMA_OK) {
  printf("line %zu, column %zu\n", location.line, location.column);
} else {
  // Past the end, or inside a multi-unit code point.
  fprintf(stderr, "offset %zu is not a position\n", offset);
}
glecma_source_release(source);
```

Outputs are written only on success, and a failure leaves nothing allocated.
A NULL allocator means the default, and NULL limits mean the defaults.

## Building

```bash
export PKG_CONFIG_PATH="<prefix>/share/pkgconfig"
make test PREFIX=<prefix>
```

`<prefix>` holds `cutil`, `unicode` and `runtime-core`, installed by the suite's
`install.sh` or by each library's `make install PREFIX=<prefix>`. pkg-config
is the only way the library finds them. Pass `PREFIX` on every make: it is
also what gives the built programs their run path. Once installed, a program
links with `pkg-config --cflags --libs ghoti.io-lang-ecmascript-0`, and
`make check-install PREFIX=<prefix>` proves that path with a C and a C++
consumer.

The tokenizer is internal for now: the parser that will call it is the first
public entry that reads source, and the tokenizer's tests, fuzzers and
benchmark reach it directly. `make fuzz-replay` runs the seed corpus of the
tokenizer and source fuzzers through their entry points; `make fuzz-tokenizer`
and `make fuzz-source` build them for libFuzzer (clang), and `make bench`
reports the throughput of the source decoder and of the tokenizer.

`make test` runs the unit tests and the gates: every exported symbol carries
the version namespace (`check-symbols`), every public header carries a
stability label (`check-labels`), the library includes and links nothing but
its declared dependencies (`check-edges`), every compile flag set disables
floating-point contraction (`check-fp-contract`), strict-aliasing warnings are
armed (`check-aliasing`) and every compile rule records its flags
(`check-stamps`), the limit names are an append-only registry
(`check-limit-names`), the benchmark harness exists and runs (`check-bench`)
and the fuzz seeds replay clean (`fuzz-replay`). `make check-planted` applies
each planted defect in `tools/planted/` to a scratch copy and requires its
gate to fail. `make test-asan`, `make test-tsan` and `make test-valgrind`
run the same tests under the sanitizers. `make help` lists every target.

## License

LGPL-3.0-only. `COPYING` holds the GPL-3.0 and `COPYING.LESSER` the LGPL-3.0
that is written as additional permissions on top of it. Contributions are not
being accepted at this time; `CONTRIBUTING.md` says why.

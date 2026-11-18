# Benchmarks

The harness is `bench/bench.c`. `make bench` runs every case; `make test`
builds it and runs it once on a tiny workload through the `check-bench` gate,
which also fails when `bench/` holds no source, so the harness cannot be
emptied without a test failing. The gate was seen to fail: with `bench/`
emptied, `make test` stopped at `check-bench` with "no benchmark harness
under bench/" and exit status 2.

## Cases

| Case | Unit of work | Input |
| --- | --- | --- |
| `calibration-xorshift` | one step of a serial xorshift64 chain; touches no library code | none |
| `source-decode-utf8` | create and release a source from each seed, UTF-8 | the seed set |
| `source-decode-utf16` | the same, from the seeds transcoded to UTF-16 | the seed set |
| `tokenize-utf8` | read every seed, already a source, to its end token by token | the five seeds (the decoder's three, `tokens.js`, `minified.js`) |
| `tokenize-utf16` | the same, from the seeds transcoded to UTF-16 | the same |
| `parse-utf8` | every seed (the same five) parsed to a tree with the tree builder; the clock stops before the trees are released | the five seeds |
| `parse-utf16` | the same, from the seeds transcoded to UTF-16 | the same |
| `parse-checking-utf8` | the same with the checking builder, which builds no function body | the same |
| `tree-destroy` | every seed's tree released; the parse that made it is not in the clock | the same |
| `parse-module-utf8` | one generated module (imports with attributes, exports of declarations, lists and `default`, and statements) parsed under the Module goal with the tree builder; the clock stops before the tree is released | the generated module, about 256 KiB |
| `reparse-functions-utf8` | every function of the five parse seeds read again with `glecma_parse_function`, from its range and a context derived from the tree; the first trees and the list of functions are built at setup | the five seeds (the same as `parse-utf8`) |

Creating a source decodes every code point and indexes its lines, which is
the decoder's whole cost. The seed set is the three files under
`bench/seeds/`: `ascii.js` (plain ASCII script), `multibyte.js` (2-, 3- and
4-byte sequences) and `terminators.js` (CRLF, CR and LF line ends). Each seed
is repeated up to 256 KiB at setup so one unit is long enough to time; setup
is not timed; the tokenizer cases read sources that already exist, so
neither the decoder nor setup is in their clock, and they choose each goal by
the library's test driver's guess, not a parser's. Cases over a real-world corpus are separate cases, added when
that corpus is pinned.

## How noise is measured

Each case runs 9 repeats of its full workload. The report gives the best
repeat, the median, and the spread: (slowest - fastest) / median, in percent.
The best repeat is the figure to compare (it is the one least disturbed by
other work on the machine); the spread says how far to trust it. A spread
above about 10% means the machine was busy, and the run is repeated before a
figure is recorded. Every figure is recorded beside the calibration figure
taken in the same run, because a figure without it cannot be compared across
machines or compilers.

## Budgets

None is asserted. A budget needs a first measurement of its case, taken and
recorded here; until a case has a row in the history below, nothing may
claim it is fast or slow. From this story on, every story that adds a
capability runs the set before and after and appends the figures.

## History

Compiler: GCC 16.2.0 (the suite's build image), `-O2`, release build.
Machine class: a 12-thread x86-64 laptop, 16 GB, shared with other work.

| Story | Case | Best | Median | Spread | Throughput | Calibration (best) |
| --- | --- | --- | --- | --- | --- | --- |
| 1b | `calibration-xorshift` | 1.32 ns/step | 1.43 ns/step | 24.6% | | 1.32 ns/step |
| 1b | `source-decode-utf8` | 2.43 ms | 2.60 ms | 16.9% | 308 MiB/s | 1.32 ns/step |
| 1b | `source-decode-utf16` | 1.79 ms | 1.83 ms | 9.0% | 784 MiB/s | 1.32 ns/step |
| 1c | `calibration-xorshift` | 1.32 ns/step | 1.35 ns/step | 12.2% | | 1.32 ns/step |
| 1c | `source-decode-utf8` | 2.38 ms | 2.53 ms | 9.5% | 316 MiB/s | 1.32 ns/step |
| 1c | `source-decode-utf16` | 1.58 ms | 1.59 ms | 14.0% | 891 MiB/s | 1.32 ns/step |

These first figures are noisy (the calibration spread is 24.6%, over the 10%
line) because the machine was in use; they are a first measurement, not a
budget, and are to be retaken on a quiet machine before any budget is
derived from them. A unit is three sources of about 256 KiB each.

Story 1c (size-first structs, counted source) changed the created object by one
counter and one atomic increment per create and release; the 1b rows are its
"before", the 1c rows its "after", on the same machine. The differences are
within the spread of the runs, so no change in decoder cost is claimed.

Story 2 (the tokenizer) added the two tokenizer cases and two seeds for them
(`tokens.js`: every kind of token in dense runs; `minified.js`: minified-style
code). The decoder cases keep their three seeds, so their rows are comparable
with the earlier ones. Same compiler and machine class as above, one run each,
the best of nine repeats.

| Story | Case | Best | Median | Spread | Throughput | Calibration (best) |
| --- | --- | --- | --- | --- | --- | --- |
| 2 before | `calibration-xorshift` | 1.25 ns/step | 1.25 ns/step | 3.1% | | 1.25 ns/step |
| 2 before | `source-decode-utf8` | 2.30 ms | 2.33 ms | 1.9% | 326 MiB/s | 1.25 ns/step |
| 2 before | `source-decode-utf16` | 1.56 ms | 1.56 ms | 0.8% | 900 MiB/s | 1.25 ns/step |
| 2 after | `calibration-xorshift` | 1.41 ns/step | 1.43 ns/step | 2.2% | | 1.41 ns/step |
| 2 after | `source-decode-utf8` | 2.29 ms | 2.31 ms | 1.6% | 327 MiB/s | 1.41 ns/step |
| 2 after | `source-decode-utf16` | 1.60 ms | 1.61 ms | 1.1% | 880 MiB/s | 1.41 ns/step |
| 2 | `tokenize-utf8` | 9.99 ms | 10.08 ms | 1.6% | 125 MiB/s, 39.6 Mtokens/s | 1.41 ns/step |
| 2 | `tokenize-utf16` | 10.02 ms | 10.07 ms | 3.7% | 240 MiB/s, 39.5 Mtokens/s | 1.41 ns/step |

A unit of the tokenizer cases is the five seeds read once: 395,477 tokens,
about 0.30 tokens per UTF-8 byte (the seeds are denser than ordinary code). The
decoder rows did not change beyond the noise (the UTF-16 row is 2.5% slower on
the second run, whose calibration is also 13% slower, a quieter and a busier
moment of the same machine), so story 2 claims no decoder regression. The
tokenizer rows are a first measurement, not a budget; an earlier run gave
42 Mtokens/s and 134 MiB/s at a calibration of 1.26 ns/step, so the
run-to-run difference of the machine is about 6%.

Story 4 (the parser and the public tree) added the four parser cases above. They
read the same five seeds as the tokenizer, which all parse as Scripts, so a
parse costs what a token costs plus what the grammar and the tree add, over
the same bytes and tokens. Same compiler and machine class as above, one run
each, the best of nine repeats (the "before" rows are the run taken before the
story's first commit, the "after" rows the run taken after its last).

| Story | Case | Best | Median | Spread | Throughput | Calibration (best) |
| --- | --- | --- | --- | --- | --- | --- |
| 4 before | `calibration-xorshift` | 1.24 ns/step | 1.27 ns/step | 3.6% | | 1.24 ns/step |
| 4 before | `source-decode-utf8` | 2.21 ms | 2.24 ms | 1.3% | 339 MiB/s | 1.24 ns/step |
| 4 before | `source-decode-utf16` | 1.55 ms | 1.56 ms | 1.4% | 907 MiB/s | 1.24 ns/step |
| 4 before | `tokenize-utf8` | 9.14 ms | 9.24 ms | 1.6% | 137 MiB/s, 43.3 Mtokens/s | 1.24 ns/step |
| 4 before | `tokenize-utf16` | 9.65 ms | 9.70 ms | 2.8% | 249 MiB/s, 41.0 Mtokens/s | 1.24 ns/step |
| 4 after | `calibration-xorshift` | 1.35 ns/step | 1.36 ns/step | 7.3% | | 1.35 ns/step |
| 4 after | `source-decode-utf8` | 2.37 ms | 2.38 ms | 1.6% | 317 MiB/s | 1.35 ns/step |
| 4 after | `source-decode-utf16` | 1.52 ms | 1.53 ms | 1.1% | 926 MiB/s | 1.35 ns/step |
| 4 after | `tokenize-utf8` | 9.54 ms | 9.58 ms | 2.6% | 131 MiB/s, 41.4 Mtokens/s | 1.35 ns/step |
| 4 after | `tokenize-utf16` | 9.76 ms | 9.81 ms | 0.6% | 247 MiB/s, 40.5 Mtokens/s | 1.35 ns/step |
| 4 | `parse-utf8` | 27.8 ms | 27.9 ms | 1.3% | 45.0 MiB/s, 14.2 Mtokens/s | 1.35 ns/step |
| 4 | `parse-utf16` | 27.0 ms | 27.1 ms | 5.6% | 89.1 MiB/s, 14.6 Mtokens/s | 1.35 ns/step |
| 4 | `parse-checking-utf8` | 25.8 ms | 25.9 ms | 0.8% | 48.5 MiB/s, 15.3 Mtokens/s | 1.35 ns/step |
| 4 | `tree-destroy` | 0.529 ms | 0.537 ms | 2.9% | 2,368 MiB/s | 1.35 ns/step |

The decoder and tokenizer rows did not change beyond the noise: the calibration
itself is 9% slower in the second run, and the library rows are 3% to 7%
slower, which is the machine and not the code (the story touched neither
function). So no change in either is claimed. The parser rows are a first
measurement and not a budget: a parse takes about three times what reading
the tokens does, which is the grammar, the tree's allocation and the
declaration and reference events (about 70 ns per token on this machine
class). The checking builder is 7% faster over these seeds, which are mostly
top-level code with small bodies; its saving is what bodies cost, and no seed
holds enough of them to show it. Destroying a tree is 2% of the cost of
making one. A budget, peak memory and the corpus cases come with the corpus.

Story 5 (early errors, Annex B syntax and regular expression literals) added a
check to the parser's every declaration, label, class and function, and a call
into `regex` for every regular expression literal. **The workload changed:** a
seed repeated 256 KiB over now repeats inside a block each (`{`, the seed,
`}`), because repeating a seed otherwise declares its `let`, `const` and
classes again in every copy, which is an early error. The decoder and
tokenizer rows read the same text as the parser's, so every row below is on the
new workload (about 0.7% more bytes than the earlier rows' and two more
tokens per copy); the "before" rows are the library as it was at the end of
story 4, built with the new harness and run on the same machine class in the
same session, alternating with the "after" runs, which is why they differ from
the story 4 rows. Machine class as above, a quiet machine this time (the
calibration spread is 0.9% to 2.6%), the best of nine repeats, one run of the
"before" and two of the "after".

| Story | Case | Best | Median | Spread | Throughput | Calibration (best) |
| --- | --- | --- | --- | --- | --- | --- |
| 5 before | `calibration-xorshift` | 1.33 ns/step | 1.34 ns/step | 2.6% | | 1.33 ns/step |
| 5 before | `source-decode-utf8` | 2.46 ms | 2.46 ms | 0.6% | 305 MiB/s | 1.33 ns/step |
| 5 before | `source-decode-utf16` | 1.53 ms | 1.53 ms | 2.4% | 922 MiB/s | 1.33 ns/step |
| 5 before | `tokenize-utf8` | 9.41 ms | 9.45 ms | 1.7% | 133 MiB/s, 42.5 Mtokens/s | 1.33 ns/step |
| 5 before | `tokenize-utf16` | 9.75 ms | 9.76 ms | 0.5% | 247 MiB/s, 41.1 Mtokens/s | 1.33 ns/step |
| 5 before | `parse-utf8` | 25.6 ms | 25.7 ms | 0.8% | 48.8 MiB/s, 15.6 Mtokens/s | 1.33 ns/step |
| 5 before | `parse-utf16` | 26.1 ms | 26.2 ms | 1.4% | 92.3 MiB/s, 15.3 Mtokens/s | 1.33 ns/step |
| 5 before | `parse-checking-utf8` | 25.3 ms | 25.3 ms | 0.5% | 49.5 MiB/s, 15.9 Mtokens/s | 1.33 ns/step |
| 5 before | `tree-destroy` | 0.413 ms | 0.423 ms | 4.9% | 3,029 MiB/s | 1.33 ns/step |
| 5 after (1) | `calibration-xorshift` | 1.30 ns/step | 1.30 ns/step | 0.9% | | 1.30 ns/step |
| 5 after (1) | `source-decode-utf8` | 2.37 ms | 2.38 ms | 0.9% | 317 MiB/s | 1.30 ns/step |
| 5 after (1) | `source-decode-utf16` | 1.48 ms | 1.48 ms | 0.6% | 951 MiB/s | 1.30 ns/step |
| 5 after (1) | `tokenize-utf8` | 9.24 ms | 9.28 ms | 0.9% | 135 MiB/s, 43.3 Mtokens/s | 1.30 ns/step |
| 5 after (1) | `tokenize-utf16` | 9.56 ms | 9.60 ms | 1.3% | 252 MiB/s, 41.9 Mtokens/s | 1.30 ns/step |
| 5 after (1) | `parse-utf8` | 27.1 ms | 27.2 ms | 2.0% | 46.1 MiB/s, 14.8 Mtokens/s | 1.30 ns/step |
| 5 after (1) | `parse-utf16` | 27.6 ms | 27.6 ms | 0.7% | 87.3 MiB/s, 14.5 Mtokens/s | 1.30 ns/step |
| 5 after (1) | `parse-checking-utf8` | 26.6 ms | 26.7 ms | 2.3% | 47.1 MiB/s, 15.1 Mtokens/s | 1.30 ns/step |
| 5 after (1) | `tree-destroy` | 0.471 ms | 0.488 ms | 7.0% | 2,656 MiB/s | 1.30 ns/step |
| 5 after (2) | `calibration-xorshift` | 1.33 ns/step | 1.33 ns/step | 2.0% | | 1.33 ns/step |
| 5 after (2) | `source-decode-utf8` | 2.46 ms | 2.47 ms | 0.7% | 305 MiB/s | 1.33 ns/step |
| 5 after (2) | `source-decode-utf16` | 1.52 ms | 1.53 ms | 1.2% | 924 MiB/s | 1.33 ns/step |
| 5 after (2) | `tokenize-utf8` | 9.32 ms | 9.36 ms | 0.7% | 134 MiB/s, 42.9 Mtokens/s | 1.33 ns/step |
| 5 after (2) | `tokenize-utf16` | 9.63 ms | 9.64 ms | 0.6% | 250 MiB/s, 41.6 Mtokens/s | 1.33 ns/step |
| 5 after (2) | `parse-utf8` | 27.0 ms | 27.1 ms | 1.6% | 46.3 MiB/s, 14.8 Mtokens/s | 1.33 ns/step |
| 5 after (2) | `parse-utf16` | 27.3 ms | 27.6 ms | 1.6% | 88.1 MiB/s, 14.6 Mtokens/s | 1.33 ns/step |
| 5 after (2) | `parse-checking-utf8` | 26.3 ms | 26.4 ms | 2.7% | 47.5 MiB/s, 15.2 Mtokens/s | 1.33 ns/step |
| 5 after (2) | `tree-destroy` | 0.460 ms | 0.468 ms | 5.1% | 2,717 MiB/s | 1.33 ns/step |

The decoder and tokenizer rows did not change beyond the noise (the story
touched neither). The parser rows cost 5% to 6% more: 25.6 to 27.0-27.1 ms for
`parse-utf8`, 26.1 to 27.3-27.6 ms for `parse-utf16`, 25.3 to 26.3-26.6 ms for the checking
builder, which is what the checks and the validation of the two regular
expression literals in each copy of `tokens.js` cost over these seeds (a
declaration record per declared name, a check when each scope that declares
one ends, a label or private-name stack operation per use). The cost is
the same for both builders, as the design requires. `tree-destroy` is 11%
slower in both "after" runs, which the story did not touch (the trees it
releases are the same shape): a difference of about 50 microseconds in a
0.4 ms case that is the noise of that case's own spread across sessions, and
is recorded and not explained. No budget is asserted.

The cost of one var declaration under many enclosing scopes, measured by
parsing a generated file (`{ let a0; { let a1; ... { var v0; var v1; ... } } }`)
through the test262 driver, once: 100,000 vars in the innermost of 250 nested
blocks that each declare a name take 0.16 s, 0.04 s when no block declares one
and 0.05 s for the same vars in a single block (`design.md`, "Early errors").

Story 6 (the Module, eval, dynamic-function and function re-parse goals)
changed the one entry every goal now runs through (`parser_execute`), added
the module and eval paths to the parser, and the tokenizer's bounded range,
and, in review, gave the source's seek an ASCII fast path.
It added two cases, which have no "before": `parse-module-utf8` (one generated module of 265,626 bytes per unit) and `reparse-functions-utf8`
(5,240 functions, 195,956 bytes of function text per unit). The "before" rows
are the library at the end of story 5 (`72c6a91`) built with the same harness
(the two new cases omitted) and run in the same session, alternating with
the "after" runs. **The machine was not quiet this time:** other work was
running, the calibration spread is 6.5% to 24.8%, and several rows spread over
20%, so a difference under about 10% is not a finding. The best of nine
repeats; two runs of each.

| Story | Case | Best | Median | Spread | Throughput | Calibration (best) |
| --- | --- | --- | --- | --- | --- | --- |
| 6 before (1) | `calibration-xorshift` | 1.32 ns/step | 1.36 ns/step | 8.4% |  | 1.32 ns/step |
| 6 before (1) | `source-decode-utf8` | 2.77 ms | 2.88 ms | 21.9% | 271 MiB/s | 1.32 ns/step |
| 6 before (1) | `source-decode-utf16` | 1.74 ms | 1.81 ms | 6.9% | 807 MiB/s | 1.32 ns/step |
| 6 before (1) | `tokenize-utf8` | 10.3 ms | 10.8 ms | 6.7% | 122 MiB/s, 39.0 Mtokens/s | 1.32 ns/step |
| 6 before (1) | `tokenize-utf16` | 10.8 ms | 11.0 ms | 8.9% | 224 MiB/s, 37.2 Mtokens/s | 1.32 ns/step |
| 6 before (1) | `parse-utf8` | 31.3 ms | 32.2 ms | 35.5% | 40.0 MiB/s, 12.8 Mtokens/s | 1.32 ns/step |
| 6 before (1) | `parse-utf16` | 32.2 ms | 33.1 ms | 4.7% | 74.9 MiB/s, 12.4 Mtokens/s | 1.32 ns/step |
| 6 before (1) | `parse-checking-utf8` | 30.6 ms | 31.3 ms | 8.2% | 40.8 MiB/s, 13.1 Mtokens/s | 1.32 ns/step |
| 6 before (1) | `tree-destroy` | 0.615 ms | 0.666 ms | 23.0% | 2,035 MiB/s | 1.32 ns/step |
| 6 before (2) | `calibration-xorshift` | 1.43 ns/step | 1.45 ns/step | 6.5% |  | 1.43 ns/step |
| 6 before (2) | `source-decode-utf8` | 2.65 ms | 2.67 ms | 23.1% | 283 MiB/s | 1.43 ns/step |
| 6 before (2) | `source-decode-utf16` | 1.64 ms | 1.67 ms | 2.6% | 857 MiB/s | 1.43 ns/step |
| 6 before (2) | `tokenize-utf8` | 10.0 ms | 10.1 ms | 13.8% | 125 MiB/s, 40.0 Mtokens/s | 1.43 ns/step |
| 6 before (2) | `tokenize-utf16` | 10.3 ms | 10.6 ms | 8.5% | 233 MiB/s, 38.7 Mtokens/s | 1.43 ns/step |
| 6 before (2) | `parse-utf8` | 28.8 ms | 29.8 ms | 4.1% | 43.5 MiB/s, 13.9 Mtokens/s | 1.43 ns/step |
| 6 before (2) | `parse-utf16` | 28.7 ms | 29.0 ms | 3.2% | 84.1 MiB/s, 14.0 Mtokens/s | 1.43 ns/step |
| 6 before (2) | `parse-checking-utf8` | 27.3 ms | 28.2 ms | 34.4% | 45.9 MiB/s, 14.7 Mtokens/s | 1.43 ns/step |
| 6 before (2) | `tree-destroy` | 0.529 ms | 0.588 ms | 51.3% | 2,364 MiB/s | 1.43 ns/step |
| 6 after (1) | `calibration-xorshift` | 1.44 ns/step | 1.47 ns/step | 24.8% |  | 1.44 ns/step |
| 6 after (1) | `source-decode-utf8` | 2.75 ms | 2.93 ms | 11.3% | 273 MiB/s | 1.44 ns/step |
| 6 after (1) | `source-decode-utf16` | 1.81 ms | 2.09 ms | 26.5% | 777 MiB/s | 1.44 ns/step |
| 6 after (1) | `tokenize-utf8` | 10.4 ms | 10.5 ms | 35.7% | 120 MiB/s, 38.5 Mtokens/s | 1.44 ns/step |
| 6 after (1) | `tokenize-utf16` | 11.1 ms | 11.4 ms | 8.5% | 217 MiB/s, 36.1 Mtokens/s | 1.44 ns/step |
| 6 after (1) | `parse-utf8` | 31.6 ms | 31.8 ms | 3.2% | 39.6 MiB/s, 12.7 Mtokens/s | 1.44 ns/step |
| 6 after (1) | `parse-utf16` | 32.2 ms | 33.7 ms | 25.9% | 74.9 MiB/s, 12.4 Mtokens/s | 1.44 ns/step |
| 6 after (1) | `parse-checking-utf8` | 29.1 ms | 30.4 ms | 13.1% | 43.0 MiB/s, 13.7 Mtokens/s | 1.44 ns/step |
| 6 after (1) | `tree-destroy` | 0.496 ms | 0.561 ms | 30.2% | 2,524 MiB/s | 1.44 ns/step |
| 6 after (1) | `parse-module-utf8` | 5.8 ms | 5.9 ms | 2.6% | 43.6 MiB/s | 1.44 ns/step |
| 6 after (1) | `reparse-functions-utf8` | 32.4 ms | 33.6 ms | 7.9% | 5.8 MiB/s | 1.44 ns/step |
| 6 after (2) | `calibration-xorshift` | 1.26 ns/step | 1.40 ns/step | 17.8% |  | 1.26 ns/step |
| 6 after (2) | `source-decode-utf8` | 2.67 ms | 2.87 ms | 16.9% | 281 MiB/s | 1.26 ns/step |
| 6 after (2) | `source-decode-utf16` | 1.63 ms | 1.67 ms | 8.7% | 866 MiB/s | 1.26 ns/step |
| 6 after (2) | `tokenize-utf8` | 9.5 ms | 9.6 ms | 1.6% | 132 MiB/s, 42.1 Mtokens/s | 1.26 ns/step |
| 6 after (2) | `tokenize-utf16` | 10.0 ms | 10.0 ms | 2.3% | 242 MiB/s, 40.2 Mtokens/s | 1.26 ns/step |
| 6 after (2) | `parse-utf8` | 27.2 ms | 27.4 ms | 4.7% | 46.0 MiB/s, 14.7 Mtokens/s | 1.26 ns/step |
| 6 after (2) | `parse-utf16` | 28.2 ms | 28.4 ms | 1.7% | 85.5 MiB/s, 14.2 Mtokens/s | 1.26 ns/step |
| 6 after (2) | `parse-checking-utf8` | 26.5 ms | 26.6 ms | 4.3% | 47.2 MiB/s, 15.1 Mtokens/s | 1.26 ns/step |
| 6 after (2) | `tree-destroy` | 0.530 ms | 0.698 ms | 29.3% | 2,362 MiB/s | 1.26 ns/step |
| 6 after (2) | `parse-module-utf8` | 6.4 ms | 7.3 ms | 53.3% | 39.8 MiB/s | 1.26 ns/step |
| 6 after (2) | `reparse-functions-utf8` | 33.2 ms | 35.2 ms | 22.5% | 5.6 MiB/s | 1.26 ns/step |

The decoder, tokenizer and parser rows did not change beyond the noise: the
best `parse-utf8` is 28.8 and 31.3 ms before and 27.2 and 31.6 ms after, the
best `parse-checking-utf8` 27.3 and 30.6 ms before and 26.5 and 29.1 ms after,
and the quietest pair of runs (the second "before" and the second "after")
agree to within 6%. The module goal costs 5.8 to 6.4 ms for 265,626 bytes
(about 21 ns a byte, against about 21 ns a byte for the script seeds). Reading
one function again costs 32.4 and 33.2 ms for 5,240 functions, about 6.2
microseconds each, or 165 ns a byte of function text: the cost of reading
the range's text a second time, rescanning its tokens, and building its tree,
which is what the compiler of M3 will pay once per function it compiles on
its first call. An earlier measurement, before the ASCII path was added to the
source's seek and the tokenizer's bounded range stopped looking up a line and
column for the range's end, was about 12 microseconds each (a single run of
the whole set, 65 ms, on the same machine). No budget is asserted (X-M1-s6-2).


Story 7 (the closing story) added the cases over the real-world corpus
(`bench/bench_corpus.c`; `make bench` runs it after `bench`), the first figures
of those cases, and changed no code on a hot path (the one source change is the
result of an error arm that the ECMAScript dialect of `regex` never reaches), so
the seed-set rows are the same measurement as story 6's and are repeated only
to show the machine: this run, and the story 6 "after (2)" rows above, agree
within the noise. **The machine was not quiet** (other work was running; the
calibration spread is 23% to 33%, and several rows spread over 30%), so a
difference under about 10% is not a finding and the best of two runs is given
for the corpus cases. Compiler and machine class as above; the corpus is the
eighteen files of `tools/corpus/CORPUS.tsv` (20.38 MiB of UTF-8, 40.8 MiB as
UTF-16, thirteen Scripts and five Modules); one unit is every file once. The best of
five repeats; two runs.

| Story | Case | Best (run 1 / run 2) | Spread (run 1 / run 2) | Throughput (best run) | Calibration (best) |
| --- | --- | --- | --- | --- | --- |
| 7 | `corpus-decode-utf8` | 68.6 ms / 69.7 ms | 1.3% / 30.1% | 297 MiB/s | 1.36 / 1.54 ns/step |
| 7 | `corpus-decode-utf16` | 49.1 ms / 49.1 ms | 11.9% / 9.2% | 831 MiB/s | 1.36 / 1.54 ns/step |
| 7 | `corpus-tokenize-utf8` | 132.6 ms / 139.8 ms | 2.5% / 17.2% | 154 MiB/s | 1.36 / 1.54 ns/step |
| 7 | `corpus-parse-utf8` | 325 ms / 546 ms | 15.5% / 2.4% | 62.7 MiB/s | 1.36 / 1.54 ns/step |
| 7 | `corpus-parse-utf16` | 337 ms / 475 ms | 10.8% / 17.6% | 121 MiB/s | 1.36 / 1.54 ns/step |
| 7 | `corpus-parse-checking-utf8` | 267 ms / 321 ms | 11.1% / 2.9% | 76.2 MiB/s | 1.36 / 1.54 ns/step |
| 7 | `corpus-tree-destroy` | 3.6 ms / 3.2 ms | 37.3% / 37.1% | 6.4 GiB/s | 1.36 / 1.54 ns/step |

The two parse runs differ by 40% on a spread of 2% in the second, which is the
machine (the first run's parse figure is the one to read; a quiet run is to be
taken before a budget is derived). A parse of the corpus takes about 15 ns per
byte, five times the 3 ns the decoder takes and about twice the tokenizer's 6;
the checking builder saves 18%, which is what bodies cost over real code
(minified files have the most of them). **Peak memory**, which is what the
numbers above cannot show and is exact: through the metering allocator, the
most bytes held at once while one file is processed, at the largest file
(TypeScript's compiler, 9,112,572 bytes): the decoder 9.3 MB (1.0 byte per
source byte; 18.4 MB from UTF-16), the tokenizer the same (its source only), a
tree-builder parse 81.1 MB (8.9 bytes per source byte there; the worst ratio is
41.0, in lodash's minified build, where one node of a dense program is a
larger share of its few bytes; 90.2 MB from UTF-16), the checking builder 34.0
MB (12.4 per byte at worst). The process's resident-size high-water mark above
the text of that file, in a fresh process per case: decode 9.0 MiB (17.9 from
UTF-16), tokenize 9.2, parse 75.8 (84.1 from UTF-16), checking 28.5. No budget
is asserted.

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

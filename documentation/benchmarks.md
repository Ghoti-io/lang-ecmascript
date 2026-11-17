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

Creating a source decodes every code point and indexes its lines, which is
the decoder's whole cost. The seed set is the three files under
`bench/seeds/`: `ascii.js` (plain ASCII script), `multibyte.js` (2-, 3- and
4-byte sequences) and `terminators.js` (CRLF, CR and LF line ends). Each seed
is repeated up to 256 KiB at setup so one unit is long enough to time; setup
is not timed. Cases over a real-world corpus are separate cases, added when
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

These first figures are noisy (the calibration spread is 24.6%, over the 10%
line) because the machine was in use; they are a first measurement, not a
budget, and are to be retaken on a quiet machine before any budget is
derived from them. A unit is three sources of about 256 KiB each.

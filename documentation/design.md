# Design

**Status:** Design, with the scaffolding, source text and tokenizer built. The
library registers an ECMAScript engine with a runtime-core context, reports
its version and target edition, takes source text (a validated, owned copy
with exact locations, under a size cap) and reads it into tokens by the
whole lexical grammar. It does not yet parse or run any script. Everything below the "What exists" section describes intended
work, and each part moves into "What exists" in the commit that builds it.

`lang-ecmascript` is an ECMAScript (JavaScript) engine for embedding: a
library that a host program links to run script under budgets it sets. It
is built on the suite's language runtime: `runtime-core` for the execution
context and the guest stack, `runtime-heap` for the collected heap,
`runtime-jit` for compiled code and `runtime-debug` for debugging. The same
runtime runs the suite's other languages, so a script, a WebAssembly module
and a Tang template can share one context, one set of budgets and one
debugger.

## The hosts it is for

The engine is meant to sit underneath several kinds of program:

- **A web browser.** Page script, with the DOM and CSSOM bound as host
  objects. This is why Annex B is in scope and why the host API has to be
  able to describe host-defined objects (WebIDL interfaces) cheaply.
- **A desktop application shell** in the manner of Electron: page script and
  privileged application script in separate realms or contexts, with the
  host deciding what each may reach.
- **A standalone runtime** in the manner of Node.js or Deno: a command-line
  host with modules loaded from files and the network, and a standard library
  of its own.
- **Any application that wants scripting**: configuration, plugins, rules,
  automation, where untrusted code must be stopped rather than trusted.

None of these is part of the library. The engine implements ECMA-262: the
language and its built-in objects. Everything else a JavaScript programmer
thinks of as "JavaScript" - `setTimeout`, `console`, `fetch`, the DOM, `fs`,
`process`, the event loop itself - is defined by a host specification (HTML,
WHATWG, Node's API) and is the host's to provide through the hooks below. A
host library for each of the programs above is separate work.

## The target

**ECMA-262, 17th edition (ECMAScript 2026)**, approved by the Ecma General
Assembly on 30 June 2026, with **Annex B** (additional ECMAScript features for
web browsers). `GLECMA_ECMA262_EDITION` and `GLECMA_ECMA262_YEAR` in `core.h`
state it, and `glecma_ecma262_edition()` reports it at run time.

- The edition is **pinned**. A feature that is in a later draft and not in
  the 17th edition is not implemented ahead of time, however settled it
  looks. Moving to a later edition is one deliberate change: the header
  constants, the pinned conformance suite and this document together.
- **ECMA-402** (the internationalization API, `Intl`) is a separate standard
  and is not in scope. The locale-sensitive methods ECMA-262 defines with a
  fallback (`toLocaleString` and its relatives) take that fallback.
- **Temporal** is not in the 17th edition and is not implemented.
- **Conformance is measured by test262**, the official conformance suite,
  pinned to a commit that matches the edition. A test is in scope, or out of
  scope by its feature flags with a reason recorded beside it. An
  out-of-scope test is reported as such and never counted as a pass, and a
  skip cannot hide a test whose features are all in scope. The suite is
  fetched by a tool into an ignored directory and verified against a pinned
  checksum, never copied into this repository.

Where the specification leaves a choice to the implementation
(implementation-defined or host-defined behavior), the choice is named in the
documentation and tested as a choice.

## What exists

| Piece | Where | What it does |
| --- | --- | --- |
| Result vocabulary | `core.h` | The suite's shared nine results, `glecma_result_string` |
| Version and edition | `core.h`, `libver.h` | Generated version macros, the version functions, the target edition |
| Allocator | `allocator.h` | cutil's allocator under this library's name |
| Engine descriptor | `engine.h` | The descriptor runtime-core uses to recognize ECMAScript frames, and idempotent registration with a context |
| Syntax result, locations, failure record | `core.h` | `GLECMA_ERR_SYNTAX` appended after the shared nine; `GLECMA_Location`; `GLECMA_Failure`, which every parse call writes |
| Limits | `limits.h` | `GLECMA_Limits` and `glecma_limits_default`; the source, token and literal-length caps (`source_units`, `tokens`, `literal_length`) |
| Rule codes | `core.h` | `GLECMA_Rule`, the append-only codes of the rules a syntax error can break, with `glecma_rule_name` and `glecma_rule_clause` |
| Parse options, feature set | `options.h` | `GLECMA_ParseOptions` (the limits and a feature set) and `GLECMA_FeatureSet` (`glecma_feature_set_add`, `glecma_feature_set_has`); a set naming a feature the build lacks is refused |
| Unicode version | `core.h` | `GLECMA_UNICODE_VERSION` and `glecma_unicode_version()` |
| Tokenizer | `src/lex/` (internal) | The whole lexical grammar, with Annex B, in each input element goal; reached through two internal entries |
| Source text | `source.h` | `GLECMA_Source`: UTF-8 or UTF-16 text copied, validated and checkpointed at creation; counts, the offset-to-location query, a dump, `glecma_source_retain` and `glecma_source_release` (the holders are counted atomically; the last release frees) |
| Benchmark harness | `bench/`, `benchmarks.md` | `make bench`, a smoke run and an empty-`bench/` gate in `make test`, a calibration case and the source decoder's throughput; no budget until a case has a recorded first measurement |

The descriptor's callbacks are all NULL, which runtime-core reads as "the
engine has nothing to say": every slot is raw, frames have no source
location, and there are no scopes. The interpreter fills them in as its
frames acquire slots, scopes and locations.

Registration is idempotent where `grcore_engine_register` is not: a realm, a
module loader and a debugger can each make sure the engine is present
without agreeing which goes first. The ownership and running-state checks are
applied before the lookup as well as by runtime-core, so a second call
refuses what the first would have refused.

## Source text

Every parse starts from a `GLECMA_Source`: an immutable copy of the text the
host handed in, validated once, from which any offset's line and column can
be had cheaply. It is what the tokenizer reads and what every location in a
token, a tree node or a failure refers to.

**Input.** UTF-8 bytes or UTF-16 code units in host byte order, with an
allocator (NULL for the default) and limits (NULL for the defaults).
ECMA-262 source text is a sequence of code points, and the two encodings
reach it differently:

- UTF-8 must be well-formed by the Unicode Standard's Table 3-7: no overlong
  form, no encoded surrogate, nothing above U+10FFFF and no truncated
  sequence. Anything else is `GLECMA_ERR_CORRUPT`, and the failure's location
  is the first byte of the first ill-formed subsequence - the start of the
  maximal subpart section 3.9 defines - with its line and column. The
  decoder's segmentation is tested against Table 3-8's example, and against
  an oracle built from the encodings of every scalar value rather than from
  the table.
- UTF-16 is decoded as ECMA-262's UTF16DecodeString decodes it: a surrogate
  pair is one code point and an unpaired surrogate is a code point of its
  own, because a string literal may hold one. UTF-16 input therefore never
  fails to decode.

Nothing is stripped or replaced. A leading U+FEFF is ECMA-262 white space and
stays a code point of the source; a leading `#!` stays for the grammar to
read as a hashbang comment where the goal allows one. A NULL text with a
non-zero length, or a NULL output, is `GLECMA_ERR_INVALID`; an empty source
is valid.

**Locations.** A `GLECMA_Location` is an offset in the input's own units
(bytes or code units), a 1-based line and a 0-based column. Lines end at
ECMA-262's line terminators and at nothing else: LF, CR, CRLF (one
terminator), U+2028 and U+2029. The LF of a CRLF is on the CR's line, one
column on. Columns count UTF-16 code units in both encodings, because source
maps (v3) and browser developer tools count them that way: a UTF-8 host still
gets columns a debugger front end can use, and a code point above U+FFFF is
two columns wide. The end of the input is a location; an offset inside a
multi-unit code point is not, and is refused.

**Representation.** The source keeps the text in its input encoding, plus
one *checkpoint* per 1024 input units: the cursor state (offset, line,
column) at the first code-point boundary at or after each multiple of 1024.
A location query divides to find its checkpoint and walks forward at most one
interval and one code point, so a query costs O(1024) units of decoding
whatever the source's length. A source costs its copy (one byte per UTF-8
unit, two per UTF-16 unit), plus 24 bytes of checkpoint per 1024 units on a
64-bit host (about 2.3% of a UTF-8 copy, 1.2% of a UTF-16 one), plus one
fixed-size struct. The cursor that walks it never looks behind
itself: CR is the only terminator whose meaning depends on a neighbor, and it
is resolved by looking one unit ahead for LF, which is one unit in both
encodings. The same cursor builds the checkpoints at creation and answers
queries afterwards, and a differential test walks every source in its corpus
code point by code point and compares each position with the query and with
a reference model written from the line-terminator rules.

Alternatives rejected:

- **An array of decoded code points.** The tokenizer would index it directly,
  but it costs four bytes per code point - four times a UTF-8 input, twice a
  UTF-16 one - for a convenience a cursor gives for free, and it is a second
  copy of the text to keep consistent with the first.
- **A table of every line start.** Exact lookup by binary search, but its
  size is the number of lines, which a hostile input chooses: a source of
  newlines costs eight bytes per input byte. Checkpoints cost a fixed
  fraction of the input whatever its shape, and the bounded walk is cheap.
- **Stripping a leading byte order mark,** as a file reader might. ECMA-262
  defines U+FEFF as white space in source text, so it is the grammar's to
  skip, and removing it would shift every offset the host computes against
  its own buffer.
- **Replacing ill-formed UTF-8 with U+FFFD.** It would make a corrupt input
  parse as a different program than the bytes say, and an overlong encoding
  is the classic way to slip a character past a check that ran on the bytes.

**Ownership and threads.** The text is copied, and it is the copy that is
validated and scanned, never the caller's buffer: a thread still writing that
buffer during the call cannot get bytes past validation other than the ones
stored. The caller's buffer may be freed once the call returns, and the allocator is copied into the source
(its context must outlive it). Every allocation goes through that allocator;
the output is written only on success, and every failure, including an
allocation failing partway, frees everything already allocated. A source is
immutable once created and has no process-wide state behind it, so any
number of threads may query one at once and sources on separate threads are
independent.

## Limits

`GLECMA_Limits`, initialized with `GLECMA_LIMITS_INIT` or filled by
`glecma_limits_default`; NULL limits mean the defaults. The struct is
size-first: its first member is the size the caller was compiled against, the
library reads only the members within it and takes the default for any later
one, and fields are only added at the end, so a caller built before a field
existed keeps working (a test is built against a shorter struct to show it).
The names a failure reports (`source_units`, ...) are kept in an append-only
registry, `tools/limit-names.txt`, and `make check-limit-names` fails when one
is removed or renamed. A field of zero is a cap of zero, not a request for the default.
Each cap is checked before the allocation it bounds, and exceeding one is
`GLECMA_ERR_LIMIT` with the field's name in the failure record - never a
truncated result and never `GLECMA_ERR_OOM`. A field is added in the change
that enforces it, with its default and a test that drives it one past the
cap.

| Field | Caps | Default | Why |
| --- | --- | --- | --- |
| `source_units` | Input units of one source | 64 Mi (67,108,864) | Real bundles reach tens of megabytes, and a source costs about 1.03 times its input, so the default admits them while bounding what a hostile host-supplied string can make the library allocate: a copy of at most 64 MiB of UTF-8 or 128 MiB of UTF-16, plus its checkpoints. A host raises it deliberately |
| `tokens` | Tokens one tokenizer run produces (the end of input is not one) | 48 Mi (50,331,648) | See "Measured defaults" below |
| `literal_length` | Code points in one identifier, string literal, template part or regular expression literal, in the source text, delimiters included | 16 Mi (16,777,216) | See "Measured defaults" below |

The nesting and node caps join with the parser that enforces them.

**Measured defaults.** Both are measured on one machine class (a 12-thread
x86-64 laptop, GCC 16.2 `-O2`) over thirteen widely deployed JavaScript files
(jQuery minified and not, Underscore, PrismJS, MathJax's configuration
bundles, and small application scripts) and over two synthetic extremes.

- *Tokens.* The unminified files take 0.07 to 0.16 tokens per input byte and
  the dense ones (minified code, and a data file) 0.34 to 0.47 (the densest,
  jQuery's minified build, is 0.470). A source of the default `source_units` cap (64 Mi) therefore holds
  at most about 31.5 million tokens of real code, and the default of 48 Mi
  admits it with a margin of 1.6 times. The synthetic extreme is source of
  one-unit tokens (`a+a+a+...`, 1.0 token per unit), which no author writes
  and which the default refuses at 48 Mi tokens, three quarters of the source
  cap. The cap exists for a host that raises `source_units` far above the
  default, or one that wants fewer tokens than bytes. A source that reaches
  the cap costs the tokenizer about 0.65 seconds per 32 Mi tokens.
- *Literal length.* The longest string, template or regular expression
  literal among the files is 1,348 code points, far below any cap worth
  having; the sample holds no file that inlines binary data, which is where
  a single literal reaches megabytes. The default is therefore chosen from
  cost, not from a measured maximum: 16 Mi code points is a literal of tens
  of megabytes of data, larger than any the sample suggests a real source
  holds, and still cheap to bound. At the cap a
  literal costs 22 ms to scan and, when its value is asked for, 22 ms and
  32 MiB (two bytes per code point) to decode, so the cap bounds what one
  hostile literal can make the library spend. These figures come from a
  small, local sample, not from a corpus chosen for the purpose; the
  defaults are re-measured when the real-world corpus is pinned.

## The failure record

`GLECMA_Failure` is written on every return of a call that takes one, when
the caller passes it, success included. It is size-first like the limits
(`GLECMA_FAILURE_INIT`), and the library writes only the members within the
size it is given. It holds the result, a static
English message or NULL, a location, for `GLECMA_ERR_LIMIT` the static name
of the field, for `GLECMA_ERR_SYNTAX` the rule code that was broken, and for
`GLECMA_ERR_UNSUPPORTED` from a feature set the name of the first feature the
build lacks. The location is meaningful for `GLECMA_ERR_CORRUPT` (the first
ill-formed byte) and `GLECMA_ERR_SYNTAX` (a point inside the offending
construct); otherwise it is zero. Every string pointer in it is static, so
the record needs no freeing; the one piece of the caller's text it carries,
a feature name, is copied into a fixed array, so it does not depend on the
caller's set outliving the call. The members after `limit` (`rule`, a
padding word, `feature`) were appended in the order given, so a caller built
against the shorter record is not written past.

The rule codes, `GLECMA_Rule`, are an append-only enumeration with explicit
values: a host stores and compares the numbers, so a code is never
renumbered, reused or removed, and a test pins each code's value. Each code
has a stable name and the clause of ECMA-262 it comes from
(`glecma_rule_name`, `glecma_rule_clause`). The clause numbers are the 16th
edition's and are checked against the 17th when the clause concordance is
committed. The lexical rules are the first twenty-four; the parser's and the
early errors' rules are appended after them.

## The Unicode version

**One version for the whole engine.** Identifiers (ID_Start and ID_Continue)
and white space (the Space_Separators) in the tokenizer, and later case
mapping, normalization, collation and the property escapes of regular
expressions, all read tables of the same Unicode Character Database version,
which is 17.0.0. It moves only when the edition does, together with the
`unicode` library, the regular expression library and the collation data, as
one change. It is never moved alone, because a version that differs between
two parts of the engine makes a name valid to one and invalid to the other.

`GLECMA_UNICODE_VERSION` is the pin, and `glecma_unicode_version()` is the
version in use: the one `unicode` reports (`guni_ucd_version()`). A test
requires the two to be equal and equal to what `unicode` answers, so a build
against a `unicode` of another version fails its tests rather than lexing
differently. A check that `unicode` and the regular expression library agree
on the version joins when the second dependency does.

Identifier characters are classified by `unicode`'s `ID_Start` and
`ID_Continue` properties, which are what ECMA-262's `UnicodeIDStart` and
`UnicodeIDContinue` name, with `$`, `_`, ZWNJ and ZWJ added as the grammar
says. A test classifies every code point, raw and through a `\u{...}` escape,
against `unicode`'s own answer.

## The parse options and the feature set

Every entry that tokenizes takes `GLECMA_ParseOptions`: a size-first struct
holding the limits and a feature set, filled by `glecma_parse_options_default`
or `GLECMA_PARSE_OPTIONS_INIT`. A member beyond the caller's declared size
takes its default, so a caller built before a member was added keeps working,
and a test is built against a header-only struct. The goal and its context
join at the end of the struct with the parser.

A **feature set** (`GLECMA_FeatureSet`) names features that are finished at
TC39 and not in the pinned edition, so that one can be built and measured
before an edition turns it on. It is a plain value (sixteen names of up to 47
characters each, size-first, no allocation). The pinned edition is what an
empty set accepts. **This build defines no feature, so every non-empty set
is refused**, before the entry reads a token, with `GLECMA_ERR_UNSUPPORTED`
and the first feature named in the failure record. The alternative, ignoring
a feature the build lacks, would hand a caller who asked for a feature the
plain edition's parse with no sign that it was not what they asked for.
Adding a name to a set does not check it against the build: the refusal is
the entry's, so one set can be carried unchanged to an engine that has the
feature.

## The tokenizer

ECMA-262 has no single token stream. The lexical grammar is a set of
*input element goals*, and the syntactic grammar chooses one at each point:
where a `/` is division and where it begins a regular expression, where a
`}` is a punctuator and where it continues a template. Only a parser knows,
so the tokenizer is **pulled**: `glecma_lexer_next` reads one token under the
goal its caller names (division, regular expression, template tail, or the
combination), and a caller that must look ahead copies the lexer, which is a
plain value, and restores the copy. A test driver guesses goals from the
previous token (it is exact only where the guess is right and is never used
by a parser); the parser will name them.

**What a token is.** A kind (each punctuator has its own), a keyword (when a
name spells a reserved, strict-mode or contextual word), flags, and a start
and end location; it points into the source by offset and holds no text and
no allocation. What a token *means* is produced on demand by
`glecma_token_value`: a name with its escapes decoded, a string literal's
value (a lone surrogate stays one), a template part's cooked or raw value.
Numeric and BigInt literals are not evaluated here: the compiler computes
their values, with the maths library, where the arithmetic belongs.

**Locations.** The scanners work in offsets and bring a token's line and
column up to date once, at its end, by walking the units it covers. An error
inside a token locates its offset from the token's start. A test compares
every token's start and end with the source's own location query over a
corpus in both encodings, so the two ways of counting cannot drift apart.

**What is validated** (each with a rule code and a location inside the
construct): every punctuator, name (with `\u` escapes, each of which must be
an identifier character), numeric literal (decimal, hexadecimal, octal and
binary, BigInt, numeric separators, Annex B's legacy octal and leading-zero
decimal forms, and what may follow), string literal and each of its escapes,
template part and regular expression literal's shape and flags. Comments are
skipped: single-line, multi-line (a line terminator inside one counts for
automatic semicolon insertion), the hashbang at the very start, and, in the
Script goal only, Annex B's HTML-like comments (`<!--` anywhere, `-->` at the
start of a line or after only comments). In the Module goal they are not
comments, so `a <!-- b` is `a < ! -- b`.

**Strictness.** Legacy octal literals and escapes, `\8`, `\9` and
leading-zero decimals are errors in strict code. The tokenizer is told the
strictness it starts in and can be told again (a directive prologue makes a
function strict after its first tokens were read), and the tokens it already
returned carry flags (`LEGACY_OCTAL`, `NON_OCTAL_DECIMAL`) so that the parser
can refuse a literal or an escape that came before the directive.

**Templates.** A template's cooked value is undefined when it holds an
invalid escape, and that is not an error in a tagged template. The tokenizer
cannot know whether a template is tagged, so it does not refuse: the token
says its cooked value is undefined, records the rule of the first invalid
escape and where it is, and keeps the raw value intact. The parser reports it
for an untagged template. Cooked and raw values read CR and CRLF as LF; a
line separator and a paragraph separator are kept.

**Identifiers and reserved words.** A name token records the word it spells
after its escapes are decoded, and a flag when it was written with an escape.
Where the grammar needs an Identifier, the parser asks
`glecma_token_identifier_check`, which refuses a reserved word, a word
reserved in the context (strict code, a generator, a module or async
function) and a reserved word spelled with an escape, with a rule code for
each. An escaped word is a name and never the keyword, so it cannot be used
where a keyword is required. A name after a dot is never asked about.

**Limits.** The token cap is checked before a token is scanned; the
literal-length cap is checked as a literal is scanned, so a hostile literal
costs no more than the cap. Neither allocates: the tokenizer allocates
nothing, and a test counts the allocator's requests while a source is read to
its end.

### The internal built-in mode

The engine's own built-ins will be written in a dialect of strict ECMAScript
with one more token: an intrinsic is spelled `%Name`. The tokenizer has a mode
bit for it. With the bit set, `%IdentifierName` is one token (an intrinsic's
name, without escapes) where the parser expects a primary expression, which
is where a `/` would begin a regular expression, and nowhere else. The bit is
set only by an internal entry, which no public header declares, and no member
of the parse options selects it, so user source can never spell an intrinsic:
in every public goal `%` is the remainder operator, and `%Name(x)` is the
tokens `%`, `Name`, `(`, `x`, `)` it always was. A test checks both, and that
the public entry refuses the bit if it is passed.

## Fuzzing

Two harnesses, `tests/fuzz/fuzz_tokenizer.c` and `fuzz_source.c`, are
libFuzzer targets (`make fuzz-tokenizer`) and, with a replay driver, plain
programs (`make fuzz-replay`) that run every file of the seed corpus through
the same entry point. The replay needs no libFuzzer, is part of `make test`,
and runs again under ASan and UBSan in `make test-asan`, so an input a
campaign once found is a failing test. The first byte of an input chooses the
encoding (UTF-8 or UTF-16), strictness, the goal, the built-in entry, the
limits and how goals are chosen; the rest is the source. The tokenizer
harness checks that the result is one a tokenizer may return, that a failure
record agrees with it, that tokens lie inside the source in order and at the
positions the source reports, that a saved lexer reads a token twice the same
way, and that the same input fails the same way. The source harness
transcodes a well-formed UTF-8 input with its own decoder and requires the
two encodings to agree on code points, lines, locations and tokens. The
seeds are regenerated by `tests/fuzz/make-seeds.py`.

## Layering on the runtime

**runtime-core** owns the execution context: the fuel, memory and stack
budgets, pause and resume, the requests other threads post, and the guest
stack. ECMAScript calls push frames on that guest stack; a call from script to
script never recurses on the C stack, so a deep recursion in script is a
budget the host set, not a native stack overflow. A budget that runs out
pauses the context, which the host can resume, extend or abandon; script
cannot catch it. Every frame names this engine's descriptor, which is how the
collector, the debugger and a profiler read it without knowing the language.

A call between languages - script calling a WebAssembly export, or a Wasm
import implemented in script - goes through runtime-core's boundary-call
interface, never by one engine reaching into another's frames. A value
crosses as a handle, not a pointer, and an exception crosses as an opaque
foreign exception that keeps its identity.

**runtime-heap** holds every ECMAScript object: ordinary objects, functions,
environments, strings, shapes, and the job and promise records. Collection is
precise: the engine describes each object type's references, and every root
(frame slots, handles, the realm's intrinsics) is reported. Object identity
comes from the heap's stable identities, never from addresses, because a
moving collector changes addresses. `WeakMap`, `WeakSet`, `WeakRef` and
`FinalizationRegistry` are built on the heap's weak cells and ephemerons.

**runtime-jit** comes after the interpreter, as a baseline tier that leaves
for the interpreter (deoptimizes) at any operation it does not compile. The
interpreter is the reference: compiled code must agree with it on results,
fuel, and the frames a debugger or collector sees at every poll.

**runtime-debug** attaches to a paused context through the engine
descriptor. Which wire protocol the engine speaks to developer tools (the
Debug Adapter Protocol the runtime already serves, the Chrome DevTools
Protocol a browser's tools expect, or both) is decided when the debugger is
built.

## The pipeline

1. **Source text.** A script or module arrives as UTF-8 or UTF-16 and is
   kept in that encoding, read as code points through a cursor (see "Source
   text" above). String values are sequences of UTF-16 code units, as the
   specification defines them, including lone surrogates.
2. **Tokenizer and parser.** A hand-written recursive-descent parser over
   the specification's grammar, with its cover grammars (arrow parameters,
   destructuring assignment), automatic semicolon insertion, the regular
   expression/division ambiguity, template literals, and the strict, sloppy
   and module goals. Nesting is bounded by a limit, so hostile input is
   refused rather than exhausting the C stack. Early errors are reported
   before any code runs, with a source location.
3. **Bytecode.** The parser's tree compiles to this library's own bytecode:
   a register or stack machine chosen when the compiler is designed, with a
   poll at every function entry and loop head so that budgets, pauses and
   the debugger have somewhere to stop.
4. **Interpreter.** Executes bytecode on runtime-core frames, with inline
   caches over object shapes for property access.
5. **Baseline JIT**, later, as above.

## Values

How a value is represented in a frame slot and in the heap (NaN-boxing,
pointer tagging or another scheme) is decided with the interpreter. The
constraints it must meet are already fixed:

- A Number is an IEEE 754 double, and the bits the interpreter computes must
  equal the bits compiled code computes. The library and its tests build with
  `-ffp-contract=off` and never `-ffast-math`, and a gate checks every flag
  set.
- A slot holding a reference must be readable by runtime-core's conservative
  decoder and reported precisely to runtime-heap.
- Host-defined objects (a DOM node, a file handle) are values of the same
  representation with a host-described type, so that a host binding is not a
  second object model.

## The host API, as planned

The embedding API follows the specification's own structure, so that a host
can be checked against ECMA-262 rather than against this library's habits:

- **Agent** - one runtime-core context. An agent cluster (a page and its
  workers, a server and its worker threads) is a context group.
- **Realm** - a global object and its intrinsics. A host can create several
  realms in one agent (iframes, a privileged shell realm beside a page).
- **Script and module records** - parse, link and evaluate as separate steps,
  so that a host can report early errors before running anything and load a
  module graph asynchronously.
- **Host hooks**, each one of the specification's host-defined operations:
  enqueueing promise jobs (the host owns the event loop and decides when the
  job queue drains), loading an imported module, tracking unhandled promise
  rejections, deciding whether a string may be compiled (`eval` and
  `Function` under a content security policy), `import.meta` properties,
  `FinalizationRegistry` cleanup scheduling, and the clock and random source
  (`Date.now`, `Math.random`), which the host supplies so that a run can be
  deterministic and replayable.
- **Values across the API** are handles in scopes that the collector sees,
  never raw pointers into the heap.
- **Results.** A failure of the API itself (a bad argument, an exhausted
  allocator, an exceeded limit) is a `GLECMA_Result`. An exception thrown by
  script is not a library failure: it will be reported by a result appended
  for it, with the thrown value available to the host as a handle. A script's
  syntax or early error is `GLECMA_ERR_SYNTAX`, so that "this is not valid
  ECMAScript" is distinct from ill-formed text (`GLECMA_ERR_CORRUPT`) and from
  a caller's mistake (`GLECMA_ERR_INVALID`).

## Built-ins and the suite libraries they will use

Each of these becomes a declared dependency in the commit that first uses
it, not before:

| Built-in | Library | Why |
| --- | --- | --- |
| Identifiers, `String.prototype.normalize`, case mapping | `unicode` | `ID_Start`/`ID_Continue`, normalization forms and case tables, from the Unicode version the edition names |
| `RegExp` | `regex` | Its ECMAScript front end, legacy and Unicode modes |
| `Number.prototype.toString`, `parseFloat`, numeric literals | `math` | Shortest round-tripping decimal and correctly rounded decimal-to-double |
| `Math` | `math` | The elementary functions, with the bit-exactness rule above |
| `Date` | `chron` | Calendar arithmetic and local time zone offsets |
| Objects, strings, environments | `runtime-heap` | The collected heap |
| Compiled code | `runtime-jit` | The baseline tier |
| Debugging | `runtime-debug` | Optional; the debugger attaches through the descriptor |

`JSON`, the string algorithms, and the abstract operations are written here
from the specification.

## How it will be tested

- **test262**, pinned, as above: the conformance score is the in-scope tests
  that pass, with the out-of-scope tests listed by reason.
- **Unit tests** that cite the clause they test, so that a test states a
  requirement rather than the code's current behavior.
- **Fuzzing** of the tokenizer, the parser and the compiler with libFuzzer,
  each harness with a seed corpus that `make test` replays.
- **A differential against another engine's behavior**: the outputs of an
  independent engine for the same program, used as an oracle. Its source is
  never read; only its answers are compared.
- **GC torture and a moving stack**: every suite also runs with a collection
  at every allocation and a guest stack that moves on every push, so that a
  missing root fails a test instead of corrupting memory later.
- **Interpreter against compiled code**, once the JIT exists, compared frame
  by frame at every poll.
- **Sanitizers**: ASan with UBSan, TSan and Valgrind, each a make target.

## Implementation rule

The engine is written from the specification. Nothing is copied, ported,
transcribed or closely paraphrased from another JavaScript engine, whatever
its license, and no other engine's source is read while this one is written.
Another engine's behavior may be used as a test oracle, as above.

## Errors

| Result | Meaning here |
| --- | --- |
| `GLECMA_ERR_INVALID` | A bad caller argument: NULL where a pointer is required, a NULL text with a non-zero length, an allocator with a NULL function, an offset that is past the end or inside a code point, a context the caller does not own, a context that is running, or one that does not hold the engine (`glecma_engine_find`) |
| `GLECMA_ERR_LIMIT` | A budget or limit refused the operation: a `GLECMA_Limits` field, named in the failure record; for registration, the context's memory budget refused the engine table |
| `GLECMA_ERR_IO` | `glecma_source_dump`'s stream refused a write or a flush |
| `GLECMA_ERR_CORRUPT` | The text is not well-formed UTF-8; the failure record locates the first ill-formed byte |
| `GLECMA_ERR_OOM` | The allocator returned NULL. Source creation takes an allocator, not a context, so a runtime-core context's counting allocator refusing for the context's memory budget is reported here too; a host that passed one tells the two apart through the context |
| `GLECMA_ERR_INTERNAL` | The library's own invariant failed, including a runtime-core result this library has no counterpart for |
| `GLECMA_ERR_SYNTAX` | Appended after the shared nine: well-formed source text that is not valid ECMAScript for its goal. Nothing returns it until the parser exists |

Outputs are written only on success, and a refusal changes nothing.

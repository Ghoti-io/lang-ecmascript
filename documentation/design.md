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
| Limits | `limits.h` | `GLECMA_Limits` and `glecma_limits_default`; the source, token, literal-length, nesting, node and native-stack caps (`source_units`, `tokens`, `literal_length`, `nesting`, `nodes`, `native_stack_bytes`) |
| Rule codes | `core.h` | `GLECMA_Rule`, the append-only codes of the rules a syntax error can break, with `glecma_rule_name` and `glecma_rule_clause` |
| Parse options, feature set | `options.h` | `GLECMA_ParseOptions` (the limits, a feature set, the goal, the context and the eval context), the goals and context flags, `GLECMA_EvalContext`, and `GLECMA_FeatureSet` (`glecma_feature_set_add`, `glecma_feature_set_has`); a set naming a feature the build lacks is refused. The names the build defines are the rows of `src/core/features.def`, which is empty |
| Goals beyond a source | `goals.h`, `src/parse/dynamic.c`, `src/parse/reparse.c` | `glecma_parse_dynamic_function` and `glecma_parse_function` with `GLECMA_FunctionContext`: CreateDynamicFunction's parsing steps, and one function read again from its range (see "The dynamic-function goal" and "The function re-parse goal") |
| Unicode version | `core.h` | `GLECMA_UNICODE_VERSION` and `glecma_unicode_version()` |
| Tokenizer | `src/lex/` (internal) | The whole lexical grammar, with Annex B, in each input element goal; reached through two internal entries |
| Source text | `source.h` | `GLECMA_Source`: UTF-8 or UTF-16 text copied, validated and checkpointed at creation; counts, the offset-to-location query, a dump, `glecma_source_retain` and `glecma_source_release` (the holders are counted atomically; the last release frees) |
| Parser and tree | `tree.h`, `src/parse/` | `glecma_parse`: a Script, a Module, or direct or indirect eval code into a read-only tree (`GLECMA_Tree`) with a walker, source ranges, a dump and one counted reference to its source; the builder interface with the tree builder and the checking builder; declaration and reference events (see "The parser") |
| Input stream | `stream.h` | `GLECMA_Stream` over memory, a file or a host's reader, and `glecma_source_create_stream` |
| Benchmark harness | `bench/`, `benchmarks.md` | `make bench`, a smoke run and an empty-`bench/` gate in `make test`, a calibration case, and the decoder, tokenizer, parser, tree destruction, module and function re-parse cases over small seed files and, in `bench_corpus.c`, over the real-world corpus with each case's peak memory; no budget is asserted, and the first figures are in the history |
| Real-world corpus | `tools/corpus/`, `tools/check-corpus.py`, `bench/bench_corpus.c` | Eighteen files of twelve packages fetched by hash and never committed; `check-corpus` parses each as its goal, in both encodings and with both builders, and reads every function again (see "The real-world corpus") |
| Node differential | `tools/check-node-differential.py`, `tools/node-oracle/`, `tools/gen-programs.py`, `tests/data-node-differences.tsv` | The accept/refuse decision compared with a pinned Node over test262, the corpus, the fuzz seeds and generated programs, every disagreement classified (see "The Node differential") |
| Hostile-input tests | `tests/unit/test_hostile.cpp`, `tests/fuzz/` | The doubling, depth, limit, allocation-failure and thread sweeps, and five fuzz harnesses with a dictionary and a campaign target (see "Hostile input" and "Fuzzing") |
| Planted defects | `tools/planted/`, `tools/check-planted.sh` | The registry of defects each gate must be seen to fail on, and the target that applies each to a scratch tree |
| test262 gate | `tools/check-test262.py`, `tests/t262/`, `tools/test262/` | The pinned corpus, fetched and checked against a list of checksums; the classification and tag tables; the parse score in `make test` with a pending ledger (see "test262") |
| Clause concordance | `documentation/clause-concordance.tsv`, `tools/ecma262/`, `tools/check-clauses.py` | The 16th-to-17th clause numbers, built from the editions' text, and a gate that fails a cited clause the 17th lacks |

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
| `nesting` | Levels of syntactic nesting: a statement, an assignment expression, a prefix operator, a pattern or an `**` operand inside another | 256 | See "Recursive descent and the stack" |
| `nodes` | Nodes one parse builds (the checking builder counts the nodes it skips as the tree builder counts the ones it builds, so both refuse the same input) | 16 Mi (16,777,216) | See "Measured defaults" below |
| `native_stack_bytes` | Native stack, in bytes, that the caller grants one parse beyond the point of the call | 1 MiB (1,048,576) | See "Recursive descent and the stack" |

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
  cap. The pinned corpus's densest file is 0.536 tokens per byte (core-js's
  minified bundle), so a 64 MiB source of it holds 36 million tokens and the
  default admits it with a margin of 1.33. The cap exists for a host that raises `source_units` far above the
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
  small, local sample; the pinned real-world corpus (see "The real-world
  corpus") was measured the same way afterwards and changes none of them: its
  longest literal is 22,288 code points, and no file inlines binary data.
- *Nodes.* Real source builds 0.08 nodes per byte (commented, formatted code)
  to 0.32 (minified code, the densest measured), and one node, with its share
  of the tree's names, texts and events, takes 85 to 110 bytes. The default
  of 16 Mi nodes is therefore about 1.7 GB of tree at its largest, and admits
  a 50 MB minified file or a 200 MB formatted one. A source of the default
  `source_units` cap is not guaranteed to fit: a minified 64 MiB file builds
  about 21 million nodes (on the pinned corpus's densest file, 0.401 nodes per
  byte, about 27 million). That is a deliberate bound on memory, stated rather
  than hidden, and a host that parses larger source raises the cap. The
  measurement is of the library's own tree on four real files and on the
  seeds, and then on the pinned real-world corpus.

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
(`glecma_rule_name`, `glecma_rule_clause`). The clause numbers are the 17th
edition's (the concordance, `documentation/clause-concordance.tsv`, maps the
16th's, and `make check-clauses` fails a number the 17th lacks). The lexical rules are the first twenty-four; the parser's and the
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
differently. `make check-data-versions` asks the built `unicode` and `regex`
libraries and requires the three versions (theirs and the pin) to be one
string; the suite's pin check, which compares the pin files of every library
that generates tables, is its other half.

Identifier characters are classified by `unicode`'s `ID_Start` and
`ID_Continue` properties, which are what ECMA-262's `UnicodeIDStart` and
`UnicodeIDContinue` name, with `$`, `_`, ZWNJ and ZWJ added as the grammar
says. A test classifies every code point, raw and through a `\u{...}` escape,
against `unicode`'s own answer.

**From a stream.** CONVENTIONS section 5 has a library that reads input read
it through its own stream, so `GLECMA_Stream` hands out bytes from memory (a
borrowed block), from a file or from a host's reader, and
`glecma_source_create_stream` reads one to its end and makes a source. The
result equals the source the buffer constructor makes from the same bytes: the
stream is read into one buffer and that buffer is handed to the buffer
constructor, so the two cannot disagree on counts, locations or failures, and
a differential test and the fuzzers' option bit check it. The size cap is
checked as bytes arrive: the buffer never grows past one byte beyond the cap
(that byte is what tells a stream longer than the cap from one exactly as
long), so a stream of unbounded length costs at most the cap before it is
refused. A stream that reports an error is `GLECMA_ERR_IO` and stays failed;
a UTF-16 stream of an odd number of bytes is `GLECMA_ERR_CORRUPT`, located at
the end of the whole units. The price of one constructor is a second copy of
the text while the source is built (the buffer, then the source's own copy);
reading straight into the source's storage would save it and is rejected
until a measurement says the copy matters, because it would put a second
validation path in the constructor that every other source depends on. The
stream has no `read_line`: the tokenizer reads units, so nothing would call
it. Parsing that starts before the stream ends is not built; the contract is
the whole text first.

## The parse options and the feature set

Every entry that tokenizes takes `GLECMA_ParseOptions`: a size-first struct
holding the limits and a feature set, filled by `glecma_parse_options_default`
or `GLECMA_PARSE_OPTIONS_INIT`. A member beyond the caller's declared size
takes its default, so a caller built before a member was added keeps working,
and a test is built against a header-only struct. The goal and its context
are the next two members: a goal (`GLECMA_PARSE_SCRIPT`, `GLECMA_PARSE_MODULE`
and the eval goals, "The eval goals" below) and the context flags
(`GLECMA_CONTEXT_STRICT`, which can add strictness and never remove it). The
last member is the eval context, a pointer to a caller-supplied struct that
only the direct eval goal takes. A goal that is not one is `GLECMA_ERR_INVALID`. Every parse entry
takes the options, and the options are the only way to ask for anything: no
member selects the internal built-in entry.

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
feature. The names this build defines are the rows of one registry,
`src/core/features.def`, an X-macro list that is empty: the refusal looks a
name up in it, so the first feature to be built is a row there and nothing
else in the refusal changes.

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

## The parser

`glecma_parse` reads a source for a goal and returns a `GLECMA_Tree`. It is a
hand-written recursive-descent parser over the tokenizer, in `src/parse/`.
This section records its decisions and, for each, what was rejected.

**Tokens are pulled, and the grammar names the goal.** The tokenizer has no
single token stream (a `/` is division or the start of a regular expression, a
`}` is a punctuator or continues a template), so the parser asks for each token
under the default goal and *rescans* the current token where the grammar
needs another: a `/` or `/=` where an expression begins becomes a regular
expression, a `}` closing a template substitution becomes the template's
middle or tail, and a `%` in the internal built-in entry becomes an intrinsic
name. A rescan resets the tokenizer to the token's start, so the token cap
counts it once. Lookahead copies the tokenizer, which is a plain value; one or
two tokens are read ahead for `async`, `let`, `get` and `set`, `static`, and
a label. *Rejected:* choosing the goal from the previous token (as some
parsers do): it is wrong after `)` and `}`, the two places a program needs it
right.

**Errors leave by `longjmp`.** The first error is recorded in the parser and
control returns to the entry. Nothing the parser owns is released on the way:
everything it holds hangs off one struct, freed once at the entry, and the
tree is destroyed there on failure, so a failure at any point frees
everything, and a test fails each allocation of a parse in turn to show it.
*Rejected:* a result code from every function, which doubles the code of a
grammar this size and leaves a failure path through every statement to test.

**The tree** is a flat arena. A node is a 32-byte header (kind, flags, an
operator, the source range, counts), then its child pointers, then up to two
texts. A kind's children are *positional*: an optional child is an empty slot,
a `NULL`, so a walker reads `child(n, 1)` the same way whether or not the
slot is filled, and the kind list in `tree.h` gives each kind's slots. Names are
interned, so equal names are one copy, and every name or string is the
tree's own UTF-16 copy: the tree keeps no pointer into the caller's buffer.
Destroying a tree is a walk of its chunks, never a recursion, and so is the
dump. *Rejected:* one C struct per node kind (about seventy types, each with
its own walker code, and a new kind changes the ABI); a node per child with a
sibling link (a pointer per child and an indirection per step, for a shape the
positional slots already give); building the tree as JSON or ESTree-shaped
text (a second parse for every consumer).

**Ranges.** A node's range is in the source's own units. A function's and a
class's range is exactly its `Function.prototype.toString` text: a method's
starts at its first modifier or name and not at `static`, a computed key's
bracket is inside it, an arrow function's starts at its parameters or its
`async`. The range of a parenthesized expression is the inner expression's:
parentheses are not nodes, because evaluation, assignment targets and
`delete` see through them, and a node for them would have to be looked past
by every consumer. A test re-parses each function of a program from nothing
but its range.

**Values.** A numeric literal's value is not computed here: the compiler
computes it with exact arithmetic, and the node's range and two flags (legacy
octal, leading-zero decimal) say what to compute. A string's value and a
template part's cooked and raw values are UTF-16 code units (a lone surrogate
stays one), and a template part whose escape is invalid has the cooked-undefined
flag. *Rejected:* computing the double here (it would duplicate the compiler's
rounding, which must be correct for every literal).

**The builder interface.** The parser makes nodes only through
`GLECMA_TreeBuilder` and never reads one. What it needs to know about an
expression it has parsed (whether it can be assigned to, whether it was a
bare literal, its range) it keeps in a small record of its own. That is what
lets a second builder skip function bodies without changing a decision:
nothing the parser decides depends on a node that was not built. Two builders
implement the interface over one tree object. The *tree builder* builds every
node. The *checking builder* builds none inside a function body and makes the
reserved `GLECMA_NODE_UNMATERIALIZED_BODY` in place of the body; it is that
kind's only producer, so no tree a public parse returns holds one. In this
version it is reached by an internal entry (`glecma_parse_internal`); a public
parse that skips bodies is a later story's. Both builders count every node the
parser asks for, whether it is built or not, so the node cap refuses the
same input under either, and both record every event, bodies included. A test
runs both over programs that parse and programs that do not and requires the
same result, the same rule, the same place and the same events, and the
fuzzer asserts it for every input it generates. *Rejected:* one builder and a
pass that prunes bodies afterwards (the whole point is not to build them);
letting the parser read a built node to decide (it would make the two
builders differ the first time a check read a body).

**Cover grammars are resolved from builder events, not by rewriting nodes.**
Two places in the grammar cannot tell what a construct is until later. A
parenthesized list may be an expression or arrow parameters, and `async (` ...
`)` a call or async arrow parameters: the group is parsed as an expression and
if `=>` follows, the builder is rewound to a mark taken at the group's start,
and the group is parsed again as parameters. An array or object literal
followed by `=` is an assignment pattern, and a `for-in` or `for-of` head may
be one: the same, with the literal parsed again as a pattern. A rewind
discards every node and event since the mark and the parser restores its own
state to the snapshot, so nothing of the first reading survives, and the node
count is restored too (a test shows the cap that admits a program equals the
nodes of its tree). A CoverInitializedName (`{a = 1}`) is the one thing a
literal may hold that an expression may not; it is recorded as pending and is an
error unless the literal turns out to be a pattern. A group that is found to
be arrow parameters has its offset remembered, so the second reading of an
enclosing group takes that decision without making it again: a chain of arrow
functions nested in each other's parameters costs quadratic work in the
nesting depth, bounded by the nesting cap, and not exponential. *Rejected:*
converting the expression's nodes into pattern nodes in place (the usual
technique): it reads built nodes to decide, which a body that was not built
cannot answer, and it is the refactoring the checking builder was reserved
to avoid; a speculative parse that is thrown away on a failure and retried
(it costs the same and loses the first error's place).

**Declaration and reference events.** Beside nodes, the builder receives
scopes entered and left (script, function, arrow, block, catch, `for`, class,
static block, field initializer, `with`), names declared (`var`, `let`,
`const`, function, class, parameter, catch parameter, a function expression's
own name, a class's inner name), names referenced with how (read, written,
`typeof`, `delete`) and uses that change how a function must be compiled
(`this`, `super` property and call, `new.target`, a call whose callee is the
plain name `eval`). A scope summary is computed from them, never by walking a
function's subtree, so a body that was checked and not built still has one. A
reference is first reported as a read when the name is parsed and its flags are
amended when what follows shows it is an assignment target (`a = 1` is a write
and not a read; `a += 1` and `a++` are both), the operand of `typeof` or of
`delete`. The events are kept in the tree and read through an internal
accessor; a public accessor is the compiler's story.

**The goals built.** The Script goal, sloppy or strict (by the context flag, or
by a directive), the Module goal with its `import` and `export` declarations
(see "The Module goal"), direct and indirect eval, the dynamic function and one
function read again (below). `import(...)` is built, and so is the refusal of
`import.meta` in a Script. Early errors are under "Early errors", below. What the grammar itself decides is built: every statement
and expression form of the edition; classes with fields, accessors, private
names, static blocks and `#x in o`; destructuring; arrow and async arrow
functions; generators and `async`/`await` as the grammar parameters `[Yield]`
and `[Await]` give them, including the contextual words as names where they
are names; optional chaining; template literals tagged and untagged (an
invalid escape is refused only when nothing tags the template); automatic
semicolon insertion with every restricted production; the directive prologue,
with the strictness it turns on applied to the token already read (a legacy
octal literal or escape before the directive is refused when it takes
effect), and strictness turned off again before the token after a strict
function's closing brace is read.

**Annex B syntax built here** is what the grammar itself needs: a function
declaration as the body of an `if` or of a label in sloppy code (the `if` form
is placed in a block of its own, as the Annex says it is to be treated), a
`for-in` head with an initializer in sloppy code, HTML-like comments (the
tokenizer's) and the legacy numeric forms. B.3.9, the call expression as an
assignment target (`f() = 1`, `f()++`, `for (f() of a)`), is accepted in
sloppy code for `=`, the compound operators, `++` and `--` and the `for`
heads, and refused in strict code, for the logical assignments and as a
destructuring target, because the clause is normative-optional for a host that
supports it and this library is built to sit under browsers; the assignment
throws a `ReferenceError` when it runs, which the compiler emits. The
early-error side of Annex B is "Early errors", below.

**The dump** (`glecma_tree_dump`) prints one node per line, indented, with its
kind, operator, flags, texts and range, and walks an unknown kind by number. It
is *not a printer*: its output does not reparse to the same tree, and nothing
reparses it. The decision is recorded here, closed by this story: a printer
that reproduces a tree needs every decision a parse made back again (where
parentheses are needed, where a `/` is division, where a semicolon must be
written, what a numeric literal's spelling was, comments), which is a second
implementation of the grammar to keep in step with the first, and its only
use here would be a differential the library already has more cheaply (the
two builders, the re-parse of each function). *Rejected:* a printer, for
that reason. A host that wants one walks the tree and prints source text
from the ranges.

**The tree's lifetime.** A tree holds one counted reference to its source and
releases it when it is destroyed, so a caller may release their own at once
and the source outlives the tree for as long as anyone else holds it. A
compiler that wants a function after its tree is gone reparses it from the
source; a test destroys a tree and parses each function again from its text
alone. The tree API promises nothing about use at run time.

### Early errors

Every early error of the edition is reported by the first parse, in every
strictness, and none reads a node. The checks live in the parser's own
records (`src/parse/early.c` and the context flags of `parser.h`), so a body
the checking builder did not build is checked exactly as a built one is; the
two builders must give the same result for the same input at the same place,
and `make check-differential` runs both over every test262 pair in scope, a
module pair under the Module goal, and fails on any difference in result, rule
or place. It was seen to fail on two plants that make a check
depend on whether the nodes it concerns were built, which is the only way a
check can differ between the two. The real-world corpus is run through it
when that corpus is pinned.

**Context flags** are the grammar's parameters and the static semantics that
`Contains` expresses, carried in the parser's flags: whether `return`,
`super.x`, `super()` and `new.target` are allowed, whether `arguments` is a
name, and whether the parser is reading parameters (where a `yield` or
`await` expression is an error). A function takes its own context, an arrow
function takes what the code around it allowed (`super`, `new.target` and
`arguments` pass through it), a method may use `super.x`, a derived
constructor `super()`, and a class field initializer or static block `super.x`
and `new.target` but not `super()` or `arguments`.

**Jump targets and labels** are a stack of labels and two counters (loops and
breakables) that a function, an arrow function or a static block sets aside
and restores. A label that labels an iteration statement is marked when that
statement begins, through any run of labels before it, which is what lets
`continue` name it. A labelled function declaration is an error as the body
of an `if`, a loop or `with`.

**Declarations** are records. Every name a scope declares is appended to one
stack, tagged with its kind (`var`, a `for-of` var, lexical, a plain
function, another function, a parameter, a simple or destructured catch
parameter). A scope's records are checked once, when it ends, by the rules of
its kind: a block or case block cannot hold a name twice or as both lexical
and var, except that sloppy code may declare one plain function twice (Annex
B.3.2.4); a function body, script or static block treats its top-level
functions as var-like, and a parameter may not also be lexical there; a
`for` head's lexical names may not be var-declared in its body; a catch
clause shares its scope with its block, a simple catch parameter may be
redeclared by a var but not by one a `for-of` head makes, and a destructured
one not at all (Annex B.3.4). A var declaration stays in the stack when its
scope ends, so that the scope around sees it too, until a function
boundary; a scope with no lexical name, function or parameter of its own has
nothing to check and passes them on untouched. The cost of a var is then
one record and one check at each enclosing scope that has a lexical name, which
the nesting cap bounds (measured: 100,000 vars in the innermost of 250 nested
blocks that each declare a name take 0.16 s, against 0.04 s when no block declares one and 0.05 s for the same
vars in one block). Checking at the end, and not at each declaration, is what lets a
directive late in a function body decide whether its parameters may repeat,
and costs the same linear time: a scope's names are hashed once when it ends.
*Rejected:* a hash per scope kept up as names are declared, which must also
be undone when a cover grammar rewinds; a record stack is cut back to a mark
and needs nothing else.

The checks that hold under the Module goal (strict code, `await` reserved, a
top level that declares its functions as lexical names, no HTML-like comments)
are the Module goal's settings of these same checks, so one body of code is
tested under both goals; the goal's own syntax and errors are "The Module
goal", below.

**Private names** are the same shape: a class's declarations and the
references made inside it are two stacks, and when the class ends its
declarations are checked for repeats (a getter and a setter of one
staticness may share a name) and its references are matched to them; what no
declaration answers moves out to the class around, and what is left at the
end of the parse, or was made in a class heritage, is an error.

**Cover grammars** keep their rewind: an object literal's second
`__proto__: value` is recorded as a pending error exactly as `{a = 1}` is,
and is an error unless the literal turns out to be a pattern. A rewind cuts
the labels, records and private names back to the mark with everything else.

**Regular expression literals** are validated by `regex`'s ECMAScript front
end (see "Regular expression literals").

### The Module goal

`GLECMA_PARSE_MODULE` reads a Module (clause 16.2): the Script grammar's
statements plus import and export declarations, in strict code, with `await`
an operator at the top level and a reserved word everywhere else, `import.meta`
valid and no HTML-like comments. The tree's root is a `GLECMA_NODE_MODULE`,
the node kinds 74 to 85 are the declarations (an import's source, attributes and
specifiers; an export's declaration, default, named list or `export *`), and the
tree records the goal. The decisions, each with what was rejected:

**A Module is the Script's parser with different settings, and its own item
loop.** The settings are parser flags (`PF_MODULE`, `PF_AWAIT_RESERVED` and, at
the top level only, `PF_AWAIT`), so every check that holds in strict code, in
a function, a class or a static block is the same code under both goals, and
top-level `await` is the existing `await` operator parse with the flag on.
Only the top level differs: `src/parse/module.c` reads module items, a loop
that takes an import or export declaration where it sees one and otherwise a
statement list item. An `import` or `export` anywhere else is an unexpected
token because the statement parser never admits one. *Rejected:* a second
statement parser for modules, which would have to be kept equal to the first.
A Module has no directive prologue (clause 16.2.1: the items are statements),
so a `"use strict"` string is an ordinary statement in the tree; it is strict
already.

**The module's names are scope records, checked once.** An import binds its
local name as a lexical declaration (declaration kind `GLECMA_BDECL_IMPORT`,
which the scope tracker treats as `let`), so "no lexical name twice, none also
var-declared" (clause 16.2.1.1) is the module scope's check at its end, the one
a block or a script has, with the top level's functions lexical as a module
makes them. The scope kind is `GLECMA_BSCOPE_MODULE` and its redeclaration is
its own rule code (`module-redeclaration`). **The exported names** are a hashed
set of interned names (equal names are equal pointers), so a thousand exports
cost a thousand insertions: `export var a, [b]`, `export function f`, `export
class C` export what the declaration added to the module scope's records (a
nested function's own names are gone when its scope ended, so they cannot be
mistaken for the module's), `export { a as b }` and `export * as ns` export
their written names, and `export default` exports `default`. A name already in
the set is the `module-duplicate-export` error at the second. **The local names
an `export { a }` refers to** are recorded as they are read and matched
against the module scope's declarations when the whole module has been read,
because the declaration may come after the export (`export { a }; var a;`); a
`from` clause, which is read after the list, discards them, since those names
are another module's. A string cannot name a local binding and a reserved word
cannot either, which depends on the same `from`, so each name carries the rule
it would break and the rule is applied once `from` is known. *Rejected:* a
tree walk at the end of the module, which no check may do (the checking
builder builds no body, and a differential holds the two builders to one
answer); a scan of the declarations for each export (quadratic in the number of
exports).

**Import attributes** (`with { type: "json" }`, clause 16.2.2) are a list node
of key and value pairs; two with one key are an error at the second
(`import-attribute-duplicate`), the key written as a name and as a string
being one key. The syntax is `with`, as the pinned edition has it, and not the
earlier `assert`.

**`export default`** takes a function, an `async` function or a class
declaration, with the name optional (such a function or class binds no name,
and the tree's declaration has an empty id), or an assignment expression
followed by a semicolon. The lookahead the grammar states (`function`, `async
function` on one line, `class`) decides which, so `export default async () =>
1` and `export default async` followed by a new line and a function are an
expression and an expression and a declaration.

**Module string names must be well formed** (clause 16.2.1.1, the value of a
string used as an import or export name holds no lone surrogate); a module
specifier and an attribute's key and value are not export names and are not
checked. The test262 gate covers all 869 module pairs the ledger held, and
`tests/unit/test_module.cpp` states each rule with the control that is
accepted.

### The eval goals

`eval` has two goals, and both read the Script grammar (clause 19.2.1.1).
**Indirect eval** (`GLECMA_PARSE_INDIRECT_EVAL`) is a Script in the global
context: it takes no context and no inherited strictness, and a caller that
gives either is `GLECMA_ERR_INVALID`, because it has no caller to inherit from.
**Direct eval** (`GLECMA_PARSE_DIRECT_EVAL`) is a Script read in the context of
the code that called `eval`, which the caller supplies as a struct on the parse
options, `GLECMA_EvalContext`: size-first like the options, so a caller built
before a member was added keeps working. It carries what PerformEval derives
from the running execution context: whether the caller is strict, in a function
(`new.target`), in a method (`super.x`), in a derived constructor (`super()`)
or in a class field initializer (`arguments` is not a name), and the private
names in scope. The flags are the same constants as the options' own context
flags (`GLECMA_CONTEXT_*`), which also serve the function re-parse goal; a
flag that implies another (a derived constructor is a method, a method is in a
function) without it is `GLECMA_ERR_INVALID`, so a caller's mistake is a
refusal and not a guess.

The parser reads the flags into the same context flags it keeps for code in a
function, so the eval code is checked by the one set of checks: `super.x` is
refused unless the flag allowed it, whatever function the eval code itself
declares, and an arrow function in the eval code inherits what the caller had
(clause 15.3) while a function does not. **Private names** are the one input
that is not a flag: the references no class of the eval code declares are
answered by the names in scope (`AllPrivateIdentifiersValid`, clause 15.7.1)
and the rest are `private-name-undeclared`. The names are interned through the
builder at the start of the parse, so a reference is matched to them by
pointer like every other name, and a name written with escapes or outside the
BMP is the name it spells. *Rejected:* passing the names as a callback, which
makes the cost per reference a call into the host and a host that is a
compiler a re-entrancy hazard; and taking them as UTF-8, which the tree's
texts are not.

**The tree records its goal, final strictness and context**, so the compiler
reads them from the tree and is not passed them twice: `glecma_tree_goal`,
`glecma_tree_strict` and `glecma_tree_eval_context`. The tree keeps its own
copy of the context, names included, in the arena that holds its texts; the
caller's memory may go as soon as the call returns. Eval code is a Script, so
its tree's root is a Script node, and the goal is what tells it from one.

### The dynamic-function goal

`glecma_parse_dynamic_function` is the parsing half of CreateDynamicFunction
(clause 20.2.1.1.1), for the four constructors (`Function`, `GeneratorFunction`,
`AsyncFunction`, `AsyncGeneratorFunction`). The caller gives the kind and the
two texts, the parameters (the arguments before the last, joined with `,`: that
is ToString and a concatenation on the caller's values) and the body, as
sources, so a lone surrogate in either arrives intact. The algorithm reads the
parameters alone as FormalParameters, the body alone as the kind's body, and
then the text it assembles as the kind's function expression, with every early
error of both; this does exactly that, as three runs of the parser, each over a
source of its own, and keeps the last one's tree.

Each piece is read alone so that each is valid alone: `new Function("/*", "*/
) {")` joins into a function and is not one, because neither piece is a
valid text on its own. That is the whole reason for the three runs, and the
reason the line feeds are in the assembled text: a line comment that ends the
parameters would otherwise swallow the closing parenthesis. The kind chooses
the parser flags of each reading (`yield` an operator in a generator's body,
`await` in an async function's), so `yield 1` is a statement in one body and a
syntax error in another. The tree is the function expression of the assembled
text, whose source (`glecma_tree_source`) is that text, the text of the
function's `toString`; its root is the whole of it and its name, `anonymous`,
is the text's and is not bound inside the function, as the algorithm sets a
name and binds none. A failure
found in one piece alone is placed in the source the caller gave for it, in its
own units, line and column (the line feed the algorithm puts before the body is
not counted, an error at the end is at the end of the piece, and a UTF-8 piece's
offset is in bytes); an error only the assembled text has (a duplicate parameter
in strict code, a directive over non-simple parameters) is in the assembled
text, which a failed call does not return. *Rejected:* one reading of
the assembled text, which accepts every text the algorithm refuses for being
valid only joined; and reading the body inside the parentheses-and-braces of a
wrapper without a source of its own, which gives the body no offsets of its own
and no way to cap or limit it separately.

Every goal reads through one entry, `parser_execute` (`src/parse/entry.c`): the
builder, the tokenizer, the limits, the one cleanup and the failure record are
the same for a Script and for the three readings here, so a goal is a job
(its source, its starting flags, a function that reads what it reads) and a
few lines.

### The function re-parse goal

A function is compiled on its first call, after the tree of the script that
holds it is gone, so it is read again from its range of the source it was first
read in: `glecma_parse_function`, with `GLECMA_FunctionContext` (size-first),
which holds the inputs the cross-milestone contract gives for that goal: the
range, the kind, the strictness, the flags (`super` property and call,
`new.target`, `yield`, `await`, inside a class field initializer or static
block, module code), the private names in scope and the feature set of the first
parse. The kinds are every form that has a body: functions, generators, async
functions and async generators, arrow and async arrow functions, methods,
getters, setters, base and derived constructors, field initializers and static
blocks. The result is the first parse's tree for that function, with locations
in the whole source, and a function that reads again differently is a defect,
never the program's syntax error: `GLECMA_ERR_INTERNAL`.

**The context is the parser's own flags at the place the function begins.**
Each flag of the struct is one parser flag, and the function's kind decides its
own (a function's `new.target`, a generator's `yield`, a method's `super.x`, a
derived constructor's `super()`; an arrow takes what is around it), so the
same code makes the same decisions in both parses: the re-parse is the first
parse's own grammar, started at a function, and not a second parser. *Rejected:*
a separate function grammar, which would have to be kept equal to the first;
and storing the parser's flags as one number in the compiled unit, which
would make the parser's private bit layout part of every cached program.
Two inputs are not in the contract's list and are here for correctness: whether
the function was written as a declaration, which decides the tree's kind and
where its name is bound (`GLECMA_FNCTX_DECLARATION`), and the class static block
as a flag of its own, because an arrow function in one has the block's
`arguments` rule but not its reserved `await` (test262's
`static-init-await-binding-valid` is a function named `await` in an arrow in a
static block).

**Only the range is read.** The tokenizer is told the text ends where the
function does. The token after a function the first parse read as a regular
expression (a `/` after a function declaration) would otherwise be read as
division here, and could fail on text that is not the function's; with the
range bound, nothing past it is read. A method's range starts at its first
modifier or its name, and so contains its name: it is read to find where the
parameters begin, and the builder is taken back to where it stood, so that the
references and functions of a computed name, which belong to the class's code,
are not the method's (a test shows the events). A class field initializer is
read as an Expression, because a parenthesized initializer's range holds what
is inside the parentheses (`(a, b)`).

**The equality is shown over the corpus.** `make check-reparse` (in
`TEST_GATES`) walks the tree of every test262 pair in scope with the rules of the
grammar, derives the context of each function from the tree alone (a second
statement of the context, in `tests/reparse_support.h`, which shares nothing
with the parser's flags), reads it again, and requires an equal tree: the same
kinds, flags, operators, ranges, texts and children, and the goal and strictness
the function should have. Every function is read twice, once with the strictness
it inherited and once with the strictness its code has (its own directive
included), which must give the same tree. At the pin that is 156,911 functions
of every kind. It cannot see a flag that only forbids (a function that was
accepted is never refused for a missing prohibition), so the unit tests give
each flag a function that needs it and is refused without it, and the real-world
corpus joins the gate when it is pinned.

### Regular expression literals

The pattern and flags of a literal are an early error unless `regex`'s
ECMAScript front end accepts them (`src/parse/regexp.c`). Only the front end
runs: the pattern is parsed and released, never compiled. The pattern is
handed over as the UTF-8 of the source's code points, a lone surrogate (which
a UTF-16 source can hold) as WTF-8 under regex's option for it; `g` and `y`,
which are not pattern options, are counted here (a repeat is an error) and
left out of the flags regex reads, which answers for the rest of the alphabet,
a repeat and `u` with `v`. The check reads the source, never a node, so a
literal inside a function body that the checking builder did not build is
validated exactly as a built one is.

*The two libraries share one Unicode version, and the gate says so.*
`unicode`, `regex` and this library report 17.0.0, and `make
check-data-versions` fails when any of the three differs (it asks the built
libraries, not their pin files; the suite's pin check compares the files).
*They do not share an edition or a test262 pin:* regex's ECMAScript front end
is written against the 16th edition (ES2025) and its own test262 commit, this
library against the 17th and its own. The 17th edition's pattern clauses
(22.2) were compared with the 16th's through the concordance and carry no
syntax change, and every test262 regular expression literal in scope that this
library's pin holds is refused or accepted as the edition says, which is the
evidence that the two agree where they meet. The front end takes no edition
argument yet, so none is passed (RX-12's edition argument is regex's, and the
engine's feature set names no pattern-level feature to pass either: the
registry of features is empty).

**Limits.** The pattern is bounded by the same caps as everything else. The
front end's nesting (each group, and each alternation or class inside one,
is a level) is capped at 256 and at what the parse's own `nesting` cap has
left: a literal nested deeper is `GLECMA_ERR_LIMIT` naming `nesting`, never a
syntax error. Its node count is charged to the parse's `nodes` cap and its
pattern length is what the literal measures; its other limits are off, being
bounded by the pattern's length. The front end recurses over the nesting, so
a literal is validated only when the native stack has room for its deepest
pattern: it took 114 KB for 255 levels of the worst of four shapes (groups,
lookaheads, alternations, classes; GCC 16.2 `-O2`, a thread whose stack was
found by bisection, which includes the thread's own base), and a literal keeps
8 KiB and 1 KiB for each opening parenthesis or bracket in its pattern in
reserve beyond the parser's own, so a thread that cannot hold it is refused
with `native_stack_bytes` before the front end runs and never faults, and a
small literal needs little.

*A pattern without `u` or `v` is read as code units, and regex reads code
points,* so the two are made to agree at the hand-over: every surrogate half
written in such a pattern (a lone one, or half of an astral character) is
passed as its `\uXXXX` escape, which regex reads as one unit. That orders a
class range over the halves as the edition does (`[😀-😁]` is out of order
without the flag, and refused) and carries a lone surrogate, which WTF-8 cannot
without `u`. Under `u` and `v` the pattern goes as WTF-8 as it is written.

### Recursive descent and the stack

A hostile input must not make the parser overrun the native stack, whatever
thread it runs on. Two controls bound it. `nesting` counts *levels of
syntactic nesting*: a statement, an assignment expression, the operand of a
prefix operator or of a right-associative `**`, a pattern, a `new`, each inside
another (the layers an expression goes through on the way down, binary and
unary, are not levels). It is a bound on the depth a tree can reach through
nesting, and it is stated in the units a person can count. `native_stack_bytes`
is the real control: the parser notes its stack position at the entry, and
every recursive function compares the bytes used since with the grant and
returns `GLECMA_ERR_LIMIT` naming the field, never a fault. The position is
the frame's own address (`__builtin_frame_address`), because an
address-sanitized build moves locals to a stack of its own and an address of
a local would measure the wrong stack. A reserve of 24 KiB is kept back for
the frames between two checks, the tokenizer and the builder. A cap of zero is
a real cap of zero.

*Measured.* The bytes one level costs differ by what the nesting is made of.
Measured on x86-64 with GCC 16.2 `-O2` (`tools/measure-stack.c`, which finds,
for each shape, the least nesting and the least grant that admit it at two
depths and takes the slope):

| Shape | Bytes per level | Shape | Bytes per level |
| --- | --- | --- | --- |
| `(((x)))` | 2,144 | `{{{}}}` | 288 |
| `[[[x]]]` | 1,888 | `function f(){function f(){...}}` | 880 |
| `({a:({a:x})})` | 2,016 | `class A{m(){class A{m(){...}}}}` | 1,520 |
| `() => () => x` | 1,808 | `if (a) if (a) x;` | 928 |
| `f(f(f(x)))` | 1,296 | `a = a = x` | 352 |
| `` `${`${x}`}` `` | 1,888 | `a ? b : a ? b : x` | 400 |
| `[[[x]]] = y` | 1,888 | `!!!x` | 176 |
| `(a = (a = x) => 0) => 0` | 1,248 | `a ** a ** x`, `new new x` | 256, 304 |
| `l0: l1: x` | 848 | `async function f() { await ...` | 1,104 |

The worst is 2,144 bytes per level (a parenthesized group, which holds a
snapshot of the parser to return to; the snapshot grew when story 5 gave the
parser the stacks of its early errors to cut back, from 1,920), the commonest
real nesting is a function or a block at 288 to 880. An address-sanitized build takes about 4.4
KiB for the same input. The frames that hold a snapshot, and the other large
frames on the recursive path, are marked not to be inlined, because inlining
them into the path moved the worst case from 1.9 to 2.4 KiB per level on the
compiler measured; a parenthesized group that is arrow parameters returns to
its caller before the arrow function is parsed, so that its frame is not under
every level of nested arrow bodies.

*Decision.* The default `nesting` is 256 and the default `native_stack_bytes`
1 MiB. At 256 levels the worst shape takes 549 KB, so the deepest input the
default nesting admits parses in under half of the default grant, with the
reserve to spare, on a thread that has 1 MiB: a test finds, for each of nineteen
shapes, the deepest input the default admits and parses it with the default
limits on a thread whose entire stack is 1 MiB. 256 levels is almost five times the
deepest real source measured (of 1,187 real files, the packages of a Node.js
installation, minified and not, the deepest nests 53 levels, and every one
that is a Script parses: the 631 that are refused are ES modules, which the
Script goal refuses at their first `import` or `export`; the pinned corpus's
deepest, TypeScript's compiler, nests 54) and below the
depth at which a chain of `else if` or conditionals in generated code begins to
appear; it is the figure to raise first if a corpus says otherwise (a host that
does raises `nesting` and `native_stack_bytes` together, and the table says how
far). 1 MiB is the stack a Windows main thread starts with and less than any
Linux or glibc thread's; a host that parses on a thread with less (a macOS
secondary thread has 512 KiB, a musl thread 128 KiB) must lower the field,
because the library cannot see how much stack its caller has, and a test
parses deep input on a thread with 256 KiB and a grant of 128 KiB and requires
the refusal. *Rejected:* a nesting cap alone (it is safe only for the shape it
was measured on, and the table spans a factor of twelve); an explicit
stack with no recursion (it would rewrite the grammar around a
state machine and give up the correspondence with the specification's
productions that makes it reviewable); measuring a frame count (it counts
frames, not bytes, and the bytes are what a stack holds).

The same tool, run under emulation at the milestone's close, gives the worst
shape per level on the other targets: **32-bit x86 1,232 bytes** (the
parenthesized group again), **AArch64 2,368**, **s390x (big-endian) 2,824**, against
x86-64's 2,144. At 256 levels the worst is then 316 KB, 606 KB and 723 KB: the
deepest input the default nesting admits fits under the default 1 MiB grant on
every one of them, with the reserve to spare, and the unit test that parses the
deepest input of nineteen shapes on a 1 MiB thread ran on each. The figures
are of emulated code of one compiler (GCC 14 for the cross targets) and say how
the frames lay out, not how fast. The Win64 figure was not taken (the gates ran
under wine, which is not a Windows stack); a real Windows machine is the place
for it.


## Fuzzing

Five harnesses, `tests/fuzz/fuzz_tokenizer.c`, `fuzz_source.c`, `fuzz_parser.c`,
`fuzz_goals.c` and `fuzz_alloc.c`, are libFuzzer targets (`make fuzz-tokenizer`
and so on) and, with a replay driver, plain programs (`make fuzz-replay`) that
run every file of the seed corpus through the same entry point. The replay
needs no libFuzzer, is part of `make test`, and runs again under ASan and UBSan
in `make test-asan`, so an input a campaign once found is a failing test. The first byte of an input chooses the
encoding (UTF-8 or UTF-16), strictness, the goal, the built-in entry, the
limits and how goals are chosen; the rest is the source. The tokenizer
harness checks that the result is one a tokenizer may return, that a failure
record agrees with it, that tokens lie inside the source in order and at the
positions the source reports, that a saved lexer reads a token twice the same
way, and that the same input fails the same way. The source harness
transcodes a well-formed UTF-8 input with its own decoder and requires the
two encodings to agree on code points, lines, locations and tokens. The
seeds are regenerated by `tests/fuzz/make-seeds.py`.

The parser harness (`fuzz_parser.c`) and the goals harness (`fuzz_goals.c`)
check what an input that is accepted must hold: the tree is walked and dumped,
every child lies in its parent, every scope event is closed, the other builder
gives the same result, rule and place, and **every function of it reads again
from its range as the tree it was** (`tests/reparse_support.h`, the walk the
re-parse gate uses). The goals harness chooses the goal with its first byte (a
Script, a Module, direct eval with a context it builds from the second byte,
indirect eval, a dynamic function of each kind with the input split into its
parameters and its body) and, for direct eval, knows which contexts are
inconsistent and requires `GLECMA_ERR_INVALID` for exactly those.

The allocation-failure harness (`fuzz_alloc.c`) runs an input twice through an
allocator that counts: once with nothing failing, to learn how many allocations
the work makes (the source's and the tree's), and once with the allocation the
input picks refused. It requires `GLECMA_ERR_OOM` and nothing else (not the
syntax error a later allocation would have found), no tree, and every block
freed, and, for the allocation one past the last, the answer the first run
gave. Its first byte chooses the goal (a Script, a Module, direct eval with a
context, indirect eval, a dynamic function), the encoding, the builder, the
strictness, whether the source is made from the stream constructor and whether
every function of an accepted program is read again through the failing
allocator; its seeds fail every allocation, in turn, of thirty small programs,
because a handful of picks over large seeds do not reach the allocation of each
construct. The unit sweep that does the same for fixed programs is
`Hostile.EveryAllocationOfEveryGoal...` (see "Hostile input").

*A campaign.* `make fuzz-campaign-<harness>` runs one harness under libFuzzer for
`FUZZ_CAMPAIGN_SECONDS` (a day by default) from a scratch copy of its committed
seeds, slices cut from the real-world corpus (`tests/fuzz/corpus-seeds.py`:
windows of each file, with the header bytes the harness reads, so a campaign
starts from real token sequences and from every error that cutting a real
program where it was not meant to be cut gives) and a dictionary of the words,
punctuators, escapes and cover-grammar forms of the language
(`tests/fuzz/ecmascript.dict`), with value profiling, inputs of up to 64 KiB, a
thirty-second limit on one input (a slow unit is a finding, measured on the
host first) and 2 GiB of resident memory. The corpus it grows, every input that
crashed or timed out, and the log (the executions, the seconds, the coverage at
its end) are under the build directory and never the committed seeds; an
input that crashed is a regression test once it is copied to
`tests/fuzz/corpus/<harness>/` with its fix, and the campaign of a changed
parser is run again from the start, because a campaign over a changed parser is
not a campaign over the final one. `make -j5 fuzz-campaign` runs all five side by
side. The closing campaign's floor is 24 hours per harness (owner question Q-2
asks whether that is the suite's floor).

## Hostile input

Source from outside the process is untrusted. The claims about what it can do
to the parser, and what states each:

| Claim | What states it | Where |
| --- | --- | --- |
| No input crashes it, leaks, or takes it past a limit | Five libFuzzer harnesses over every entry, run for a day each at the close; their seeds replayed in `make test` and under ASan and UBSan | "Fuzzing" |
| Failing any allocation of any parse is `GLECMA_ERR_OOM` with nothing left held | `Hostile.EveryAllocationOf...` (every goal, both builders, three ways to make a source, a dynamic function), and the `fuzz_alloc` harness over the inputs a fuzzer finds | below |
| Time and memory grow linearly with the size of the input, within a stated factor | `Hostile.DoublingTheInputKeepsTimeAndMemoryLinear`, sixty-eight families of input at six sizes each | below |
| A nest costs a constant per level, except where the grammar must read it twice | `Hostile.DoublingTheDepthWithinTheCapKeepsTheCostPerLevelFlatOrLinearForTheCoverGrammars` | below |
| A parse that is refused for a limit holds memory in proportion to the limit, not to the input | `Hostile.AParseRefusedForALimitHoldsMemory...` | below |
| The deepest input the default limits admit parses on a thread with 1 MiB of stack, and a smaller grant is refused and never faults | `ParseLimits.TheDeepestInputTheDefaultNestingAcceptsParsesOnADefaultStack`, `AParseOnAThreadWithASmallStackNeverFaults` | "Recursive descent and the stack" |
| Parses on separate threads are independent | `Hostile.ParsesOfEveryGoal...OnSeparateThreads...` and the older threaded tests, under `make test-tsan` | below |
| Every one of these checks is seen to fail | planted defects 134 to 138, among the rest of the registry (`make check-planted`) | "Planted defects" |

**The factor.** `Hostile.DoublingTheInputKeepsTimeAndMemoryLinear` makes sixty-eight
families of input, each a size doubled five times (so the largest is thirty-two
times the smallest, between 0.2 and 10 MB), and parses each with the metering
allocator of `tests/test_helpers.h`, which counts every byte the library holds
at its peak. The families are the shapes a hostile input is made of: many
statements, many distinct and many repeated names (as `var`, as `let`, as
references), many functions, long and duplicated parameter lists, labels, long
chains of binary operators, members, calls and optional chains, arrays with and
without holes, objects with distinct and with repeated keys, spreads, long
parenthesized lists that are and are not arrow parameters, sequences of arrow
candidates and of pattern candidates, long patterns, long templates and
templates in templates, long strings, comments and identifiers, escapes, regular
expressions (long, alternated, grouped), lines (LF and CRLF), the decoders
(UTF-8, UTF-16, and a stream read one byte at a time), classes with thousands
of members and private names, modules with thousands of exports and imports,
and ten kinds of nest repeated at 100 to 240 levels (the cap is 256). The cost per
byte at each of the three largest sizes, over the least cost per byte of the
three smallest, is the factor: 1.0 is exactly linear, and a cost that is
quadratic in the size gives 32 here, so a factor of three is an order of
magnitude from the failure it exists to see.

Measured (GCC 16.2 `-O2`, a 12-thread x86-64 laptop shared with other work):
the worst time factor over the sixty-eight families is 1.8 (the least, 1.0),
and the worst memory factor 1.05. The memory is counted, not sampled, so it is
the same on every machine of a word size; the time is the least of three to
five runs of each size, which is why a loaded machine moves it by a few
tenths and not by a factor. The tests assert time within **3.0** and memory
within **2.5** (`kTimeFactor`, `kMemoryFactor` in `test_hostile.cpp`). The
memory bound is wider than what is measured because the library's arenas double
as they grow: a table that has just doubled holds up to twice what it needs.
Under Valgrind and a sanitizer the sweep takes smaller sizes (the cost per byte
is a ratio, so it says the same).

**The depth.** Every nest costs the same per byte at any depth except those
the grammar cannot classify until their end: a parenthesis that turns out to
be arrow parameters, and an array or object literal that turns out to be a
pattern. Such a nest is parsed as one and, when the `=>` or the `=` is seen,
read again as the other (`parse_paren`, `finish_assignment`: the builder's
rewind, which is how the cover grammars are resolved without rewriting nodes),
with a memo of the decisions made so that the reading again does not itself
read nests again. A nest that is read again inside the reading again
(`(a = (a = (a = x) => 0) => 0) => 0`) is read again at each level: its cost
is quadratic in its depth and not in the size of the input, and the nesting
cap bounds the depth. `Hostile.DoublingTheDepth...` holds the total number of
levels fixed and varies how they are divided into nests, from 6 levels deep
to the deepest the default limits admit (127 to 254), and measures the cost
per byte at each. Measured: `(a, b = (...) ...) => 0` costs 14 to 16 times
as much per byte at 127 levels as at 6, an async arrow's candidate 17 to 22,
`[[[...]]] = y` 33 to 58 at 254, and `[{a: [{a: ...}]}] = y` 14 to 20 at 127;
every other shape (a plain parenthesis, an array literal, a call, a template,
a function, a class, a block, arrow bodies) costs 0.8 to 1.7 times as much. So
the per-byte cost of a cover-grammar nest is proportional to its depth, with a
constant below one, and the cap of 256 makes the worst case a factor of about
sixty on the cost of the same bytes at shallow depth: a megabyte of the worst
nest costs a few seconds and not a minute. The test requires each such nest to
grow no faster than its depth (within the factor above) and every other nest not
to grow at all. *Rejected:* making the reading again linear by converting the
nodes of the first reading into the second (the builder interface forbids a
check, or the builder, to read a node it has made: it is what keeps the
checking builder's bodies out of memory); lowering the nesting cap (it is the
figure a real corpus sets: the deepest real file nests 54 levels).

**Limits hold memory.** A parse is refused for the first limit it passes, and the
check comes before the allocation it bounds, so the memory a refused parse
holds depends on the cap and not on the size of the input: the test refuses an
input four times and two hundred and fifty-six times over the cap of `nodes`,
`tokens`, `nesting` and `literal_length` and requires about the same peak
memory (within twice, and four KiB, the slack of a table that has doubled).

**Allocation failure.** Every allocation of a parse goes through the caller's
allocator, so each can be refused. `Hostile.EveryAllocationOfEveryGoalMayFail...`
runs each goal (a Script, a Module, direct eval with a context and a private
name, indirect eval), each valid and each refused, with both builders and each of
the three ways to make a source (UTF-8, UTF-16, a stream read a byte at a time),
once with nothing failing to count the allocations and then once for each, and
requires `GLECMA_ERR_OOM`, no tree and no source, and every block freed; one
more than the last allocation fails nothing and gives the answer the first run
gave. The dynamic function does the same through its two sources. The
`fuzz_alloc` harness (below) does it for the inputs a fuzzer finds.

**Threads.** `Hostile.ParsesOfEveryGoal...` parses a Script and a Module on
eight threads from two shared sources with shared options, limits and builders'
choices, two hundred times each; under `make test-tsan` it is the library's
claim that a parse touches nothing it does not own and a source is read-only.

## The real-world corpus

Conformance tests are small and written to be read. Real JavaScript is the
other population: minified bundles that put a hundred thousand tokens on one
line, a compiler of nine megabytes, an ES-module package. `tools/corpus/CORPUS.tsv`
names eighteen files of twelve npm packages: TypeScript 5.9.3's compiler,
Babel standalone 7.29.0 (minified), React DOM 18.3.1 (development and
minified), lodash 4.17.21, jQuery 3.7.1, moment 2.30.1, d3 7.9.0, core-js
3.51.0's minified bundle and RxJS 7.8.2 (Scripts), and three.js 0.187.0, Vue
3.5.45 (development and minified) and acorn 8.19.0 (Modules): 20.4 MiB, thirteen
Scripts and five Modules, seven minified. Each row has the exact package
version, the SHA-256 of the registry's tarball of it, of the member read and of
the license file, the goal the file is read under, whether it is minified, and
its license as the package declares it.

*Which files are admitted* is owner question Q-59. The rule applied here, which
the table's tool enforces and the owner is asked to confirm, is that a file is
admitted only when its license is plainly one that allows redistribution and
the package is public on the registry: MIT, ISC, Apache-2.0, BSD-2-Clause or
BSD-3-Clause. Fourteen files are MIT, two ISC (d3) and two Apache-2.0
(TypeScript, RxJS). A row under any other license is refused by
`tools/corpus/corpus.py` (planted defect 131 puts one in). Nothing is copied
into a repository of ours: `tools/corpus/fetch.sh` (`make fetch-corpus`) downloads
each tarball, checks all three hashes and writes the members under
`third_party/corpus/`, which is ignored, and every gate that reads the files
checks every hash first and fails, naming the script, on a missing, changed or
extra file (planted defect 130).

*What it is for.* `make check-corpus`, in `make test`, parses each file as its
goal from UTF-8 and again from UTF-16 and requires the same answer, parses it
with the tree builder and the checking builder and requires the same answer,
and reads every function of every tree again from its range in the context the
tree says it had and requires the first parse's tree: 47,992 functions of
eleven kinds (async generators and field initializers do not occur in the
corpus; test262's gate has them). The benchmark cases over the corpus
(`bench/bench_corpus.c`) and the seeds of a fuzz campaign (`tests/fuzz/corpus-seeds.py`)
read it, and so does the Node differential. `tools/measure-corpus.c` measures
what the corpus asks of the limits' defaults, by bisecting each field through
the public parse entry:

| Per byte of source, the most | Value | At | Default |
| --- | --- | --- | --- |
| Tokens | 0.536 | core-js's minified bundle | 48 Mi, which admits a 64 MiB source of it with a margin of 1.33 |
| Nodes | 0.401 | lodash, minified | 16 Mi, which a minified 64 MiB file passes at about 27 million |
| Peak memory of a tree-builder parse | 41 bytes | lodash, minified (9 for the formatted ones) | |
| Longest literal | 22,288 code points | three.js, a data string | 16 Mi |
| Deepest nesting | 54 levels | TypeScript | 256, which is 4.7 times it |

The defaults stand. The token margin is narrower than the 1.6 times the first
sample gave (that sample's densest file was 0.470; this corpus's is 0.536) and
is still above one; a source of the default `source_units` cap that is entirely
that dense is refused for `tokens` before `source_units`, which is stated and
deliberate. The literal default is still chosen from cost and not from a
maximum (the corpus has no file that inlines binary data). The nesting default
needs no raising for any file here (owner question Q-s4-2 asks for the
owner's confirmation of the figures).

*Rejected:* committing the files (their licenses want attribution that a
fetch keeps beside the file, and they are twenty megabytes); a gate that
fetches (it would pass with a network and fail without); the registry's
newest version of each package (a corpus that moves is not one whose first
figures can be compared); the files of the Node.js installation the first
sample used (they are whatever was installed).

## The Node differential

test262 says what the language requires, and the two builders check each other.
Neither is a second implementation reading the same text, so a decision nobody
wrote a test for, and a program nobody wrote, could be wrong in both.
`make check-node-differential` asks one: V8, through a pinned Node, whose source
is never read (Node is a reference for behavior only).

*The oracle.* The image `tools/node-oracle/PIN` names by digest,
`docker.io/library/node:24-slim@sha256:0e0ff4...`, which reports Node v24.21.0,
V8 13.6.233.17-node.53 and Unicode 17.0 (the data version this library is pinned
to), started with `--regexp-interpret-all`, `--experimental-vm-modules` and
`--no-warnings`. It is the pin the regular-expression library uses for its
ECMAScript oracle, for the same reason: V8's regular-expression interpreter and
compiler disagreed in an earlier version and the flag keeps the answer one
whatever replaces it. A newer V8 would be a new pin with its own triage (it
ships more of the 17th edition and answers some rows below differently) and a
written reason; none is justified here. The oracle prints its version and
flags and the gate requires them to equal the pin and prints them beside the
result.

*What it asks.* A script is compiled with `vm.Script` and a module parsed with
`vm.SourceTextModule`, and neither is run or linked, so what is compared is
the accept or refuse decision, early errors included. The inputs are the 81,624
in-scope test262 pairs (each in its mode: sloppy, strict, module, raw), the
corpus (each Script file also in strict mode), the fuzz seeds (decoded as the
harnesses read them; the eval and dynamic-function inputs, the built-in mode and
ill-formed text are not comparable and are counted), and 20,000 generated
programs (`tools/gen-programs.py`: a grammar of the statements and expressions
with the contextual keywords, a slash that may be division, a line break where a
semicolon may be inserted, arrow and pattern candidates, classes and templates,
damaged by one token edit in half of them) in sloppy, strict and module mode:
142,415 inputs.

*What it finds.* Every disagreement is a row of `tests/data-node-differences.tsv`
or the gate fails; a row that explains no disagreement fails it too, and a row
of a class that blames this library is refused, because a defect of ours is
fixed with a test. Two classes exist: `edition`, a syntax V8 and the pinned
edition decide differently, and `v8-defect`, citing the clause. There are
fourteen rows, all of one kind: **a call as an assignment target in strict
code.** Clause B.3.9 makes it a runtime ReferenceError only in code that is not
strict, and an early error in strict code; V8 accepts it in both. test262 says so
(eight of its tests, which V8 fails), and so do three generated programs, in strict
and module mode (six inputs). The first run over 150,000 generated programs found two more
shapes of disagreement and no defect of ours: `using` declarations, which the
pinned V8 implements and the 17th edition does not have (so the generator does
not write one: every program that had one disagreed, which said nothing else),
and `a<!--b` in a Module, where V8 refuses with "HTML comments are not allowed in
modules" a program the Module grammar reads as `a < !(--b)`: a V8 defect against
the grammar's tokens, two programs in 150,000 (it needs the rows `gen:086877` and
`gen:094244` if the generator's count is raised; at 20,000 there is none).

*Checked offline.* `make check-node-selftest`, in `make test`, runs the
comparison on canned answers (an unclassified disagreement in either direction
fails, a row for the other direction or mode does not cover it, an unused row
fails, a short answer fails), parses the pin, loads the table, and requires the
generator to be deterministic, because the table names generated programs by
index. The oracle itself needs a container engine and the pinned image
(`make fetch-node-oracle`), so it is not part of `make test`. Planted defects
132 (`delete x` accepted in strict code) and 133 (a disagreement without a row)
are seen to fail it.

## test262

The conformance suite is `tc39/test262`, the whole repository, at one pinned
commit. It is never committed: `tools/test262/fetch.sh` (`make fetch-test262`)
downloads that commit into the ignored `third_party/test262/<commit>/`, and
`tools/test262/SHA256SUMS` lists the SHA-256 of every file the commit holds
(56,485 of them). `make test` never fetches; a corpus that is absent fails
the gate and names the script, and one with a missing, altered or extra file
fails it too, so a published score names the corpus it was measured over.

### The pin

The commit is `e91219cb6b87865703ce8619b5bc0f7c2bf608e1`, committed
2026-03-30T23:42:41Z. It was chosen this way, and the way is the reason it
can be moved by rule rather than by taste:

1. test262 has no edition tags, so the edition is fixed on the specification's
   side. ECMA-262's `es2026` tag is a release branch, cut from `main` on
   2026-03-31T05:40:50Z (the merge base of the two, by GitHub's compare), plus
   one editorial commit that adds the edition's introduction on 2026-07-27.
   `main` is 213 commits ahead of that branch point. The 17th edition's
   normative text is `main` at the branch point.
2. The pin is the last test262 commit at or before that instant. A later
   commit can carry tests for ECMA-262 changes the 17th edition does not have,
   and a test for a normative change that has no `features:` tag cannot be
   told from one for the edition. Tests for the edition's own additions were
   written before the edition cut, so they are all present: the tags that the
   edition's introduction names (`Math.sumPrecise`, `iterator-sequencing`,
   `Array.fromAsync`, `Error.isError`, `upsert`, `uint8array-base64`,
   `json-parse-with-source`) each have tests at the pin.
3. The sibling regular-expression library pins its own commit
   (`35d566604512`, 2026-09-17) for the RegExp tree only. The two are not the
   same commit: that one is later than the branch point by the reasoning
   above, and each library's pin moves with its own work. They are named side
   by side here, and moving either is deliberate.

Moving the pin is deliberate and recorded: change `TEST262_COMMIT`, run
`tools/test262/fetch.sh --write-sums`, and review the list's difference. New
tests that fail are findings, not regressions.

### Classification

`tests/data-test262-classification.tsv` names every file of the corpus, in
order, and the first row that matches decides. A file no row names fails the
gate, and so does a row that matches no file. A file is a test (in scope, in
the modes its `flags:` select), a harness file (parsed as a Script, sloppy and
strict), a fixture (an imported module, parsed as a Module and in scope when
a test or fixture that imports it is), or out with a named reason that is
counted and printed. A skip is never a pass. At this pin 42,836 of the 56,485
files are in scope and give 81,624 (file, mode) pairs.

A fixture is in scope by what imports it, because it has no `features:` of
its own: a fixture imported only by tests that are out by tag is out with
that reason, and a fixture nothing imports fails the gate. Nine fixtures are
early errors on purpose (their importing tests expect the import to fail), so
the table names them `fixture-refuse` and the parse of each must be refused.
They were found by asking a second engine for the syntax of every in-scope
fixture and then reading the importing tests; the engine is an oracle for
the table's rows, not a part of the gate.

### The tag table

`tests/data-test262-tags.tsv` maps every `features:` tag at the pin (194, the
tags in test262's `features.txt`, all of which tests carry) to one of three
states: **in** (the 17th edition defines it; the row cites the clause), **out**
(it does not; the row names a string that must not occur in the edition's
text), and **in when the feature is on** (a feature of a later edition that a
build implements behind a feature set). No feature is defined yet, so no real
tag has the third state, and the gate's self-test proves it on a synthetic
row: scored out with the set empty, in with the feature on. The table is
built from the edition's text, never from which tests pass: `make
check-clauses` verifies each `in` row's clause in the concordance and each
`out` row's absence probe in the 17th edition's source. A tag with no row
fails the gate, and so does a row for a tag that nothing carries. Every
edition score is taken with the feature set empty.

### Modes and the score

A test with no flag runs as a sloppy Script and as a strict Script (the text
with `"use strict";` and a newline in front); `onlyStrict`, `noStrict`, `raw`
and `module` select one mode. A header the gate cannot read, a flag it does
not know, or a test with no header and no fixture role is an error, never a
default. `negative: {phase: parse, type: SyntaxError}` passes only when the
parse is refused with `GLECMA_ERR_SYNTAX` in every mode it runs in; every
other test passes only when the parse succeeds (a `resolution` or `runtime`
negative parses; resolving and running need a host). The score is the passing
pairs over the in-scope pairs, printed with the skips by reason.

### The pending ledger

While the parser was being built a pair could not pass, and every commit
passed `make test`. `tests/data-test262-pending.tsv` lists the pairs that cannot
pass yet (a row names a file, or a directory with a trailing `/`, and a mode
or `*`): a failing pair the ledger does not list fails the gate, a listed pair
that now passes fails it too, so the ledger stays exact, and pairs only leave
it. They are printed as their own count, never as passes or skips. A
construct or a check not yet built answered `GLECMA_ERR_UNSUPPORTED`, which
never passes a negative test, so that a parser that refused everything would
score nothing. No such path remains (the last was the Module goal; the one arm
that could still say it, regex's own "a pattern I do not read", is
`GLECMA_ERR_INTERNAL`, because the ECMAScript dialect never answers it), and
the ledger is empty.

The gate runs `tests/t262/t262_driver.c` once over every pair (one process,
one line in and one result out per pair). It is the only place the gate calls
the library. The ledger is empty: the last 869 rows, all Module pairs, left it
with the Module goal.

## The clause concordance

ECMA-262 renumbers its clauses between editions, so a clause cited from
memory is a guess. `documentation/clause-concordance.tsv` maps every clause
anchor (`sec-...`) of the 16th edition to the 17th's number (and the reverse),
following `oldids` through renames. `tools/ecma262/make-concordance.py` builds
it from the two editions' `spec.html`, fetched at pinned commits by
`tools/ecma262/fetch.sh` (checked against `tools/ecma262/SHA256SUMS`); `make
check-clauses` rebuilds it and fails a committed copy that differs.

A citation is written `clause 12.9.4`, `clauses 12.9.4 and 12.8` or `clause
B.1.1`, in a comment or in documentation, with the 17th edition's number; the
rule table's clause strings and the `glecma_rule_clause` assertions in the
tests are citations too. `make check-clauses` fails a citation the
concordance does not have as a 17th-edition clause. It checks that the number
exists and not that it names the topic cited, so a citation is still read
against the concordance's title for it. Nothing else is read as a citation.

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

- **test262**, pinned (see "test262"): the conformance score is the in-scope
  tests that pass, with the out-of-scope tests listed by reason.
- **Unit tests** that cite the clause they test, so that a test states a
  requirement rather than the code's current behavior.
- **Fuzzing** of the tokenizer, the parser and the compiler with libFuzzer,
  each harness with a seed corpus that `make test` replays, and a campaign of a
  day per harness at the close of each milestone.
- **A differential against another engine's behavior**: the outputs of an
  independent engine for the same program, used as an oracle. Its source is
  never read; only its answers are compared. For the front end it is a pinned
  Node, over test262, a real-world corpus, the fuzz seeds and generated
  programs (see "The Node differential").
- **Planted defects**: every gate is shown to fail on a defect put where it
  looks, by a registry that `make check-planted` applies (`tools/planted/`).
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
| `GLECMA_ERR_SYNTAX` | Appended after the shared nine: well-formed source text that is not valid ECMAScript for its goal, a grammar error or an early error. It departs from the suite's section 5 vocabulary, which calls a front end's compile error `ERR_FORMAT`, and its value is nine, as `GRCORE_ERR_GUEST`'s is, so a `GLECMA_Result` and a `GRCORE_Result` are never held in one variable (`core.h` says so) |
| `GLECMA_ERR_UNSUPPORTED` | A feature the build does not define: a name in a feature set that the build lacks (every non-empty set, today), which the failure record names; and an operation a kind of stream cannot do (a seek or a size on a callback stream). It never means work not yet built: no such path remains |

Outputs are written only on success, and a refusal changes nothing.

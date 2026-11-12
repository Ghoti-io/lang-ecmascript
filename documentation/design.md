# Design

**Status:** Design, with the scaffolding built. The library registers an
ECMAScript engine with a runtime-core context and reports its version and
target edition. It does not yet parse or run any script. Everything below the
"What exists" section describes intended work, and each part moves into
"What exists" in the commit that builds it.

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

The descriptor's callbacks are all NULL, which runtime-core reads as "the
engine has nothing to say": every slot is raw, frames have no source
location, and there are no scopes. The interpreter fills them in as its
frames acquire slots, scopes and locations.

Registration is idempotent where `grcore_engine_register` is not: a realm, a
module loader and a debugger can each make sure the engine is present
without agreeing which goes first. The ownership and running-state checks are
applied before the lookup as well as by runtime-core, so a second call
refuses what the first would have refused.

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
   held as code points for the tokenizer. String values are sequences of
   UTF-16 code units, as the specification defines them, including lone
   surrogates.
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
  early error will likewise get a result of its own, so that "this is not
  valid ECMAScript" is distinct from a caller's mistake.

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
| `GLECMA_ERR_INVALID` | A bad caller argument: NULL where a pointer is required, a context the caller does not own, a context that is running, or one that does not hold the engine (`glecma_engine_find`) |
| `GLECMA_ERR_LIMIT` | A budget or limit refused the operation; for registration, the context's memory budget refused the engine table |
| `GLECMA_ERR_OOM` | An allocation failed outside any budget |
| `GLECMA_ERR_INTERNAL` | The library's own invariant failed, including a runtime-core result this library has no counterpart for |

Outputs are written only on success, and a refusal changes nothing.

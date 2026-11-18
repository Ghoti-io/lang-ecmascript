#!/usr/bin/env python3
# Regenerates the seed corpora under tests/fuzz/corpus/. Each seed is the
# option byte the harness reads first (see fuzz_tokenizer.c) followed by the
# source, as UTF-8 or as UTF-16 in little-endian order. The seeds are small
# and cover each construct of the lexical grammar, so that a replay exercises
# every scanner and so that a campaign starts from something that lexes.
#
# Usage: make-seeds.py [directory]   (default: tests/fuzz/corpus beside this file)
import os
import sys

SNIPPETS = {
    "punctuators": "{ } ( ) [ ] . ... ; , < > <= >= == != === !== + - * / % ** ++ -- << >> >>> & | ^ ! ~ && || ?? ? ?. : = += -= *= /= %= **= <<= >>= >>>= &= |= ^= &&= ||= ??= =>",
    "keywords": "break case catch class const continue debugger default delete do else enum export extends false finally for function if import in instanceof new null return super switch this throw true try typeof var void while with yield await let static async of get set",
    "names": "$a _b c1 \\u0061bc a\\u{62}c été αβγ \U00020BB7 #priv #\\u0061 x‍y",
    "numbers": "0 1 123 1_000 0.5 .5 5. 5.e3 1e10 1E+10 0x1F 0b101 0o17 0n 123n 0x1Fn 017 089 08.5 0.0_1e1_0",
    "bad-numbers": "0x 1_ 1__0 0_1 3in 1.5n 0b12 1e+ 08_9",
    "strings": "'a' \"b\" '\\n\\t\\x41\\u0041\\u{1F600}' '\\0' '\\07' '\\8' 'a\\\nb' '\\uD800' \" \"",
    "bad-strings": "'abc\n'",
    "bad-escapes": "'\\x4' '\\u{110000}'",
    "templates": "`a` `a${b}c${`d${e}`}f` `\\u{` `\\01` `a\r\nb` `$` `\\${x}` `\\\\`",
    "regexps": "x = /[/]\\//gi; y = a / b / c; z = /=/u.test(w)",
    "comments": "// line\n/* block */ /* multi\nline */ x <!-- html open\n--> html close\ny",
    "hashbang": "#!/usr/bin/env node\nlet x = 1;\n",
    "terminators": "a\nb\rc\r\nd e f",
    "program": "'use strict';\nconst f = (a, b = 2, ...c) => a ** b + c.length;\nclass A { #p = 1; static s = `t${f(1)}`; get p() { return this.#p; } }\nasync function* g() { for await (const x of y) yield x?.z ?? 0; }\nexport default A;\n",
    "builtin": "%ArrayPrototype(x) + %B + a % b\n",
    "unterminated-comment": "a /* never",
    "unterminated-template": "`a${b}c",
    "lone-percent": "% %= %1",
}

# What the parser's harness reads (fuzz_parser.c): programs that between them
# use every production, and the constructs that need the parser to decide
# (the cover grammars, automatic semicolon insertion, the directive prologue).
PARSER_SNIPPETS = {
    "expressions": "a ? b : c ?? d || e && f | g ^ h & i == j < k << l + m * n ** o; !p; typeof q; x = y = z; x += 1; (a, b); new A(b).c; a?.b?.[c]?.(d); a`b${c}`; `a${`b${c}`}`; /re/g.test(a) / b / c;\n",
    "arrows": "x => x; (a, b = 1, ...c) => a; async y => await y; async (a) => { await a }; (a = (b = c) => d) => e; ({a = 1}) => a; ([a, [b]]) => a; async(a, b); (x => 1)(2);\n",
    "patterns": "[a, b] = c; [a, ...b] = c; ({a, b: [c], d = 1, ...e} = f); for ([a] of b); for ({a = 1} of b); var [a = 1, {b}] = c; let {a, ...b} = c; [{}[x], (y)] = z;\n",
    "statements": "var a = 1, b; let c; const d = 2; if (a) b; else c; for (;;) break; for (var i = 0; i < 1; i++) continue; for (a in b); for (a of b); while (a) b; do a; while (b) switch (a) { case 1: default: } try { a } catch (e) { b } finally { c } l: for (;;) break l; with (a) b; throw a; debugger;\n",
    "functions": "function f(a, b = 1, ...c) { return a } function* g() { yield 1; yield* x } async function h() { await y } async function* i() { for await (const a of b) yield a } x = function () {}; y = async function* () {}; z = { get a() {}, set a(v) {}, async b() {}, *c() {}, [d]() {} };\n",
    "classes": "class A { constructor() {} static m() {} get g() {} set g(v) {} async *a() {} static #p = 1; #q; [k]() {} static { x } y = 1; static z; 'str'() {} 1() {} } class B extends A { m() { super.m(); } constructor() { super(); new.target; #q in this } }\n",
    "asi": "a\nb\n++c\n/d/g.test(e)\nvar f = 1\nvar g = 2\nlet h = f\n(g)\nthrow\nx\n",
    "cover-errors": "({a = 1}); (a, b) => ; [a = 1] ; ({a} = {b = 1}); (...a); (a,) ; () ; async (a)\n=> 1;",
    "directives": "'use strict'; 010; with (a) b; function f() { '\\07'; 'use strict'; }",
    "annexb": "if (a) function f() {} l: function g() {} for (var x = 1 in y); a <!-- b\n--> c\n",
    "contextual": "var async, get, set, of, let, yield, await, static; x = { async, get, set, async() {}, get() {}, get a() {} }; async\nfunction f() {} for (async of => 1;;); for (let in a); let\nq = 1;",
    "nested": "((((((((((((((((((((x))))))))))))))))))));\n[[[[[[[[[[[[[[[[[[[[x]]]]]]]]]]]]]]]]]]]];\n!!!!!!!!!!!!!!!!!!!!x;\nfunction f() { function g() { function h() { function i() { function j() {} } } } }\n(a = (b = (c = (d = () => 1) => 2) => 3) => 4) => 5;\n",
    "builtin": "%ArrayPrototype(x); y = %B + a % b;\n",
    "templates": "`a${b}c${`d${`e${f}`}`}`; a`\\u{`; `\\01`;",
    "early-valid": "{ function f() {} function f() {} } try {} catch (e) { var e; } for (let a of b) { let a; var c; } switch (a) { case 1: function g() {} case 2: function g() {} } class D { #p; get #q() {} set #q(v) {} static { var s; } m() { this.#p; #q in this; return () => super.x; } } function h(a, a) { return /(?<n>x)\\k<n>[a-z]+/u.test(a); } outer: for (;;) { inner: while (1) { continue outer; } }\n",
    "early-invalid": "let a; let a; break; { function f() {} let f; } function g(a = 1) { 'use strict'; } class C { #a; #a; constructor() {} constructor() {} } this.#b; a: a: ; try {} catch ([e]) { var e; } /(/; /a/gg; /[z-a]/; return; super.x; new.target; delete x; with (a) b;\n",
}

# (label, option byte) for the parser: bit 0 UTF-16, 1 strict, 2 stream,
# 3 built-in, 4 checking builder, 5-6 limits, 7 feature set (fuzz_parser.c).
PARSER_OPTIONS = [
    ("sloppy", 0x00), ("strict", 0x02), ("utf16", 0x01), ("stream", 0x04),
    ("stream-utf16", 0x05), ("builtin", 0x0A), ("checking", 0x10),
    ("checking-strict", 0x12), ("tiny", 0x20), ("stack", 0x40),
    ("tokens", 0x60), ("feature", 0x80),
]

# (name, option byte): the byte is bit 0 UTF-16, 1 strict, 2 module,
# 3 built-in, 4-5 limits, 6-7 variant (see fuzz_tokenizer.c).
OPTIONS = [
    ("sloppy", 0x00),
    ("strict", 0x02),
    ("module", 0x06),
    ("builtin", 0x0A),
    ("tiny", 0x10),
    ("small", 0x22),
    ("rotating", 0x42),
    ("twice", 0x80),
    ("feature", 0xC0),
    ("utf16", 0x01),
    ("utf16-strict", 0x03),
]

def write(path, option, text, wide, second=None):
    data = text.encode("utf-16-le" if wide else "utf-8", "surrogatepass")
    header = bytes([option]) + (bytes([second]) if second is not None else b"")
    with open(path, "wb") as f:
        f.write(header + data)

def main():
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "corpus")
    for sub in ("tokenizer", "source", "parser"):
        os.makedirs(os.path.join(root, sub), exist_ok=True)
    for name, text in SNIPPETS.items():
        for label, option in OPTIONS:
            # The second byte of a tokenizer seed: bit 0 reads the source from the stream.
            write(os.path.join(root, "tokenizer", "%s-%s.bin" % (name, label)), option, text, option & 1, 0)
        write(os.path.join(root, "tokenizer", "%s-stream.bin" % name), 0x00, text, False, 1)
        write(os.path.join(root, "tokenizer", "%s-stream-utf16.bin" % name), 0x01, text, True, 1)
    # The source harness: UTF-8 and UTF-16, a small cap, and ill-formed UTF-8.
    for name, text in SNIPPETS.items():
        write(os.path.join(root, "source", "%s-utf8.bin" % name), 0x00, text, False)
        write(os.path.join(root, "source", "%s-utf16.bin" % name), 0x01, text, True)
        write(os.path.join(root, "source", "%s-stream.bin" % name), 0x04, text, False)
        write(os.path.join(root, "source", "%s-stream-utf16.bin" % name), 0x05, text, True)
    for name, text in list(SNIPPETS.items()) + list(PARSER_SNIPPETS.items()):
        for label, option in PARSER_OPTIONS:
            write(os.path.join(root, "parser", "%s-%s.bin" % (name, label)), option, text, option & 1)
    write(os.path.join(root, "source", "program-cap.bin"), 0x02, SNIPPETS["program"], False)
    for label, raw in (("overlong", b"a\xc0\x80b"), ("surrogate", b"\xed\xa0\x80"), ("truncated", b"x\xf0\x9f"), ("high", b"\xf5\x80\x80\x80")):
        with open(os.path.join(root, "source", "bad-%s.bin" % label), "wb") as f:
            f.write(bytes([0x00]) + raw)
    # A lone surrogate in UTF-16.
    with open(os.path.join(root, "source", "lone-surrogate.bin"), "wb") as f:
        f.write(bytes([0x01]) + "'".encode("utf-16-le") + b"\x00\xd8" + "'".encode("utf-16-le"))

main()

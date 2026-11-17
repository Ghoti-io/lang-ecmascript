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

def write(path, option, text, wide):
    data = text.encode("utf-16-le" if wide else "utf-8", "surrogatepass")
    with open(path, "wb") as f:
        f.write(bytes([option]) + data)

def main():
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "corpus")
    for sub in ("tokenizer", "source"):
        os.makedirs(os.path.join(root, sub), exist_ok=True)
    for name, text in SNIPPETS.items():
        for label, option in OPTIONS:
            write(os.path.join(root, "tokenizer", "%s-%s.bin" % (name, label)), option, text, option & 1)
    # The source harness: UTF-8 and UTF-16, a small cap, and ill-formed UTF-8.
    for name, text in SNIPPETS.items():
        write(os.path.join(root, "source", "%s-utf8.bin" % name), 0x00, text, False)
        write(os.path.join(root, "source", "%s-utf16.bin" % name), 0x01, text, True)
    write(os.path.join(root, "source", "program-cap.bin"), 0x02, SNIPPETS["program"], False)
    for label, raw in (("overlong", b"a\xc0\x80b"), ("surrogate", b"\xed\xa0\x80"), ("truncated", b"x\xf0\x9f"), ("high", b"\xf5\x80\x80\x80")):
        with open(os.path.join(root, "source", "bad-%s.bin" % label), "wb") as f:
            f.write(bytes([0x00]) + raw)
    # A lone surrogate in UTF-16.
    with open(os.path.join(root, "source", "lone-surrogate.bin"), "wb") as f:
        f.write(bytes([0x01]) + "'".encode("utf-16-le") + b"\x00\xd8" + "'".encode("utf-16-le"))

main()

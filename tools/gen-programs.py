#!/usr/bin/env python3
"""Generated programs for the Node differential (tools/check-node-differential.py).

A deterministic generator: the same seed and count give the same programs on
every machine, so a classification of a disagreement can name a program by its
index. A program is built from a small grammar of the language's statement and
expression forms, with the pressure points of the lexical and syntactic
grammar over-represented (contextual keywords used as names, a slash that is
division or the start of a regular expression, a line break where a semicolon
may be inserted, a parenthesis that begins an arrow function or does not,
destructuring written as a pattern and as an expression, classes with private
names, templates, optional chaining, labels), and about half of them are then
damaged by one token-level edit (a token deleted, doubled, swapped with its
neighbor or replaced by another), which is where the refused programs, and the
accept-or-refuse disagreements, are.

A `using` declaration is not generated: it is not in the pinned edition and the pinned V8 implements it,
so every program with one would be a disagreement of one kind and say nothing else.

Usage: gen-programs.py --out DIR [--count N] [--seed S]
       writes DIR/NNNNNN.js and prints the number written.

Copyright 2026 by Corey Pennycuff
"""
import argparse
import os
import random
import re

NAMES = ["a", "b", "c", "x", "y", "z", "f", "g", "o", "p", "async", "await", "yield", "let", "static", "of",
         "get", "set", "from", "as", "target", "meta", "eval", "arguments", "undefined", "implements",
         "interface", "package", "private", "protected", "public", "enum", "NaN", "$", "_", "\\u0061",
         "\\u{62}", "café", "αβ"]
NUMBERS = ["0", "1", "42", "1_000", "0.5", ".5", "5.", "1e3", "0x1F", "0b101", "0o17", "0n", "10n", "07", "089",
           "1e+3", "0.0_1"]
STRINGS = ["'a'", '"b"', "'\\n'", "'\\x41'", "'\\u{1F600}'", "'\\0'", "'\\07'", "'\\8'", "'use strict'",
           "'a\\\nb'"]
BINARY = ["+", "-", "*", "/", "%", "**", "==", "!=", "===", "!==", "<", ">", "<=", ">=", "&&", "||", "??", "&",
          "|", "^", "<<", ">>", ">>>", "in", "instanceof"]
ASSIGN = ["=", "+=", "-=", "*=", "/=", "%=", "**=", "<<=", ">>=", ">>>=", "&=", "|=", "^=", "&&=", "||=", "??="]
UNARY = ["!", "~", "+", "-", "typeof ", "void ", "delete ", "++", "--", "await ", "yield "]
MUTATION_TOKENS = ["(", ")", "{", "}", "[", "]", ";", ",", ".", "=>", "=", "?", ":", "...", "*", "async", "await",
                   "yield", "let", "static", "class", "function", "new", "super", "this", "#p", "?.", "`", "/",
                   "'", "\"", "\n", "in", "of", "var", "const", "import", "export", "default", "if", "else",
                   "for", "while", "return", "break", "continue", "with", "delete", "typeof", "0", "1", "a"]


class Gen:
    def __init__(self, rng):
        self.r = rng

    def pick(self, items):
        return items[self.r.randrange(len(items))]

    def name(self):
        return self.pick(NAMES)

    def expr(self, d):
        r = self.r
        if d <= 0:
            return self.atom(0)
        k = r.randrange(26)
        if k < 4:
            return self.atom(d)
        if k == 4:
            return "%s %s %s" % (self.expr(d - 1), self.pick(BINARY), self.expr(d - 1))
        if k == 5:
            return "%s %s %s" % (self.lhs(d - 1), self.pick(ASSIGN), self.expr(d - 1))
        if k == 6:
            return "%s%s" % (self.pick(UNARY), self.expr(d - 1))
        if k == 7:
            return "%s ? %s : %s" % (self.expr(d - 1), self.expr(d - 1), self.expr(d - 1))
        if k == 8:
            return "(%s, %s)" % (self.expr(d - 1), self.expr(d - 1))
        if k == 9:
            return "%s(%s)" % (self.expr(d - 1), self.args(d - 1))
        if k == 10:
            return "%s.%s" % (self.expr(d - 1), self.name())
        if k == 11:
            return "%s[%s]" % (self.expr(d - 1), self.expr(d - 1))
        if k == 12:
            return "%s?.%s" % (self.expr(d - 1), self.pick([self.name(), "[0]", "(1)", "#p"]))
        if k == 13:
            return "new %s(%s)" % (self.name(), self.args(d - 1))
        if k == 14:
            return self.arrow(d - 1)
        if k == 15:
            return "[%s]" % self.args(d - 1)
        if k == 16:
            return self.object(d - 1)
        if k == 17:
            return "`%s${%s}%s`" % (self.pick(["", "t", "\\u{", "\\01", "$"]), self.expr(d - 1), self.pick(["", "u", "\\x"]))
        if k == 18:
            return "%s`%s${%s}`" % (self.name(), "t", self.expr(d - 1))
        if k == 19:
            return self.function(d - 1, expression=True)
        if k == 20:
            return self.klass(d - 1, expression=True)
        if k == 21:
            return self.pick(["/re+/g", "/[/]/", "/(?<n>a)\\k<n>/u", "/a{2,1}/", "/(/", "/\\p{L}/u", "/[a-z]/v", "/a/gg",
                              "/(?:a)|b/s"])
        if k == 22:
            return "%s %s %s" % (self.name(), self.pick(["/", "/ 2 /", "++ /", "\n/"]), self.name())
        if k == 23:
            return "async (%s) => %s" % (self.params(d - 1), self.expr(d - 1))
        if k == 24:
            return "import(%s)" % self.expr(d - 1)
        return "%s" % self.pick(["new.target", "import.meta", "super.x", "super()", "this", "arguments", "#p in this"])

    def atom(self, d):
        r = self.r.randrange(6)
        if r == 0:
            return self.name()
        if r == 1:
            return self.pick(NUMBERS)
        if r == 2:
            return self.pick(STRINGS)
        if r == 3:
            return self.pick(["null", "true", "false", "this", "undefined"])
        if r == 4:
            return self.pick(["[]", "{}", "[,]", "[a, , b]", "({})"])
        return self.name()

    def lhs(self, d):
        k = self.r.randrange(8)
        if k < 3:
            return self.name()
        if k == 3:
            return "%s.%s" % (self.name(), self.name())
        if k == 4:
            return "[%s]" % self.pattern_items(d)
        if k == 5:
            return "({%s})" % self.pattern_props(d) if self.r.randrange(2) else "{%s}" % self.pattern_props(d)
        if k == 6:
            return self.pick(["f()", "(a)", "this", "new.target", "a?.b", "((a))", "[a] = [b]"])
        return "%s[%s]" % (self.name(), self.expr(0))

    def pattern_items(self, d):
        return ", ".join(self.pick([self.name(), "%s = %s" % (self.name(), self.expr(0)), "...%s" % self.name(), "",
                                    "[%s]" % self.name(), "{%s}" % self.name()])
                         for _ in range(self.r.randrange(1, 4)))

    def pattern_props(self, d):
        return ", ".join(self.pick([self.name(), "%s = %s" % (self.name(), self.expr(0)), "%s: %s" % (self.name(), self.name()),
                                    "...%s" % self.name(), "[%s]: %s" % (self.name(), self.name())])
                         for _ in range(self.r.randrange(1, 4)))

    def args(self, d):
        return ", ".join(self.pick([self.expr(d), "...%s" % self.expr(0)]) for _ in range(self.r.randrange(0, 3)))

    def params(self, d):
        return ", ".join(self.pick([self.name(), "%s = %s" % (self.name(), self.expr(0)), "...%s" % self.name(),
                                    "[%s]" % self.name(), "{%s}" % self.name(), "{%s} = {}" % self.name()])
                         for _ in range(self.r.randrange(0, 4)))

    def arrow(self, d):
        body = self.pick([self.expr(d), "{ %s }" % self.stmt(d, 0), "({})"])
        head = self.pick([self.name(), "(%s)" % self.params(d), "async %s" % self.name(), "async (%s)" % self.params(d)])
        return "%s => %s" % (head, body)

    def object(self, d):
        members = []
        for _ in range(self.r.randrange(0, 4)):
            members.append(self.pick([
                self.name(), "%s: %s" % (self.name(), self.expr(d)), "%s = %s" % (self.name(), self.expr(0)),
                "[%s]: %s" % (self.expr(0), self.expr(d)), "%s() {}" % self.name(), "get %s() { return 1 }" % self.name(),
                "set %s(v) {}" % self.name(), "async %s() {}" % self.name(), "*%s() {}" % self.name(),
                "async *%s() {}" % self.name(), "...%s" % self.expr(0), "'s': 1", "1: 2", "__proto__: null",
                "__proto__: a, __proto__: b", "get: 1", "set: 2", "async: 3"]))
        return "{%s}" % ", ".join(members)

    def function(self, d, expression=False):
        kind = self.pick(["function", "function*", "async function", "async function*"])
        name = self.pick([self.name(), "", self.name()]) if expression else self.name()
        return "%s %s(%s) { %s }" % (kind, name, self.params(d), self.stmts(d, self.r.randrange(0, 3)))

    def klass(self, d, expression=False):
        members = []
        for _ in range(self.r.randrange(0, 5)):
            members.append(self.pick([
                "%s() {}" % self.name(), "static %s() {}" % self.name(), "get %s() { return 1 }" % self.name(),
                "set %s(v) {}" % self.name(), "#p = %s;" % self.expr(0), "static #q;", "#m() {}", "get #g() { return 1 }",
                "set #g(v) {}", "%s = %s;" % (self.name(), self.expr(0)), "static { %s }" % self.stmts(d, 1),
                "constructor() { %s }" % self.pick(["", "super();", "this.x = 1;"]), "static constructor() {}",
                "[%s]() {}" % self.expr(0), "async *%s() {}" % self.name(), "static async %s() {}" % self.name(),
                "'quoted'() {}", "42() {}", "accessor x;", "static static() {}", "static async static() {}",
                "async\n%s() {}" % self.name(), "get\n%s() {}" % self.name()]))
        head = self.pick(["class %s" % self.name(), "class %s extends %s" % (self.name(), self.name()), "class extends %s" % self.name()]) \
            if not expression else self.pick(["class", "class %s" % self.name(), "class extends %s" % self.name()])
        return "%s { %s }" % (head, "\n".join(members))

    def stmt(self, d, label):
        r = self.r
        k = r.randrange(34)
        if d <= 0:
            return "%s;" % self.expr(0)
        if k < 5:
            return "%s;" % self.expr(d)
        if k == 5:
            return "%s %s = %s;" % (self.pick(["var", "let", "const"]), self.name(), self.expr(d - 1))
        if k == 6:
            return "%s %s;" % (self.pick(["var", "let"]), ", ".join(self.name() for _ in range(r.randrange(1, 3))))
        if k == 7:
            return "%s [%s] = %s;" % (self.pick(["var", "let", "const"]), self.pattern_items(d), self.expr(0))
        if k == 8:
            return "%s {%s} = %s;" % (self.pick(["var", "let", "const"]), self.pattern_props(d), self.expr(0))
        if k == 9:
            return "if (%s) %s%s" % (self.expr(d - 1), self.stmt(d - 1, 0), self.pick(["", " else %s" % self.stmt(d - 1, 0)]))
        if k == 10:
            return "for (%s; %s; %s) %s" % (self.pick(["", "var i = 0", "let i = 0", "i = 0", "let [a] = b", "const x = 1"]),
                                            self.pick(["", "i < 1", "a"]), self.pick(["", "i++", "a"]), self.stmt(d - 1, 0))
        if k == 11:
            return "for (%s %s %s) %s" % (self.pick(["var x", "let x", "const x", "x", "let [x]", "var {x}", "x.y", "async", "let", "(x)", "[x]", "{x}", "var x = 1", "let x = 1"]),
                                          self.pick(["in", "of"]), self.expr(0), self.stmt(d - 1, 0))
        if k == 12:
            return "for %s(%s %s %s) %s" % (self.pick(["await ", ""]), self.pick(["const x", "x", "let y"]), "of", self.expr(0), self.stmt(d - 1, 0))
        if k == 13:
            return "while (%s) %s" % (self.expr(d - 1), self.stmt(d - 1, 0))
        if k == 14:
            return "do %s while (%s)%s" % (self.stmt(d - 1, 0), self.expr(d - 1), self.pick([";", "", "\n"]))
        if k == 15:
            return "{ %s }" % self.stmts(d - 1, r.randrange(0, 3))
        if k == 16:
            return self.function(d - 1)
        if k == 17:
            return self.klass(d - 1) if self.r.randrange(2) else "%s;" % self.expr(d)
        if k == 18:
            return "try { %s } catch %s{ %s }%s" % (self.stmt(d - 1, 0), self.pick(["", "(e) ", "([e]) ", "({e}) ", "(e = 1) "]), self.stmt(d - 1, 0),
                                                  self.pick(["", " finally { }"]))
        if k == 19:
            return "switch (%s) { case %s: %s default: %s case 1: }" % (self.expr(0), self.expr(0), self.stmt(d - 1, 0), self.stmt(d - 1, 0))
        if k == 20:
            return "%s: %s" % (self.pick(["l", "a", "yield", "await", "let", "async"]), self.stmt(d - 1, 0))
        if k == 21:
            return self.pick(["break;", "continue;", "break l;", "continue l;", "return;", "return %s;" % self.expr(0), "throw %s;" % self.expr(0),
                              "return\n%s" % self.expr(0), "throw\n1;", "debugger;", ";", "break\nl;"])
        if k == 22:
            return "with (%s) %s" % (self.expr(0), self.stmt(d - 1, 0))
        if k == 23:
            return self.pick(["'use strict';", "'use strict'\n1", "\"use\\x20strict\";", "010;", "'\\07';", "var eval;", "arguments = 1;",
                              "delete x;", "function eval() {}", "var let;", "var yield;", "let\nx = 1;", "let\n[x] = 1;", "async\nfunction f() {}",
                              "if (a) function f() {}", "l: function f() {}", "if (a) ; else function g() {}", "for (;;) function h() {}"])
        if k == 24:
            return self.pick(["import a from 'm';", "import {a as b, c} from 'm';", "import * as n from 'm';", "export default 1;", "export {a};",
                              "export var v = 1;", "export * from 'm';", "export {a as 'b'};", "import 'm';", "export default function () {}",
                              "export default class {}", "export {x as default};", "import {default as d} from 'm';", "import x, * as y from 'm' with {type: 'json'};"])
        if k == 25:
            return "var %s = %s, %s = %s;" % (self.name(), self.expr(0), self.name(), self.expr(0))
        if k == 26:
            return "let %s; let %s;" % (self.name(), self.name())
        if k == 27:
            return "%s\n%s\n" % (self.pick(["a", "x++", "y", "(a)", "`t`", "[1]", "+1"]), self.pick(["(b)", "[c]", "/re/g", "++z", "`t`", "-1", "(function(){})"]))
        if k == 28:
            return "<!-- html comment\n"
        if k == 29:
            return "--> html close\n"
        if k == 30:
            return "/* multi\nline */ --> not at start\n"
        if k == 31:
            return "var %s;" % self.pick(["async", "await", "yield", "let", "static", "of", "get", "set", "enum", "implements"])
        if k == 32:
            return "%s = function %s(%s) { 'use strict'; };" % (self.name(), self.name(), self.params(0))
        return "label: { break label; }"

    def stmts(self, d, n):
        return "\n".join(self.stmt(d, 0) for _ in range(n))

    def program(self):
        n = self.r.randrange(1, 5)
        return "\n".join(self.stmt(3, 0) for _ in range(n)) + "\n"


TOKEN = re.compile(r"""\s+|//[^\n]*|/\*.*?\*/|`(?:\\.|[^`\\])*`|'(?:\\.|[^'\\\n])*'|"(?:\\.|[^"\\\n])*"|[A-Za-z_$#][\w$]*|\d[\w.]*|=>|\.\.\.|\?\.|[-+*/%&|^<>=!]=*|.""", re.S)


def damage(rng, text):
    tokens = TOKEN.findall(text)
    if not tokens:
        return text
    i = rng.randrange(len(tokens))
    k = rng.randrange(4)
    if k == 0:
        del tokens[i]
    elif k == 1:
        tokens.insert(i, tokens[i])
    elif k == 2 and i + 1 < len(tokens):
        tokens[i], tokens[i + 1] = tokens[i + 1], tokens[i]
    else:
        tokens[i] = MUTATION_TOKENS[rng.randrange(len(MUTATION_TOKENS))]
    return "".join(tokens)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", required=True)
    parser.add_argument("--count", type=int, default=20000)
    parser.add_argument("--seed", type=int, default=262)
    args = parser.parse_args()
    rng = random.Random(args.seed)
    gen = Gen(rng)
    os.makedirs(args.out, exist_ok=True)
    for index in range(args.count):
        text = gen.program()
        if rng.randrange(2):
            text = damage(rng, text)
        with open(os.path.join(args.out, "%06d.js" % index), "w", encoding="utf-8", newline="") as handle:
            handle.write(text)
    print(args.count)


if __name__ == "__main__":
    main()

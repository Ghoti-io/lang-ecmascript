#!/usr/bin/env python3
"""Fails when the registry of syntax rule codes (tools/rule-codes.txt) and the
library disagree, or when a code that was already committed has been removed,
renamed or renumbered.

The registry is the thing a host can rely on: the number of a rule, its
enumerator and its stable name. The library's enumeration (core.h) and name
table (failure.c) must hold exactly the registry's rows, in order, and a row
that HEAD already holds must still be there unchanged.

Usage: check-rule-codes.py [root]
"""
import re
import subprocess
import sys


def fail(message):
    print("check-rule-codes: %s" % message, file=sys.stderr)
    sys.exit(1)


def registry_rows(text):
    rows = []
    for line in text.splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) != 3 or not parts[0].isdigit():
            fail("malformed registry row: %r" % line)
        rows.append((int(parts[0]), parts[1], parts[2]))
    return rows


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "."
    try:
        rows = registry_rows(open(root + "/tools/rule-codes.txt").read())
        header = open(root + "/include/ghoti.io/lang-ecmascript/core.h").read()
        table = open(root + "/src/core/failure.c").read()
    except OSError as error:
        fail("cannot read an input: %s" % error)
    if not rows:
        fail("the registry holds no rows, so this gate checks nothing")
    enum = re.findall(r"(GLECMA_RULE_[A-Z0-9_]+)\s*=\s*(\d+)\s*,", header)
    names = re.findall(r'\{(GLECMA_RULE_[A-Z0-9_]+),\s*"([a-z0-9-]+)"', table)
    if not enum or not names:
        fail("found no rules in core.h or failure.c; the pattern has rotted")
    enum = [(c, int(n)) for c, n in enum if c != "GLECMA_RULE_COUNT"]
    name_of = dict(names)
    for index, (number, enumerator, name) in enumerate(rows):
        if number != index:
            fail("row %d has number %d: numbers are consecutive from 0" % (index, number))
    built = [(n, c, name_of.get(c)) for c, n in enum]
    if len(built) != len(rows):
        fail("the library has %d rules and the registry %d" % (len(built), len(rows)))
    for have, want in zip(built, rows):
        if have != want:
            fail("rule %d is %s %r in the library and %s %r in the registry"
                 % (want[0], have[1], have[2], want[1], want[2]))
    # Append-only against the committed registry.
    git = subprocess.run(["git", "-C", root, "show", "HEAD:tools/rule-codes.txt"],
                         capture_output=True, text=True)
    if git.returncode == 0:
        old = registry_rows(git.stdout)
        for i, row in enumerate(old):
            if i >= len(rows) or rows[i] != row:
                now = rows[i] if i < len(rows) else None
                fail("committed rule %r is now %r: the registry is append-only" % (row, now))
    print("check-rule-codes: %d rules, registry and library agree" % len(rows))


main()

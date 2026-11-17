#!/usr/bin/env python3
"""The clause-citation gate.

Fails when

  * the committed concordance (documentation/clause-concordance.tsv) is not
    what tools/ecma262/make-concordance.py builds from the two editions'
    fetched source text (so it cannot drift from the text it claims to map);
  * a row of the test262 tag table cites a clause the concordance lacks in the
    17th edition, or an `out` row's absence probe occurs in the 17th edition's
    text (the table is built from the edition's text, and this reads it);
  * a source file, header, test or document cites a clause the concordance
    lacks in the 17th edition.

A citation is written `clause 12.9.4`, `clauses 12.9.4 and 12.8`, or
`clause B.1.1` (any case), in a comment or in documentation; the rule table's
clause strings (src/core/failure.c) and the `glecma_rule_clause` assertions in
the tests are citations too. A clause number is the 17th edition's. A number
written for the 16th edition is found in the concordance's 16th column.
Nothing else is read as a citation.

Usage: check-clauses.py [--root DIR] [--editions DIR]
"""
import argparse
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
NUM = r"(?:[A-F]|\d{1,2})(?:\.\d+)*"
CITE = re.compile(r"\bclauses?\s+(" + NUM + r"(?:(?:\s*,|\s+and|\s+or)\s+" + NUM + r")*)\b", re.I)
NUMBER = re.compile(NUM)
RULE_ROW = re.compile(r'^\s*\{GLECMA_RULE_[A-Z_0-9]+,\s*"[a-z0-9-]+",\s*"([^"]+)"')
RULE_ROW_NEXT = re.compile(r'^\s*"(\d{1,2}(?:\.\d+)+|[A-F](?:\.\d+)+)"')
TEST_CLAUSE = re.compile(r'glecma_rule_clause\([^;]*?,\s*"([^"]+)"\)')
SCAN_DIRS = ("src", "include", "tests", "documentation", "bench", "examples")
SCAN_FILES = ("README.md",)
SKIP = ("documentation/clause-concordance.tsv",)
TEXT_SUFFIXES = (".c", ".h", ".cpp", ".md", ".py", ".sh", ".txt", ".def", ".yaml")


def fail(message):
    print("check-clauses: %s" % message, file=sys.stderr)
    raise SystemExit(1)


def read_concordance(path):
    rows = {}
    n17, n16 = set(), set()
    try:
        text = open(path, encoding="utf-8").read()
    except OSError as error:
        fail("cannot read %s: %s" % (path, error))
    for number, line in enumerate(text.splitlines(), 1):
        if not line or line.startswith("#"):
            continue
        cells = line.split("\t")
        if len(cells) != 5:
            fail("%s:%d: a concordance row has five columns" % (path, number))
        rows[cells[0]] = cells
        if cells[1]:
            n16.add(cells[1])
        if cells[2]:
            n17.add(cells[2])
    if not rows:
        fail("%s holds no rows" % path)
    return rows, n16, n17


def element_text(spec, clause_id):
    """The source text of the clause with this id, nested clauses included."""
    start = spec.find('id="%s"' % clause_id)
    if start < 0:
        return None
    start = spec.rfind("<", 0, start)
    depth = 0
    for m in re.finditer(r"<(/?)(emu-clause|emu-annex)\b[^>]*>", spec[start:]):
        depth += -1 if m.group(1) else 1
        if depth == 0:
            return spec[start:start + m.end()]
    return None


def check_concordance(args, problems):
    editions = {}
    with open(os.path.join(args.root, "tools/ecma262/EDITIONS"), encoding="utf-8") as handle:
        for line in handle:
            if line.strip() and not line.startswith("#"):
                commit, edition = line.split()
                editions[edition] = os.path.join(args.editions, commit, "spec.html")
    for edition, path in sorted(editions.items()):
        if not os.path.exists(path):
            fail("the %s edition's source is not at %s; run tools/ecma262/fetch.sh "
                 "(it needs the network; `make test` never fetches)" % (edition, path))
    built = subprocess.run(
        [sys.executable, os.path.join(args.root, "tools/ecma262/make-concordance.py"),
         editions["16th"], editions["17th"]],
        capture_output=True, text=True)
    if built.returncode != 0:
        fail("make-concordance.py failed: %s" % built.stderr.strip()[:300])
    committed = open(os.path.join(args.root, "documentation/clause-concordance.tsv"),
                     encoding="utf-8").read()
    if built.stdout != committed:
        problems.append("documentation/clause-concordance.tsv is not what "
                        "tools/ecma262/make-concordance.py builds from the pinned sources; "
                        "regenerate it and review the difference")
    with open(editions["17th"], encoding="utf-8") as handle:
        return handle.read()


def check_tags(args, rows, spec17, problems):
    path = os.path.join(args.root, "tests/data-test262-tags.tsv")
    for number, line in enumerate(open(path, encoding="utf-8").read().splitlines(), 1):
        if not line or line.startswith("#"):
            continue
        tag, state, cite, _note = line.split("\t")
        where = "%s:%d (%s)" % ("tests/data-test262-tags.tsv", number, tag)
        if state == "in":
            row = rows.get(cite)
            if row is None or not row[2]:
                problems.append("%s cites %s, which is not a clause of the 17th edition "
                                "in the concordance" % (where, cite))
        elif state == "out":
            if not cite.startswith("absent:"):
                problems.append("%s: an out row's cite is `absent:<text>`" % where)
                continue
            probe = cite[len("absent:"):]
            scope = spec17
            m = re.match(r"^(sec-[A-Za-z0-9.%_-]+):(.+)$", probe)
            if m:
                scope = element_text(spec17, m.group(1))
                probe = m.group(2)
                if scope is None:
                    problems.append("%s: the clause %s is not in the 17th edition" % (where, m.group(1)))
                    continue
            if probe in scope:
                problems.append("%s: the 17th edition's text contains %r, so the tag is "
                                "not out" % (where, probe))
        elif state == "in-when-on":
            if not cite.strip():
                problems.append("%s: an in-when-on row names its feature" % where)


def citations(root):
    """Yield (path, line number, number) for every citation under root."""
    paths = []
    for directory in SCAN_DIRS:
        base = os.path.join(root, directory)
        for dirpath, dirnames, filenames in os.walk(base):
            dirnames[:] = sorted(d for d in dirnames if d not in ("corpus", "out", "__pycache__"))
            for name in sorted(filenames):
                if name.endswith(TEXT_SUFFIXES):
                    paths.append(os.path.join(dirpath, name))
    paths += [os.path.join(root, f) for f in SCAN_FILES if os.path.exists(os.path.join(root, f))]
    for path in paths:
        rel = os.path.relpath(path, root).replace(os.sep, "/")
        if rel in SKIP or rel.startswith("tests/data-test262"):
            continue
        try:
            text = open(path, encoding="utf-8").read()
        except UnicodeDecodeError:
            continue
        # A citation may wrap across a comment's lines: read the text with the
        # comment leaders removed, keeping line numbers.
        for number, line in enumerate(text.splitlines(), 1):
            for m in CITE.finditer(line):
                for n in NUMBER.findall(m.group(1)):
                    yield rel, number, n
            if rel == "src/core/failure.c":
                m = RULE_ROW.match(line) or RULE_ROW_NEXT.match(line)
                if m and re.match(r"^(\d|[A-F]\.)", m.group(1)):
                    yield rel, number, m.group(1)
            for m in TEST_CLAUSE.finditer(line):
                yield rel, number, m.group(1)
        # `clause` at the end of a line and its number at the start of the next
        # comment line: join comment continuations.
        joined = re.sub(r"\n\s*(?://+<?|\*|///<)\s*", " ", text)
        if joined != text:
            seen = set()
            for m in CITE.finditer(joined):
                for n in NUMBER.findall(m.group(1)):
                    seen.add(n)
            for n in sorted(seen):
                yield rel, 0, n


def check_citations(args, n17, problems):
    reported = set()
    for rel, number, n in citations(args.root):
        if n in n17 or (rel, n) in reported:
            continue
        reported.add((rel, n))
        problems.append("%s%s cites clause %s, which the concordance lacks in the 17th edition"
                        % (rel, ":%d" % number if number else "", n))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--root", default=os.path.dirname(HERE))
    parser.add_argument("--editions", default=None)
    args = parser.parse_args()
    args.root = os.path.abspath(args.root)
    if args.editions is None:
        args.editions = os.path.join(args.root, "third_party/ecma262")
    problems = []
    spec17 = check_concordance(args, problems)
    rows, n16, n17 = read_concordance(
        os.path.join(args.root, "documentation/clause-concordance.tsv"))
    check_tags(args, rows, spec17, problems)
    check_citations(args, n17, problems)
    if problems:
        for p in problems[:40]:
            print("check-clauses: %s" % p, file=sys.stderr)
        fail("%d problem(s)" % len(problems))
    print("check-clauses: ok (%d clauses in the concordance, %d in the 17th edition)"
          % (len(rows), len(n17)))


if __name__ == "__main__":
    main()

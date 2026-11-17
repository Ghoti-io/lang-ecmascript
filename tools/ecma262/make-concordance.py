#!/usr/bin/env python3
"""Build the 16th-to-17th clause concordance from the two editions' spec.html.

Reads two ECMA-262 source files (tc39/ecma262's spec.html at the 16th and the
17th edition's commits, fetched by fetch.sh), numbers every clause as ecmarkup
does (nested emu-clause elements count from 1; top-level emu-annex elements
are lettered; the introduction is unnumbered), and writes one row per clause
anchor id found in either edition:

    <id> TAB <16th number> TAB <17th number> TAB <title>

A number is empty where the edition has no such clause. A clause that kept its
anchor id keeps its identity across the renumbering; an anchor an edition
renamed carries its old name in `oldids`, which this tool follows, so a clause
renamed between the editions is one row and not two.

Usage: make-concordance.py SPEC_16TH SPEC_17TH > clause-concordance.tsv
"""
import html
import re
import sys

TAG = re.compile(r"<!--.*?-->|<(/?)(emu-clause|emu-annex|emu-intro)\b([^>]*)>", re.S)
ATTR_ID = re.compile(r'\bid="([^"]+)"')
ATTR_OLD = re.compile(r'\boldids="([^"]+)"')


def letters(n):
    return chr(ord("A") + n - 1)


def parse(path):
    text = open(path, encoding="utf-8").read()
    clauses = {}      # id -> (number, title)
    old = {}          # old id -> current id
    stack = []        # [counter-of-children, number]
    top = 0
    annex_top = 0
    pending_title = []
    for m in TAG.finditer(text):
        if m.group(0).startswith("<!--"):
            continue
        closing, kind, attrs = m.group(1), m.group(2), m.group(3)
        if closing:
            stack.pop()
            continue
        if kind == "emu-intro":
            stack.append([0, ""])
            continue
        if stack:
            stack[-1][0] += 1
            number = (stack[-1][1] + "." if stack[-1][1] else "") + (
                str(stack[-1][0]))
        elif kind == "emu-annex":
            annex_top += 1
            number = letters(annex_top)
        else:
            top += 1
            number = str(top)
        stack.append([0, number])
        ident = ATTR_ID.search(attrs)
        if not ident:
            continue
        # The title is the first heading after the opening tag.
        h = re.compile(r"<h1>(.*?)</h1>", re.S).search(text, m.end())
        title = ""
        if h and h.start() - m.end() < 400:
            title = html.unescape(re.sub(r"<[^>]+>|\s+", " ", h.group(1))).strip()
            # An abstract operation's heading carries its signature; the
            # name is what identifies it.
            title = title.split(" ( ")[0].split(" (")[0].strip()
        cid = ident.group(1)
        if cid in clauses:
            raise SystemExit("%s: clause id %s appears twice" % (path, cid))
        clauses[cid] = (number, title)
        o = ATTR_OLD.search(attrs)
        if o:
            for name in o.group(1).split(","):
                old[name.strip()] = cid
    return clauses, old


def main(argv):
    if len(argv) != 3:
        raise SystemExit(__doc__)
    c16, o16 = parse(argv[1])
    c17, o17 = parse(argv[2])
    rows = {}   # id -> [17th number, title, 16th number, note]
    for cid, (num, title) in c17.items():
        rows[cid] = [num, title, None, ""]
    # A 16th id is the same clause as the 17th id that carries it, or that
    # names it in oldids. When two 16th clauses reach one 17th clause (the
    # edition merged them), the second keeps its own row, pointing at the
    # clause that took it.
    for cid, (num, title) in c16.items():
        if cid in c17:
            rows[cid][2] = num
            continue
        target = o17.get(cid)
        if target is not None and rows[target][2] is None:
            rows[target][2] = num
            rows[target][3] = "was " + cid
        elif target is not None:
            rows[cid] = [rows[target][0], title, num, "merged into " + target]
        else:
            rows[cid] = [None, title, num, "removed"]
    out = ["# The ECMA-262 clause concordance, 16th edition to 17th.",
           "# id\t16th\t17th\ttitle\tnote",
           "# An empty 17th number: the clause is not in the 17th edition. An empty",
           "# 16th number: it is new. A title is the 17th's, or the 16th's for a",
           "# clause only the 16th has."]
    for cid, (n17, title, n16, note) in sorted(rows.items()):
        out.append("%s\t%s\t%s\t%s\t%s" % (cid, n16 or "", n17 or "", title, note))
    sys.stdout.write("\n".join(out) + "\n")


if __name__ == "__main__":
    main(sys.argv)

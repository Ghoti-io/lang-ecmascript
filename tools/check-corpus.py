#!/usr/bin/env python3
"""The real-world corpus gate.

Real JavaScript, not only conformance tests, must parse, and parse the same
every way the library offers. Over the files of tools/corpus/CORPUS.tsv (fetched
by hash, never committed; tools/corpus/fetch.sh) this

  * verifies every file against the hashes the table pins, and the table's
    licenses against the admitted ones;
  * parses each as its goal (a Script as written, a Module under the Module
    goal), from UTF-8 and again from UTF-16, and requires every parse accepted
    and the two encodings to answer alike;
  * parses each with the tree builder and the checking builder
    (tests/t262/t262_driver.c, --differential) and requires the same result;
  * reads every function of every tree again from its range, in the context the
    tree says it had, and requires an equal tree (--reparse): the re-parse
    equality over the real corpus, where test262's gate (check-reparse.py) has
    it over the conformance tests.

It fails when a file is refused, when the builders or the encodings disagree,
when a function reads again differently, and when the corpus holds no file of
one goal or no function of a kind its table says it has.

Usage: check-corpus.py --driver PROGRAM [--root DIR]
"""
import argparse
import importlib.util
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("corpus", os.path.join(HERE, "corpus", "corpus.py"))
corpus = importlib.util.module_from_spec(spec)
spec.loader.exec_module(corpus)

KINDS = ["function", "generator", "async function", "async generator", "arrow",
         "async arrow", "method", "getter", "setter", "constructor",
         "derived constructor", "field initializer", "static block"]


def fail(message):
    print("check-corpus: %s" % message, file=sys.stderr)
    raise SystemExit(1)


def run(driver, flags, rows, root):
    manifest = "".join("%s\t%s\n" % ("module" if r.goal == "module" else "sloppy", r.path(root)) for r in rows)
    done = subprocess.run([driver] + flags, input=manifest.encode("utf-8"), stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, check=False)
    if done.returncode != 0:
        fail("the driver exited %d: %s" % (done.returncode, done.stderr.decode("utf-8", "replace")[:400]))
    answers = done.stdout.decode("utf-8").splitlines()
    if len(answers) != len(rows):
        fail("the driver answered %d lines for %d files" % (len(answers), len(rows)))
    return answers


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--driver", required=True)
    parser.add_argument("--root", default=corpus.DEFAULT_ROOT)
    args = parser.parse_args()
    rows = corpus.verify(args.root)
    if not any(r.goal == "script" for r in rows) or not any(r.goal == "module" for r in rows):
        fail("the corpus holds no file of one of the goals")
    bytes_total = sum(os.path.getsize(r.path(args.root)) for r in rows)

    accepted = run(args.driver, [], rows, args.root)
    wide = run(args.driver, ["--utf16"], rows, args.root)
    problems = []
    for row, a, b in zip(rows, accepted, wide):
        if a != "OK":
            problems.append("%s %s as a %s was refused: %s" % (row.package, row.member, row.goal, a))
        if a != b:
            problems.append("%s %s: UTF-8 answered %s and UTF-16 answered %s" % (row.package, row.member, a, b))
    for row, a in zip(rows, run(args.driver, ["--differential"], rows, args.root)):
        if a != "SAME OK":
            problems.append("%s %s: the builders differ or refuse: %s" % (row.package, row.member, a))
    wide_builders = run(args.driver, ["--differential", "--utf16"], rows, args.root)
    for row, a in zip(rows, wide_builders):
        if a != "SAME OK":
            problems.append("%s %s from UTF-16: the builders differ or refuse: %s" % (row.package, row.member, a))
    functions, kinds = 0, [0] * len(KINDS)
    for row, a in zip(rows, run(args.driver, ["--reparse"], rows, args.root)):
        fields = a.split()
        if not fields or fields[0] != "REPARSED":
            problems.append("%s %s: a function read again differently, or the file was refused: %s" % (row.package, row.member, a[:300]))
            continue
        functions += int(fields[1])
        for i in range(len(KINDS)):
            kinds[i] += int(fields[2 + i])
    if problems:
        for problem in problems[:12]:
            print("check-corpus: " + problem, file=sys.stderr)
        raise SystemExit(1)
    if functions == 0:
        fail("no function was read again, so the equality was not checked")
    present = [name for name, count in zip(KINDS, kinds) if count]
    print("check-corpus: %d files, %.1f MiB (%d scripts, %d modules; %d minified), all parsed from UTF-8 and from UTF-16, "
          "both builders agree, and %d functions read again equal"
          % (len(rows), bytes_total / 1048576.0, sum(r.goal == "script" for r in rows), sum(r.goal == "module" for r in rows),
             sum(r.form == "minified" for r in rows), functions))
    print("check-corpus: function kinds read again: " + ", ".join("%s %d" % (n, c) for n, c in zip(KINDS, kinds) if c))
    absent = [name for name, count in zip(KINDS, kinds) if not count]
    if absent:
        print("check-corpus: kinds the corpus has no function of (test262's gate covers them): " + ", ".join(absent))


if __name__ == "__main__":
    main()

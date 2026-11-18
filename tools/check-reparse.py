#!/usr/bin/env python3
"""The function re-parse gate.

A function is compiled on its first call, by parsing that one function again
from its range of the source with the context it had (glecma_parse_function).
The tree that gives must be the tree the first parse gave for it: the same
kinds, flags, operators, ranges (in the whole source), texts and children.
This runs the check over every (file, mode) pair of test262 that the score
covers, through tests/t262/t262_driver.c in its --reparse mode: the driver
parses the pair, finds every function in the tree (function declarations and
expressions, arrow functions, methods, accessors and constructors, class field
initializers and static blocks), derives the context each had from the tree
(tests/reparse_support.h) and reads it again. A wrong context flag, planted,
makes it fail (tools/planted).

It also fails when a kind of function was never read again, so that a driver
that finds no functions of some kind cannot pass for having checked them.

Usage: check-reparse.py --root DIR --sums FILE --classification FILE
           --tags FILE --driver PROGRAM
"""
import argparse
import importlib.util
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("check_test262", os.path.join(HERE, "check-test262.py"))
ct = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ct)

# The kinds, in the order of GLECMA_FNKIND_* from 1.
KINDS = ["function", "generator", "async function", "async generator", "arrow",
         "async arrow", "method", "getter", "setter", "constructor",
         "derived constructor", "field initializer", "static block"]


def main():
    parser = argparse.ArgumentParser()
    for name in ("root", "sums", "classification", "tags", "driver"):
        parser.add_argument("--" + name, required=True)
    parser.add_argument("--unicode-version", default="17.0.0")
    args = parser.parse_args()
    files = ct.integrity(args.root, args.sums)
    classifier = ct.Classifier(args.classification, args.unicode_version)
    tags = ct.load_tags(args.tags)
    _in_files, _skips, pairs, problems, _seen = ct.survey(args.root, files, classifier, tags, set())
    if problems or not pairs:
        ct.fail("no pairs to run, or the classification has problems")
    with tempfile.NamedTemporaryFile("w", suffix=".pairs", delete=False, encoding="utf-8") as handle:
        for name, mode, _expect in pairs:
            handle.write("%s\t%s\n" % (mode, os.path.join(args.root, name)))
        listing = handle.name
    try:
        with open(listing, "rb") as stdin:
            done = subprocess.run([args.driver, "--reparse"], stdin=stdin,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    finally:
        os.unlink(listing)
    if done.returncode != 0:
        ct.fail("the driver exited %d: %s" % (done.returncode, done.stderr.decode("utf-8", "replace")[:400]))
    answers = done.stdout.decode("utf-8").splitlines()
    if len(answers) != len(pairs):
        ct.fail("the driver answered %d lines for %d pairs" % (len(answers), len(pairs)))
    totals = [0] * len(KINDS)
    parsed = functions = skipped = 0
    failures = []
    for (name, mode, _expect), answer in zip(pairs, answers):
        if answer.startswith("REPARSED "):
            numbers = [int(n) for n in answer.split()[1:]]
            if len(numbers) != 1 + len(KINDS):
                ct.fail("a driver line has the wrong shape: %s" % answer)
            parsed += 1
            functions += numbers[0]
            for i, n in enumerate(numbers[1:]):
                totals[i] += n
        elif answer.startswith("SKIP "):
            skipped += 1
        else:
            failures.append((name, mode, answer))
    print("check-reparse: %d pairs parsed (%d refused, which have no tree), %d functions read again, %d that do not equal the first parse's"
          % (parsed, skipped, functions, len(failures)))
    print("  by kind: " + ", ".join("%s %d" % (k, n) for k, n in zip(KINDS, totals)))
    if failures:
        for name, mode, answer in failures[:8]:
            print("  %s [%s]: %s" % (name, mode, answer), file=sys.stderr)
        raise SystemExit(1)
    never = [k for k, n in zip(KINDS, totals) if n == 0]
    if never:
        ct.fail("no function of these kinds was read again, so the check did not cover them: %s" % ", ".join(never))


if __name__ == "__main__":
    main()

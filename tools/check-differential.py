#!/usr/bin/env python3
"""The two-builder differential gate.

The parser makes its tree through a builder interface, and no early-error
check, cover-grammar reinterpretation or node count may read a node
(src/parse/builder.h). Two builders implement the interface: the tree builder,
which builds every node, and the checking builder, which builds no function
body. So the two must give the same answer for every input: the same accept or
refuse, and for a refusal the same rule at the same place. This runs both over
every (file, mode) pair of test262 that the score covers, through
tests/t262/t262_driver.c in its --differential mode, and fails on a pair
where they differ. A check that read a body node, planted, makes it fail
(tools/planted).

Usage: check-differential.py --root DIR --sums FILE --classification FILE
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
            done = subprocess.run([args.driver, "--differential"], stdin=stdin,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    finally:
        os.unlink(listing)
    if done.returncode != 0:
        ct.fail("the driver exited %d: %s" % (done.returncode, done.stderr.decode("utf-8", "replace")[:400]))
    answers = done.stdout.decode("utf-8").splitlines()
    if len(answers) != len(pairs):
        ct.fail("the driver answered %d lines for %d pairs" % (len(answers), len(pairs)))
    refused = sum(1 for a in answers if a.startswith("SAME ERR_SYNTAX"))
    different = [(p, a) for p, a in zip(pairs, answers) if not a.startswith("SAME")]
    print("check-differential: %d pairs, %d refused with a syntax error, %d where the builders differ"
          % (len(pairs), refused, len(different)))
    if different:
        for (name, mode, _e), answer in different[:8]:
            print("  %s [%s]: %s" % (name, mode, answer), file=sys.stderr)
        raise SystemExit(1)
    if refused == 0:
        ct.fail("no pair was refused, so this gate compared nothing that could differ")


if __name__ == "__main__":
    main()

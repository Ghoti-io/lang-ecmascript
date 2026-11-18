#!/usr/bin/env python3
"""Seeds for a fuzz campaign from the real-world corpus (tools/corpus/CORPUS.tsv).

The corpus is fetched, never committed, so what a campaign starts from beyond the
committed seeds is made here, in a scratch directory the campaign owns:
slices of each file, cut at line boundaries, each with the header bytes the
named harness reads first (tests/fuzz/fuzz_*.c). A slice is mostly not a valid
program (it is cut in the middle of one), which is what makes it a seed: the
fuzzer starts from real token sequences, real identifiers and real escapes, and
from every error that cutting a real program in a place it was not meant to be
cut gives.

Usage: corpus-seeds.py HARNESS OUT_DIR [--root DIR] [--count N] [--max-bytes N]
       HARNESS is one of parser, goals, tokenizer, source, alloc.
"""
import argparse
import importlib.util
import os
import random

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("corpus", os.path.join(HERE, "..", "..", "tools", "corpus", "corpus.py"))
corpus = importlib.util.module_from_spec(spec)
spec.loader.exec_module(corpus)


def headers(harness, module, rng):
    """The bytes a harness reads before the text, for a Script (or a Module)."""
    if harness == "parser":
        return bytes([rng.choice([0x00, 0x00, 0x02, 0x10])])
    if harness == "goals":
        return bytes([0x01 if module else 0x00, 0x00])
    if harness == "tokenizer":
        return bytes([0x04 if module else 0x00, 0x00])
    if harness == "source":
        return bytes([0x00])
    if harness == "alloc":
        return bytes([0x01 if module else 0x00, rng.randrange(256), rng.randrange(256)])
    raise SystemExit("unknown harness %s" % harness)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("harness")
    parser.add_argument("out")
    parser.add_argument("--root", default=corpus.DEFAULT_ROOT)
    parser.add_argument("--count", type=int, default=800)
    parser.add_argument("--max-bytes", type=int, default=6000)
    args = parser.parse_args()
    rows = corpus.verify(args.root)
    rng = random.Random(262)
    os.makedirs(args.out, exist_ok=True)
    per_file = max(args.count // len(rows), 1)
    written = 0
    for row in rows:
        data = open(row.path(args.root), "rb").read()
        for index in range(per_file):
            # A window of the file, begun after a line end when one is near (a minified file is one line,
            # and is cut where it falls), and whole code points.
            offset = rng.randrange(len(data))
            near = data.find(b"\n", offset, offset + 200)
            if near >= 0:
                offset = near + 1
            piece = data[offset:offset + args.max_bytes].decode("utf-8", "ignore").encode("utf-8")
            if not piece:
                continue
            name = "%s-%s-%d.bin" % (row.package.replace("/", "_"), os.path.basename(row.member), index)
            with open(os.path.join(args.out, name), "wb") as handle:
                handle.write(headers(args.harness, row.goal == "module", rng) + piece)
            written += 1
    print("corpus-seeds: %d seeds for %s in %s" % (written, args.harness, args.out))


if __name__ == "__main__":
    main()

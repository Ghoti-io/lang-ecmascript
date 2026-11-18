#!/usr/bin/env python3
"""The Node accept/refuse differential.

The parser's decision for each program (accepted, or refused with a syntax
error) is compared with a pinned Node's (V8's). Node is a test-only reference:
its source is never read, and what it answers is a second opinion, not the
specification, so every disagreement is classified, in
tests/data-node-differences.tsv, as one of

  edition    the program is a syntax this library's pinned edition decides
             differently from the V8 that is pinned (a feature of the 17th
             edition V8 has not shipped, or one it ships that the edition
             does not have), the row citing the clause;
  v8-defect  V8 contradicts the clause the row cites (test262 itself says so
             for a test262 program).

A disagreement the table does not classify fails the gate, and so does a row
that classifies none. A defect of this library is never a row: it is fixed, with
a test. The inputs are the in-scope pairs of test262, the real-world corpus, the
fuzz seed corpus (decoded as the harnesses read it) and generated programs
(tools/gen-programs.py), each read as a Script (sloppy and strict) and, where it
can be one, a Module.

The oracle is the container image tools/node-oracle/PIN names, by digest, run
with the flags it names; the version it prints (node, V8, Unicode data, flags)
must be the one the pin says, and is printed beside the result.

Usage: check-node-differential.py --driver PROGRAM [--engine podman|docker]
           [--root DIR --sums FILE --classification FILE --tags FILE]
           [--table FILE] [--generated N] [--no-test262]
       check-node-differential.py --selftest
"""
import argparse
import collections
import contextlib
import hashlib
import importlib.util
import io
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
LIB = os.path.dirname(HERE)

CLASSES = ("edition", "v8-defect")
DIRECTIONS = ("node-refuses", "node-accepts")
GENERATED_SEED = 262


def fail(message):
    print("check-node-differential: %s" % message, file=sys.stderr)
    raise SystemExit(1)


def load_module(name, filename):
    spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, filename))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class Input:
    def __init__(self, ident, group, mode, path):
        self.ident = ident
        self.group = group
        self.mode = mode
        self.path = path


# ---- the classification table ------------------------------------------------------------

class Row:
    def __init__(self, ident, mode, direction, cls, reason, number):
        self.ident, self.mode, self.direction, self.cls, self.reason = ident, mode, direction, cls, reason
        self.number = number
        self.used = 0

    def matches(self, item, direction):
        if self.direction != direction:
            return False
        if self.mode != "*" and self.mode != item.mode:
            return False
        if self.ident.endswith("/"):
            return item.ident.startswith(self.ident)
        return item.ident == self.ident


def load_table(path):
    rows = []
    with open(path, encoding="utf-8") as handle:
        for number, line in enumerate(handle, 1):
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            fields = line.split("\t")
            if len(fields) != 5:
                fail("%s line %d has %d fields, not 5" % (path, number, len(fields)))
            ident, mode, direction, cls, reason = fields
            if cls not in CLASSES:
                fail("%s line %d: the class %r is not one of %s (a defect of ours is fixed, never a row)"
                     % (path, number, cls, ", ".join(CLASSES)))
            if direction not in DIRECTIONS:
                fail("%s line %d: the direction %r is not one of %s" % (path, number, direction, ", ".join(DIRECTIONS)))
            if mode not in ("*", "sloppy", "strict", "module", "raw"):
                fail("%s line %d: the mode %r is not known" % (path, number, mode))
            if not reason.strip():
                fail("%s line %d: a row without a reason (the clause or the V8 behavior)" % (path, number))
            rows.append(Row(ident, mode, direction, cls, reason, number))
    return rows


# ---- comparing -----------------------------------------------------------------------------

def ours_verdict(answer):
    if answer == "OK":
        return "accept"
    if answer == "ERR_SYNTAX":
        return "refuse"
    return "other"


def node_verdict(answer):
    if answer == "ACCEPT":
        return "accept"
    if answer.startswith("REFUSE"):
        return "refuse"
    return "other"


def compare(inputs, ours, node, rows, show=12):
    """Returns (report lines, problems). `ours` and `node` are lists of answers by input."""
    if len(ours) != len(inputs) or len(node) != len(inputs):
        return [], ["the driver answered %d lines and the oracle %d, for %d inputs" % (len(ours), len(node), len(inputs))]
    counts = collections.Counter()
    problems = []
    unclassified = []
    for item, a, b in zip(inputs, ours, node):
        va, vb = ours_verdict(a), node_verdict(b)
        if va == "other" or vb == "other":
            counts[(item.group, "incomparable")] += 1
            continue
        if va == vb:
            counts[(item.group, "agree " + va)] += 1
            continue
        direction = "node-refuses" if vb == "refuse" else "node-accepts"
        row = next((r for r in rows if r.matches(item, direction)), None)
        if row is None:
            unclassified.append((item, direction, b))
            continue
        row.used += 1
        counts[(item.group, row.cls)] += 1
    for row in rows:
        if row.used == 0:
            problems.append("tests/data-node-differences.tsv line %d (%s %s %s) classifies no disagreement"
                            % (row.number, row.ident, row.mode, row.direction))
    for item, direction, answer in unclassified[:show]:
        problems.append("unclassified: %s [%s] %s (node: %s)" % (item.ident, item.mode, direction, answer.replace("\t", " ")[:100]))
    if len(unclassified) > show:
        problems.append("... and %d more unclassified" % (len(unclassified) - show))
    report = []
    for group in sorted({g for g, _ in counts}):
        parts = ["%s %d" % (k, counts[(group, k)]) for k in
                 ("agree accept", "agree refuse", "edition", "v8-defect", "incomparable") if counts[(group, k)]]
        report.append("  %-10s %6d inputs: %s" % (group, sum(v for (g, _), v in counts.items() if g == group), ", ".join(parts)))
    return report, problems


# ---- the inputs ----------------------------------------------------------------------------

def well_formed(data):
    try:
        data.decode("utf-8")
        return True
    except UnicodeDecodeError:
        return False


def fuzz_inputs(corpus_dir, scratch):
    """The seed corpus, decoded as each harness reads its first bytes (tests/fuzz/*.c); an input that is
    not a program of the Script or Module goal (an eval or dynamic function, the internal built-in
    mode, a feature set) or whose text is not well-formed is not one of ours to compare and is counted."""
    items, skipped = [], 0
    out = os.path.join(scratch, "fuzz")
    os.makedirs(out)
    for harness in ("parser", "goals", "tokenizer"):
        directory = os.path.join(corpus_dir, harness)
        if not os.path.isdir(directory):
            continue
        for name in sorted(os.listdir(directory)):
            data = open(os.path.join(directory, name), "rb").read()
            if harness == "parser" and len(data) >= 1:
                first, body = data[0], data[1:]
                wide, strict, builtin, feature = first & 1, first & 2, first & 8, first & 128
                module = 0
            elif harness == "goals" and len(data) >= 2:
                first, body = data[0], data[2:]
                if (first & 7) > 1 or first & 128:
                    skipped += 1
                    continue
                wide, strict, builtin, feature = first & 8, data[1] & 1, 0, 0
                module = first & 7
            elif harness == "tokenizer" and len(data) >= 2:
                first, body = data[0], data[2:]
                wide, strict, builtin, feature = first & 1, first & 2, first & 8, (first >> 6) == 3
                module = first & 4
            else:
                skipped += 1
                continue
            if builtin or feature:
                skipped += 1
                continue
            if wide:
                body = body[:len(body) - len(body) % 2]
                try:
                    body = body.decode("utf-16-le").encode("utf-8")
                except UnicodeDecodeError:
                    skipped += 1
                    continue
            if not well_formed(body):
                skipped += 1
                continue
            path = os.path.join(out, "%s-%s.js" % (harness, name))
            with open(path, "wb") as handle:
                handle.write(body)
            mode = "module" if module else ("strict" if strict else "sloppy")
            items.append(Input("fuzz:%s/%s" % (harness, name), "fuzz", mode, path))
    return items, skipped


def generated_inputs(count, scratch):
    out = os.path.join(scratch, "generated")
    done = subprocess.run([sys.executable, "-I", os.path.join(HERE, "gen-programs.py"), "--out", out,
                           "--count", str(count), "--seed", str(GENERATED_SEED)],
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if done.returncode != 0:
        fail("the generator failed: %s" % done.stderr.decode("utf-8", "replace")[:300])
    items = []
    for index in range(count):
        path = os.path.join(out, "%06d.js" % index)
        for mode in ("sloppy", "strict", "module"):
            items.append(Input("gen:%06d" % index, "generated", mode, path))
    return items


# ---- running the two sides ------------------------------------------------------------------

def read_pin():
    pin = {}
    with open(os.path.join(HERE, "node-oracle", "PIN"), encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if line and not line.startswith("#"):
                key, _, value = line.partition("=")
                pin[key.strip()] = value.strip()
    for key in ("image", "node", "v8", "unicode", "flags"):
        if key not in pin:
            fail("tools/node-oracle/PIN has no %s" % key)
    return pin


def run_ours(driver, manifest):
    done = subprocess.run([driver], input=manifest.encode("utf-8"), stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if done.returncode != 0:
        fail("the driver exited %d: %s" % (done.returncode, done.stderr.decode("utf-8", "replace")[:400]))
    return done.stdout.decode("utf-8").splitlines()


def run_node(engine, pin, manifest, mounts):
    command = [engine, "run", "--rm", "-i", "-v", "%s:/oracle:ro,z" % os.path.join(HERE, "node-oracle")]
    for mount in sorted(mounts):
        command += ["-v", "%s:%s:ro,z" % (mount, mount)]
    command += [pin["image"], "node"] + pin["flags"].split() + ["/oracle/syntax.mjs"]
    done = subprocess.run(command, input=manifest.encode("utf-8"), stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if done.returncode != 0:
        fail("the oracle exited %d: %s\n(the image is fetched by `make fetch-node-oracle`)"
             % (done.returncode, done.stderr.decode("utf-8", "replace")[:600]))
    banner = done.stderr.decode("utf-8", "replace").splitlines()[0] if done.stderr else ""
    expected = "node %s, V8 %s, Unicode %s, flags %s" % (
        pin["node"], pin["v8"], pin["unicode"], '[' + ",".join('"%s"' % f for f in pin["flags"].split()) + ']')
    if banner != expected:
        fail("the oracle says %r, and tools/node-oracle/PIN pins %r" % (banner, expected))
    return banner, done.stdout.decode("utf-8").splitlines()


def selftest():
    """The comparison logic on canned answers: no node, no driver."""
    mk = lambda ident, mode="sloppy", group="gen": Input(ident, group, mode, "/x")
    rows = [Row("test262:a/", "*", "node-refuses", "edition", "clause 1", 1)]
    inputs = [mk("test262:a/x.js", group="test262"), mk("gen:1"), mk("gen:2"), mk("gen:3")]
    # Agreement in both directions, an input neither side can answer, and a classified disagreement.
    rows = [Row("test262:a/", "*", "node-refuses", "edition", "clause 1", 1)]
    inputs = [mk("test262:a/x.js", group="test262"), mk("gen:1"), mk("gen:2"), mk("gen:3")]
    report, problems = compare(inputs, ["ERR_SYNTAX", "OK", "ERR_LIMIT", "ERR_SYNTAX"],
                               ["REFUSE\tm", "ACCEPT", "ACCEPT", "REFUSE\tm"], [])
    assert len(problems) == 0 and any("agree refuse" in line for line in report), (problems, report)
    inputs = [mk("test262:a/x.js", group="test262")]
    report, problems = compare(inputs, ["OK"], ["REFUSE\tm"], rows)
    assert problems == [] and any("edition 1" in line for line in report), (problems, report)
    # A row that classifies nothing fails.
    _, problems = compare([mk("gen:1")], ["OK"], ["ACCEPT"], [Row("test262:a/", "*", "node-refuses", "edition", "clause 1", 1)])
    assert len(problems) == 1 and "classifies no" in problems[0], problems
    # An unclassified disagreement fails, in either direction, and a row for the other direction does not cover it.
    _, problems = compare([mk("gen:9")], ["OK"], ["REFUSE\tm"], [])
    assert len(problems) == 1 and "unclassified" in problems[0], problems
    _, problems = compare([mk("test262:a/y.js")], ["ERR_SYNTAX"], ["ACCEPT"], [Row("test262:a/", "*", "node-refuses", "edition", "c", 1)])
    assert any("unclassified" in p for p in problems) and any("classifies no" in p for p in problems), problems
    # A row for one mode does not cover another.
    _, problems = compare([mk("gen:1", "strict")], ["OK"], ["REFUSE\tm"], [Row("gen:1", "sloppy", "node-refuses", "v8-defect", "c", 1)])
    assert any("unclassified" in p for p in problems), problems
    # A short answer is a problem, not a pass.
    _, problems = compare([mk("gen:1")], [], ["ACCEPT"], [])
    assert problems and "answered" in problems[0], problems
    # A row without a reason, with a class that blames us, or in a direction that is not one, is refused.
    for bad in ("a\t*\tnode-refuses\tours\tr\n", "a\t*\tnode-refuses\tedition\t\n", "a\t*\tup\tedition\tr\n"):
        with tempfile.NamedTemporaryFile("w", suffix=".tsv", delete=False) as handle:
            handle.write(bad)
        try:
            with contextlib.redirect_stderr(io.StringIO()):
                load_table(handle.name)
        except SystemExit:
            pass
        else:
            raise AssertionError("a bad row was accepted: %r" % bad)
        finally:
            os.unlink(handle.name)
    print("check-node-differential: self-test passed")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--driver")
    parser.add_argument("--engine", default=None)
    parser.add_argument("--root")
    parser.add_argument("--sums")
    parser.add_argument("--classification")
    parser.add_argument("--tags")
    parser.add_argument("--unicode-version", default="17.0.0")
    parser.add_argument("--table", default=os.path.join(LIB, "tests", "data-node-differences.tsv"))
    parser.add_argument("--corpus-root", default=os.path.join(LIB, "third_party", "corpus"))
    parser.add_argument("--fuzz-corpus", default=os.path.join(LIB, "tests", "fuzz", "corpus"))
    parser.add_argument("--generated", type=int, default=20000)
    parser.add_argument("--no-test262", action="store_true")
    parser.add_argument("--show", type=int, default=12, help="how many unclassified disagreements to print")
    args = parser.parse_args()
    for name in ("root", "corpus_root", "fuzz_corpus", "table"):
        if getattr(args, name):
            setattr(args, name, os.path.realpath(getattr(args, name)))
    if args.selftest:
        selftest()
        return
    if not args.driver:
        fail("--driver is required")
    engine = args.engine or shutil.which("podman") or shutil.which("docker")
    if not engine:
        fail("no container engine (podman or docker) to run the pinned oracle in")
    pin = read_pin()
    rows = load_table(args.table)
    scratch = tempfile.mkdtemp(prefix="glecma-node-diff.")
    try:
        inputs, mounts, notes = [], set(), []
        if not args.no_test262:
            ct = load_module("check_test262", "check-test262.py")
            files = ct.integrity(args.root, args.sums)
            classifier = ct.Classifier(args.classification, args.unicode_version)
            tags = ct.load_tags(args.tags)
            _in, _skips, pairs, problems, _seen = ct.survey(args.root, files, classifier, tags, set())
            if problems or not pairs:
                fail("no test262 pairs to run, or the classification has problems")
            for name, mode, _expect in pairs:
                inputs.append(Input("test262:" + name, "test262", mode, os.path.join(args.root, name)))
            mounts.add(os.path.realpath(args.root))
        corpus = load_module("corpus", os.path.join("corpus", "corpus.py"))
        for row in corpus.verify(args.corpus_root):
            inputs.append(Input("corpus:%s/%s" % (row.package, row.member), "corpus",
                                "module" if row.goal == "module" else "sloppy", row.path(args.corpus_root)))
            if row.goal == "script":
                inputs.append(Input("corpus:%s/%s" % (row.package, row.member), "corpus", "strict", row.path(args.corpus_root)))
        mounts.add(os.path.realpath(args.corpus_root))
        fuzz, skipped = fuzz_inputs(args.fuzz_corpus, scratch)
        inputs += fuzz
        notes.append("%d fuzz seeds are not programs of the Script or Module goal or are not well-formed text, and are not compared" % skipped)
        if args.generated > 0:
            inputs += generated_inputs(args.generated, scratch)
        mounts.add(os.path.realpath(scratch))
        manifest = "".join("%s\t%s\n" % (i.mode, i.path) for i in inputs)
        ours = run_ours(args.driver, manifest)
        banner, node = run_node(engine, pin, manifest, mounts)
        report, problems = compare(inputs, ours, node, rows, args.show)
        print("check-node-differential: oracle %s" % banner)
        print("check-node-differential: image %s" % pin["image"])
        print("check-node-differential: %d inputs (the in-scope test262 pairs, the corpus, the fuzz seeds, %d generated programs of seed %d)"
              % (len(inputs), args.generated, GENERATED_SEED))
        for line in report:
            print(line)
        for note in notes:
            print("  note: " + note)
        if problems:
            for problem in problems:
                print("check-node-differential: " + problem, file=sys.stderr)
            raise SystemExit(1)
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


if __name__ == "__main__":
    main()

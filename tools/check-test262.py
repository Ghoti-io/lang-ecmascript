#!/usr/bin/env python3
"""The test262 conformance gate: pin, classification and the parse score.

Subcommands:

  check      The gate. Fails when the fetched corpus is missing, altered or
             has extra files; when a file or a `features:` tag has no row in
             the committed tables; when a pair fails and is not in the pending
             ledger, or is in it and passes. Prints the score.
  integrity  Only the first of those (the fetch script calls it).
  sums       Write the committed list of file checksums from a fetched tree.
             Used once, deliberately, when the pin moves.

What the gate asks, per (file, mode) pair, is the one question M1 can answer
before there is a host to run anything: does the parser accept exactly what
the edition accepts? A test that expects a parse-phase SyntaxError passes
only when the parse is refused with GLECMA_ERR_SYNTAX; one that does not
passes only when the parse succeeds. Every other result, GLECMA_ERR_UNSUPPORTED
for work not yet built included, fails the pair, and a pair that fails must be
in the pending ledger, so that unbuilt work can never score.

The driver is a separate program (tests/t262/t262_driver.c) fed one
"mode<TAB>path" line per pair on standard input; it answers one result name
per line. It is the only place the library is called.

See documentation/design.md ("test262") and tools/test262/fetch.sh.

Usage: check-test262.py check --root DIR --sums FILE --classification FILE
           --tags FILE --pending FILE --concordance FILE --driver PROGRAM
           [--features NAME,NAME] [--unicode-version V.V.V]
"""
import argparse
import itertools
import fnmatch
import hashlib
import os
import re
import subprocess
import sys
import tempfile
from collections import Counter, defaultdict

MODES = ("sloppy", "strict", "module", "raw")
DECISIONS = ("in", "include", "fixture", "fixture-refuse", "out")
STATES = ("in", "out", "in-when-on")


def fail(message):
    print("check-test262: %s" % message, file=sys.stderr)
    raise SystemExit(1)


def read_table(path, columns, what):
    """Rows of a TSV table: '#' lines and blank lines are comments."""
    try:
        text = open(path, encoding="utf-8").read()
    except OSError as error:
        fail("cannot read %s: %s" % (path, error))
    rows = []
    for number, line in enumerate(text.splitlines(), 1):
        if not line.strip() or line.startswith("#"):
            continue
        cells = line.split("\t")
        if len(cells) != columns:
            fail("%s:%d: %s rows have %d tab-separated columns, this has %d"
                 % (path, number, what, columns, len(cells)))
        if not all(c.strip() for c in cells):
            fail("%s:%d: an empty column in a %s row" % (path, number, what))
        rows.append((number, cells))
    return rows


# ---------------------------------------------------------------- integrity

def walk(root):
    """Every file under root, as a sorted list of '/'-separated paths."""
    found = []
    for directory, subdirs, files in os.walk(root):
        subdirs.sort()
        for name in files:
            full = os.path.join(directory, name)
            found.append(os.path.relpath(full, root).replace(os.sep, "/"))
    return sorted(found)


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def read_sums(path):
    sums = {}
    try:
        text = open(path, encoding="utf-8").read()
    except OSError as error:
        fail("cannot read %s: %s" % (path, error))
    for number, line in enumerate(text.splitlines(), 1):
        if not line or line.startswith("#"):
            continue
        digest, _, name = line.partition("  ")
        if len(digest) != 64 or not name:
            fail("%s:%d: not '<sha256>  <path>'" % (path, number))
        if name in sums:
            fail("%s:%d: %s is listed twice" % (path, number, name))
        sums[name] = digest
    if not sums:
        fail("%s lists no files, so the integrity check would see nothing" % path)
    return sums


def integrity(root, sums_path):
    if not os.path.isdir(root):
        fail("the test262 corpus is not at %s; run tools/test262/fetch.sh "
             "(it needs the network; `make test` never fetches)" % root)
    sums = read_sums(sums_path)
    files = walk(root)
    present = set(files)
    missing = sorted(set(sums) - present)
    extra = sorted(present - set(sums))
    altered = [name for name in files
               if name in sums and sha256_file(os.path.join(root, name)) != sums[name]]
    problems = []
    for label, names in (("missing", missing), ("altered", altered), ("extra", extra)):
        if names:
            problems.append("%d %s file(s), for example %s"
                            % (len(names), label, ", ".join(names[:3])))
    if problems:
        fail("the corpus at %s differs from tools/test262/SHA256SUMS: %s"
             % (root, "; ".join(problems)))
    return files


def write_sums(root, sums_path):
    files = walk(root)
    with open(sums_path, "w", encoding="utf-8", newline="\n") as out:
        for name in files:
            out.write("%s  %s\n" % (sha256_file(os.path.join(root, name)), name))
    print("check-test262: wrote %d checksums to %s" % (len(files), sums_path))


# ---------------------------------------------------------------- metadata

HEADER = re.compile(r"/\*---(.*?)---\*/", re.S)
FIXTURE_REF = re.compile(r"[A-Za-z0-9_./-]*_FIXTURE\.js")


def unquote(item):
    item = item.strip()
    if len(item) >= 2 and item[0] == item[-1] and item[0] in "\"'":
        item = item[1:-1]
    return item


def parse_list(value, lines, index):
    """A flow list `[a, b]`, or a block list on the following lines."""
    value = value.strip()
    if value.startswith("["):
        if not value.endswith("]"):
            raise ValueError("an unterminated flow list")
        inner = value[1:-1]
        return [unquote(i) for i in inner.split(",") if i.strip()], index
    if value:
        raise ValueError("a list that is neither flow nor block: %r" % value)
    items = []
    while index < len(lines):
        match = re.match(r"^\s*-\s+(.*)$", lines[index])
        if not match:
            break
        items.append(unquote(match.group(1)))
        index += 1
    return items, index


def parse_metadata(text):
    """The keys the gate uses from a test's YAML header, or None for no header.

    Only `features`, `flags`, `includes` and `negative` matter here. A
    description or an `info` block scalar is skipped by indentation. A header
    that this cannot read is an error, never a default: a silent default is a
    test scored in the wrong mode.
    """
    match = HEADER.search(text)
    if not match:
        return None
    lines = re.split(r"\r\n|\r|\n", match.group(1))
    meta = {"features": [], "flags": [], "includes": [], "negative": None}
    index = 0
    while index < len(lines):
        line = lines[index]
        top = re.match(r"^([A-Za-z0-9_]+):\s*(.*)$", line)
        index += 1
        if not top:
            continue
        key, value = top.group(1), top.group(2)
        if key in ("features", "flags", "includes"):
            meta[key], index = parse_list(value, lines, index)
        elif key == "negative":
            fields = {}
            if value.startswith("{"):
                for part in value.strip("{} ").split(","):
                    name, _, v = part.partition(":")
                    fields[name.strip()] = unquote(v)
            elif value == "":
                while index < len(lines):
                    sub = re.match(r"^\s+([A-Za-z]+):\s*(.*)$", lines[index])
                    if not sub:
                        break
                    fields[sub.group(1)] = unquote(sub.group(2))
                    index += 1
            else:
                raise ValueError("a negative that is neither a mapping nor flow")
            if set(fields) != {"phase", "type"}:
                raise ValueError("negative needs exactly phase and type: %r" % fields)
            meta["negative"] = fields
    return meta


def modes_for(flags):
    for flag in flags:
        if flag not in KNOWN_FLAGS:
            raise ValueError("unknown flag %r" % flag)
    chosen = [f for f in flags if f in ("onlyStrict", "noStrict", "raw", "module")]
    # `raw` says only that the harness and the strict directive are not
    # added, which a module never gets; it does not choose a second mode.
    if "module" in chosen and set(chosen) <= {"module", "raw"}:
        return ["module"]
    if len(chosen) > 1:
        raise ValueError("flags that exclude each other: %r" % chosen)
    if not chosen:
        return ["sloppy", "strict"]
    return {"onlyStrict": ["strict"], "noStrict": ["sloppy"],
            "raw": ["raw"], "module": ["module"]}[chosen[0]]


# test262's INTERPRETING.md: the flags a test may carry. A flag the gate does
# not know changes how a test runs and is refused, not ignored.
KNOWN_FLAGS = {"onlyStrict", "noStrict", "raw", "module", "async", "generated",
               "CanBlockIsFalse", "CanBlockIsTrue", "non-deterministic"}


# ---------------------------------------------------------------- tables

class Classifier:
    def __init__(self, path, unicode_version):
        self.rows = []
        self.unicode_version = tuple(int(p) for p in unicode_version.split("."))
        self.used = Counter()
        for number, (selector, decision, reason, where) in read_table(
                path, 4, "classification"):
            if decision not in DECISIONS:
                fail("%s:%d: decision %r is not one of %s"
                     % (path, number, decision, ", ".join(DECISIONS)))
            self.rows.append((number, selector, decision, reason, where))
        if not self.rows:
            fail("%s holds no rows" % path)

    def newer_unicode(self, name, root):
        """Whether a file was generated for a Unicode version above the pin.

        Two families: `test/language/identifiers/*-unicode-X.Y.Z*.js` (the
        version is in the name) and the generated property escapes, whose
        header names `Unicode vX.Y.Z`. Only a version above the pinned one is
        out; an older one is the same assertion the pinned data still makes.
        """
        m = re.search(r"-unicode-(\d+)\.(\d+)\.(\d+)", name)
        if name.startswith("test/language/identifiers/") and m:
            return tuple(int(g) for g in m.groups()) > self.unicode_version
        if "/property-escapes/generated/" in name and name.endswith(".js"):
            with open(os.path.join(root, name), "rb") as handle:
                head = handle.read(4096).decode("utf-8", "replace")
            v = re.search(r"Unicode v(\d+)\.(\d+)\.(\d+)", head)
            if v:
                return tuple(int(g) for g in v.groups()) > self.unicode_version
        return False

    def classify(self, name, root):
        for number, selector, decision, reason, where in self.rows:
            if selector == "toplevel:":
                hit = "/" not in name
            elif selector.startswith("suffix:"):
                hit = name.endswith(selector[7:])
            elif selector == "nonjs:":
                hit = not name.endswith(".js")
            elif selector == "unicode-newer:":
                hit = self.newer_unicode(name, root)
            elif selector.endswith("/"):
                hit = name.startswith(selector)
            else:
                hit = name == selector
            if hit:
                self.used[number] += 1
                return decision, reason, where
        return None


def load_tags(path):
    tags = {}
    for number, (tag, state, cite, note) in read_table(path, 4, "tag"):
        if state not in STATES:
            fail("%s:%d: state %r is not one of %s"
                 % (path, number, state, ", ".join(STATES)))
        if tag in tags:
            fail("%s:%d: tag %s has two rows" % (path, number, tag))
        tags[tag] = (state, cite, note)
    return tags


class Ledger(list):
    """The pending ledger's rows, with an index so that a lookup does not read
    every row: the ledger names thousands of files, and a pair is looked up
    for each of tens of thousands of pairs."""

    def __init__(self):
        super().__init__()
        self._exact = None
        self._prefixes = None

    def build(self):
        self._exact = {}
        self._prefixes = []
        for row in self:
            number, selector, mode, _reason, _cleared = row
            if selector.endswith("/"):
                self._prefixes.append(row)
            else:
                self._exact.setdefault(selector, []).append(row)


def load_pending(path):
    rows = Ledger()
    for number, (selector, mode, reason, cleared) in read_table(path, 4, "pending ledger"):
        if mode != "*" and mode not in MODES:
            fail("%s:%d: mode %r is not * or one of %s" % (path, number, mode, ", ".join(MODES)))
        rows.append((number, selector, mode, reason, cleared))
    return rows


def pending_match(rows, name, mode, used):
    if isinstance(rows, Ledger):
        # The first row, in table order, that names the pair.
        if rows._exact is None:
            rows.build()
        best = None
        candidates = rows._exact.get(name, ())
        for row in itertools.chain(candidates, rows._prefixes):
            number, selector, row_mode = row[0], row[1], row[2]
            if row_mode not in ("*", mode):
                continue
            if selector.endswith("/") and not name.startswith(selector):
                continue
            if best is None or number < best:
                best = number
        if best is not None:
            used[best] += 1
        return best
    for number, selector, row_mode, _reason, _cleared in rows:
        if row_mode not in ("*", mode):
            continue
        if selector.endswith("/"):
            hit = name.startswith(selector)
        else:
            hit = name == selector
        if hit:
            used[number] += 1
            return number
    return None


# ---------------------------------------------------------------- the gate

def survey(root, files, classifier, tags, features_on):
    """Classify every file; read metadata; resolve fixtures; build pairs.

    Returns (counts, pairs, problems) where a pair is (name, mode, expect).
    """
    problems = []
    skips = Counter()          # reason -> files
    pairs = []
    meta_of = {}
    tag_seen = set()
    fixtures = {}              # name -> (decision, importers)
    importers = defaultdict(set)
    kind = {}                  # name -> decision for in-scope-by-directory files
    in_files = 0

    for name in files:
        verdict = classifier.classify(name, root)
        if verdict is None:
            problems.append("%s is named by no row of the classification table" % name)
            continue
        decision, reason, where = verdict
        if decision == "out":
            skips["out: " + reason] += 1
            # A tag is "present" wherever a file carries it, in scope or not.
            if name.endswith(".js") and name.startswith("test/"):
                try:
                    with open(os.path.join(root, name), "rb") as handle:
                        meta = parse_metadata(handle.read().decode("utf-8", "replace"))
                except ValueError as error:
                    problems.append("%s: %s" % (name, error))
                    continue
                if meta:
                    tag_seen.update(meta["features"])
            continue
        kind[name] = decision
        if decision == "include":
            continue
        with open(os.path.join(root, name), "rb") as handle:
            text = handle.read().decode("utf-8", "replace")
        if decision in ("fixture", "fixture-refuse"):
            fixtures[name] = decision
            for ref in set(FIXTURE_REF.findall(text)):
                target = os.path.normpath(os.path.join(os.path.dirname(name), ref)).replace(os.sep, "/")
                importers[target].add(name)
            continue
        try:
            meta = parse_metadata(text)
        except ValueError as error:
            problems.append("%s: %s" % (name, error))
            continue
        if meta is None:
            problems.append("%s has no test262 metadata header and is not a fixture" % name)
            continue
        meta_of[name] = meta
        tag_seen.update(meta["features"])
        for ref in set(FIXTURE_REF.findall(text)):
            target = os.path.normpath(os.path.join(os.path.dirname(name), ref)).replace(os.sep, "/")
            importers[target].add(name)

    # Tags: each present tag needs a row; its state decides the file.
    for tag in sorted(tag_seen):
        if tag not in tags:
            problems.append("the tag %r has no row in the tag table" % tag)

    def file_state(name):
        """None when in scope, else the skip reason."""
        for tag in meta_of[name]["features"]:
            if tag not in tags:
                return "tag %s has no row" % tag
            state, cite, _ = tags[tag]
            if state == "out":
                return "tag: " + tag
            if state == "in-when-on" and cite not in features_on:
                return "tag: %s (feature %s off)" % (tag, cite)
        return None

    in_scope = {}
    for name in meta_of:
        in_scope[name] = file_state(name) is None
        if not in_scope[name]:
            skips["out: " + file_state(name)] += 1

    # A fixture is in scope when some importer is: a test in scope, or a
    # fixture in scope. Iterate to a fixed point so chains and cycles settle.
    fixture_in = {f: False for f in fixtures}
    changed = True
    while changed:
        changed = False
        for f in fixtures:
            if not fixture_in[f] and any(
                    in_scope.get(i) or fixture_in.get(i) for i in importers.get(f, ())):
                fixture_in[f] = True
                changed = True
    for f in sorted(fixtures):
        if not importers.get(f):
            problems.append("%s is imported by no file, so no tag or scope reaches it" % f)

    for name in sorted(kind):
        decision = kind[name]
        if decision == "include":
            in_files += 1
            for mode in ("sloppy", "strict"):
                pairs.append((name, mode, "accept"))
        elif decision in ("fixture", "fixture-refuse"):
            if not fixture_in.get(name):
                skips["out: fixture imported only by out-of-scope tests"] += 1
                continue
            in_files += 1
            pairs.append((name, "module", "refuse" if decision == "fixture-refuse" else "accept"))
        elif in_scope.get(name):
            meta = meta_of[name]
            try:
                modes = modes_for(meta["flags"])
            except ValueError as error:
                problems.append("%s: %s" % (name, error))
                continue
            expect = "accept"
            neg = meta["negative"]
            if neg:
                if neg["phase"] == "parse":
                    if neg["type"] != "SyntaxError":
                        problems.append("%s: a parse-phase negative of type %s" % (name, neg["type"]))
                    expect = "refuse"
                elif neg["phase"] not in ("resolution", "runtime"):
                    problems.append("%s: negative phase %r" % (name, neg["phase"]))
            in_files += 1
            for mode in modes:
                pairs.append((name, mode, expect))
    return in_files, skips, pairs, problems, tag_seen


def run_driver(driver, root, pairs):
    with tempfile.NamedTemporaryFile("w", suffix=".pairs", delete=False,
                                     encoding="utf-8") as handle:
        for name, mode, _expect in pairs:
            handle.write("%s\t%s\n" % (mode, os.path.join(root, name)))
        list_path = handle.name
    try:
        with open(list_path, "rb") as stdin:
            done = subprocess.run([driver], stdin=stdin, stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE, check=False)
    finally:
        os.unlink(list_path)
    if done.returncode != 0:
        fail("the driver %s exited %d: %s" % (driver, done.returncode,
                                              done.stderr.decode("utf-8", "replace")[:400]))
    answers = done.stdout.decode("utf-8").splitlines()
    if len(answers) != len(pairs):
        fail("the driver answered %d lines for %d pairs" % (len(answers), len(pairs)))
    return answers


def check(args):
    files = integrity(args.root, args.sums)
    classifier = Classifier(args.classification, args.unicode_version)
    tags = load_tags(args.tags)
    ledger = load_pending(args.pending)
    features_on = set(f for f in args.features.split(",") if f)

    in_files, skips, pairs, problems, tag_seen = survey(
        args.root, files, classifier, tags, features_on)
    # test262's own registry of tags (features.txt) and the tags the tests
    # carry are the two populations a tag row can come from; the table must
    # name all of the first and all of the second, and nothing else.
    registry = set()
    registry_path = os.path.join(args.root, "features.txt")
    if os.path.exists(registry_path):
        for line in open(registry_path, encoding="utf-8"):
            word = line.split("#")[0].strip()
            if word:
                registry.add(word)
    for tag in sorted(registry - set(tags)):
        problems.append("the tag %r is in features.txt and has no row in the tag table" % tag)
    for tag in sorted(set(tags) - registry - tag_seen):
        problems.append("tag table row %r names a tag no test carries and features.txt does not list" % tag)
    # A table that names nothing is stale: rows are exact.
    for number, selector, decision, reason, where in classifier.rows:
        if classifier.used[number] == 0 and selector != "unicode-newer:":
            problems.append("classification row %d (%s) matches no file at this pin" % (number, selector))
    if problems:
        for p in problems[:30]:
            print("check-test262: %s" % p, file=sys.stderr)
        fail("%d problem(s) in the classification or the tag table" % len(problems))
    if not pairs:
        fail("no (file, mode) pair is in scope, so this gate measures nothing")

    answers = run_driver(args.driver, args.root, pairs)
    used = Counter()
    passed = pending = 0
    unledgered = []
    now_passing = []
    refused_other = Counter()
    by_result = Counter()
    for (name, mode, expect), answer in zip(pairs, answers):
        by_result[answer] += 1
        ok = (answer == "OK") if expect == "accept" else (answer == "ERR_SYNTAX")
        row = pending_match(ledger, name, mode, used)
        if ok:
            passed += 1
            if row is not None:
                now_passing.append((name, mode, row))
        else:
            if expect == "refuse" and answer != "OK":
                refused_other[answer] += 1
            if row is None:
                unledgered.append((name, mode, expect, answer))
            else:
                pending += 1
    stale = [(n, s, m) for n, s, m, _r, _c in ledger if used[n] == 0]

    total = len(pairs)
    print("test262 pin %s" % os.path.basename(args.root.rstrip("/")))
    print("  files in the corpus: %d; in scope: %d; pairs (file, mode): %d" % (len(files), in_files, total))
    for reason, count in sorted(skips.items(), key=lambda kv: (-kv[1], kv[0])):
        print("  skipped %7d  %s" % (count, reason))
    print("  driver results: %s" % ", ".join("%s %d" % kv for kv in sorted(by_result.items())))
    print("  score: %d passed of %d pairs (%.2f%%); pending %d; failing outside the ledger %d"
          % (passed, total, 100.0 * passed / total, pending, len(unledgered)))
    print("  features on: %s" % (", ".join(sorted(features_on)) or "none"))

    bad = False
    if unledgered:
        bad = True
        print("check-test262: %d failing pair(s) are not in the pending ledger, for example:" % len(unledgered), file=sys.stderr)
        for name, mode, expect, answer in unledgered[:8]:
            print("  %s [%s] expected %s, got %s" % (name, mode, expect, answer), file=sys.stderr)
    if now_passing:
        bad = True
        print("check-test262: %d pair(s) in the pending ledger now pass; remove them from it, for example:" % len(now_passing), file=sys.stderr)
        for name, mode, row in now_passing[:8]:
            print("  %s [%s] (ledger row %d)" % (name, mode, row), file=sys.stderr)
    if stale:
        bad = True
        print("check-test262: %d ledger row(s) match no pair:" % len(stale), file=sys.stderr)
        for n, s, m in stale[:8]:
            print("  row %d: %s [%s]" % (n, s, m), file=sys.stderr)
    if bad:
        raise SystemExit(1)
    print("check-test262: ok")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("command", choices=("check", "integrity", "sums"))
    parser.add_argument("--root", required=True)
    parser.add_argument("--sums", required=True)
    parser.add_argument("--classification")
    parser.add_argument("--tags")
    parser.add_argument("--pending")
    parser.add_argument("--concordance")
    parser.add_argument("--driver")
    parser.add_argument("--features", default="")
    parser.add_argument("--unicode-version", default="17.0.0")
    args = parser.parse_args()
    if args.command == "sums":
        write_sums(args.root, args.sums)
    elif args.command == "integrity":
        integrity(args.root, args.sums)
    else:
        for needed in ("classification", "tags", "pending", "driver"):
            if getattr(args, needed) is None:
                fail("check needs --%s" % needed)
        check(args)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""The real-world corpus: fetched by hash, never committed.

tools/corpus/CORPUS.tsv names each file by the npm registry's tarball of one
exact package version (and that tarball's SHA-256), the member of it, the
member's SHA-256, the goal it is read under (script or module), whether it is
minified, and the license it is under with the license file's SHA-256. Nothing
here is copied into a repository of ours: `fetch` puts the files under
third_party/corpus/ (which is ignored), and every gate that reads them first
checks every hash, so a changed, a missing or an extra file is noticed and is
never absorbed.

    corpus.py fetch   [--root DIR]   download each tarball, verify it, write
                                     the named members and licenses
    corpus.py verify  [--root DIR]   check the written files against the table
    corpus.py list    [--root DIR] [--goal script|module]
                                     print `mode<TAB>path` lines for the
                                     driver (sloppy for a script, module for a
                                     module), after verifying
    corpus.py licenses               print the table of licenses

Only files under a plainly redistributable license (MIT, ISC, Apache-2.0,
BSD-2-Clause, BSD-3-Clause) may have a row; `check_table` refuses any other,
which is the rule owner question Q-59 asks the owner to confirm.

Copyright 2026 by Corey Pennycuff
"""
import argparse
import hashlib
import io
import os
import sys
import tarfile
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
TABLE = os.path.join(HERE, "CORPUS.tsv")
DEFAULT_ROOT = os.path.join(os.path.dirname(os.path.dirname(HERE)), "third_party", "corpus")
ADMITTED_LICENSES = ("MIT", "ISC", "Apache-2.0", "BSD-2-Clause", "BSD-3-Clause")
GOALS = ("script", "module")
FORMS = ("source", "minified")
# Each file is read in full; a tarball larger than this is not one of ours.
MAX_TARBALL = 64 * 1024 * 1024


def fail(message):
    print("corpus: %s" % message, file=sys.stderr)
    raise SystemExit(1)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


class Row:
    def __init__(self, fields):
        (self.package, self.version, self.tarball_sha, self.member, self.member_sha,
         self.goal, self.form, self.license, self.license_member, self.license_sha) = fields

    @property
    def directory(self):
        return "%s-%s" % (self.package.replace("/", "_"), self.version)

    @property
    def url(self):
        base = self.package.split("/")[-1]
        return "https://registry.npmjs.org/%s/-/%s-%s.tgz" % (self.package, base, self.version)

    def path(self, root):
        return os.path.join(root, self.directory, self.member)

    def license_path(self, root):
        return os.path.join(root, self.directory, self.license_member)


def load_table():
    rows = []
    with open(TABLE, encoding="utf-8") as handle:
        for number, line in enumerate(handle, 1):
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            fields = line.split("\t")
            if len(fields) != 10:
                fail("CORPUS.tsv line %d has %d fields, not 10" % (number, len(fields)))
            rows.append(Row(fields))
    return rows


def check_table(rows):
    if not rows:
        fail("CORPUS.tsv has no rows")
    seen = set()
    shas = {}
    for row in rows:
        key = (row.package, row.version, row.member)
        if key in seen:
            fail("CORPUS.tsv names %s twice" % "/".join(key))
        seen.add(key)
        if row.license not in ADMITTED_LICENSES:
            fail("%s %s is under %s, which is not an admitted license (%s)" %
                 (row.package, row.version, row.license, ", ".join(ADMITTED_LICENSES)))
        if row.goal not in GOALS or row.form not in FORMS:
            fail("%s: goal %r or form %r is not known" % (row.member, row.goal, row.form))
        for name, value in (("tarball", row.tarball_sha), ("member", row.member_sha),
                            ("license", row.license_sha)):
            if len(value) != 64 or any(c not in "0123456789abcdef" for c in value):
                fail("%s: the %s hash is not a SHA-256" % (row.member, name))
        # Two rows of one package version name one tarball.
        if shas.setdefault((row.package, row.version), row.tarball_sha) != row.tarball_sha:
            fail("%s %s names two tarball hashes" % (row.package, row.version))
        # Registry names and member paths are never absolute or leave their directory.
        for part in (row.member, row.license_member, row.package):
            if part.startswith("/") or ".." in part.split("/"):
                fail("%r is not a plain relative name" % part)


def fetch(root):
    rows = load_table()
    check_table(rows)
    done = {}
    for row in rows:
        done.setdefault((row.package, row.version), []).append(row)
    for (package, version), group in done.items():
        if all(os.path.isfile(r.path(root)) and os.path.isfile(r.license_path(root)) and
               sha256(open(r.path(root), "rb").read()) == r.member_sha for r in group):
            print("have    %s %s" % (package, version))
            continue
        first = group[0]
        print("fetch   %s %s" % (package, version))
        request = urllib.request.Request(first.url, headers={"User-Agent": "lang-ecmascript-corpus-fetch"})
        with urllib.request.urlopen(request, timeout=120) as response:
            data = response.read(MAX_TARBALL + 1)
        if len(data) > MAX_TARBALL:
            fail("%s is larger than %d bytes" % (first.url, MAX_TARBALL))
        if sha256(data) != first.tarball_sha:
            fail("%s: the tarball's hash is %s, not the %s the table pins" %
                 (first.url, sha256(data), first.tarball_sha))
        with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as archive:
            for row in group:
                for member, want, where in ((row.member, row.member_sha, row.path(root)),
                                            (row.license_member, row.license_sha, row.license_path(root))):
                    # A member is read by its exact name and written to a path built here: the
                    # tarball's own paths are never used to place a file.
                    handle = archive.extractfile("package/" + member)
                    if handle is None:
                        fail("%s %s has no member %s" % (package, version, member))
                    body = handle.read()
                    if sha256(body) != want:
                        fail("%s %s: %s hashes to %s, not the %s the table pins" %
                             (package, version, member, sha256(body), want))
                    os.makedirs(os.path.dirname(where), exist_ok=True)
                    with open(where + ".partial", "wb") as out:
                        out.write(body)
                    os.replace(where + ".partial", where)
    verify(root)


def verify(root):
    rows = load_table()
    check_table(rows)
    problems = []
    expected = set()
    for row in rows:
        for name, where, want in (("file", row.path(root), row.member_sha),
                                  ("license", row.license_path(root), row.license_sha)):
            expected.add(os.path.normpath(where))
            if not os.path.isfile(where):
                problems.append("missing %s %s" % (name, where))
            elif sha256(open(where, "rb").read()) != want:
                problems.append("%s %s does not match its hash" % (name, where))
    if os.path.isdir(root):
        for here, _dirs, names in os.walk(root):
            for name in names:
                if os.path.normpath(os.path.join(here, name)) not in expected:
                    problems.append("extra file %s" % os.path.join(here, name))
    if problems:
        for problem in problems[:10]:
            print("corpus: %s" % problem, file=sys.stderr)
        fail("the corpus under %s is not the one tools/corpus/CORPUS.tsv pins; run tools/corpus/fetch.sh "
             "(%d problems)" % (root, len(problems)))
    return rows


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=("fetch", "verify", "list", "licenses"))
    parser.add_argument("--root", default=DEFAULT_ROOT)
    parser.add_argument("--goal", choices=GOALS)
    args = parser.parse_args()
    if args.command == "licenses":
        rows = load_table()
        check_table(rows)
        for row in rows:
            print("%s\t%s\t%s\t%s" % (row.package, row.version, row.license, row.member))
    elif args.command == "fetch":
        fetch(args.root)
        print("corpus: %d files verified under %s" % (len(load_table()), args.root))
    elif args.command == "verify":
        rows = verify(args.root)
        print("corpus: %d files verified under %s" % (len(rows), args.root))
    else:
        for row in verify(args.root):
            if args.goal is None or row.goal == args.goal:
                print("%s\t%s" % ("module" if row.goal == "module" else "sloppy", row.path(args.root)))


if __name__ == "__main__":
    main()

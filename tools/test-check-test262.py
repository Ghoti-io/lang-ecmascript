#!/usr/bin/env python3
"""Self-test of tools/check-test262.py on a synthetic corpus.

The real corpus has no tag in the third state ("in when the feature is on"),
so the gate's handling of it can only be shown on a synthetic one; this also
shows each way the gate must refuse (a missing, altered or extra file, a file
or a tag no table names, a failing pair outside the ledger, a ledger pair that
passes, a parse-negative refused with the wrong result) on a corpus small
enough to alter. Each scenario starts from a corpus the gate passes, so a
refusal is the scenario's and nothing else's.
"""
import os
import shutil
import stat
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
GATE = os.path.join(HERE, "check-test262.py")

TESTS = {
    "harness/assert.js": "function assert(){}\n",
    "test/language/a.js": "/*---\ndescription: a plain test\n---*/\nvar a;\n",
    # A header with CR-only line terminators, as one real test262 file has, and
    # a block-style list.
    "test/language/cr.js": "/*---\rdescription: cr\rfeatures:\r  - Symbol\rflags: [onlyStrict]\r---*/\rvar c;\r",
    "test/language/neg.js": "/*---\ndescription: n\nnegative:\n  phase: parse\n  type: SyntaxError\n---*/\nSYNTAXERROR\n",
    "test/language/resolution.js": "/*---\ndescription: r\nflags: [module]\nnegative:\n  phase: resolution\n  type: SyntaxError\n---*/\nimport './fx_FIXTURE.js';\n",
    "test/language/fx_FIXTURE.js": "export var x;\n",
    "test/language/bad_FIXTURE.js": "BADMODULE\n",
    "test/language/importsbad.js": "/*---\ndescription: i\nflags: [module]\n---*/\nimport './bad_FIXTURE.js';\n",
    "test/language/gated.js": "/*---\ndescription: g\nfeatures: [synthetic-gated]\n---*/\nvar g;\n",
    "test/language/gatedfx_FIXTURE.js": "export var y;\n",
    "test/language/gatedimporter.js": "/*---\ndescription: gi\nfeatures: [synthetic-gated]\nflags: [module]\n---*/\nimport './gatedfx_FIXTURE.js';\n",
    "test/language/later.js": "/*---\ndescription: l\nfeatures: [a-later-edition]\n---*/\nvar l;\n",
    "test/staging/s.js": "/*---\ndescription: s\n---*/\nvar s;\n",
    "README.md": "x\n",
}
CLASSIFICATION = """\
toplevel:\tout\tnot a test\tnever
test/staging/\tout\tnot consensus tests\tlater
unicode-newer:\tout\tnewer unicode\tlater
test/language/bad_FIXTURE.js\tfixture-refuse\tearly error as a module\t-
harness/\tinclude\tharness\t-
suffix:_FIXTURE.js\tfixture\tan imported module\t-
test/language/\tin\tthe language\t-
"""
TAGS = """\
Symbol\tin\tsec-x\tdefines it
a-later-edition\tout\tabsent:nothing\tnot in the edition
synthetic-gated\tin-when-on\tsyntheticfeature\tthe synthetic feature of this self-test
"""

# The fake driver answers by file content, so that every result is chosen here.
DRIVER = """\
#!/usr/bin/env python3
import os, sys
for line in sys.stdin:
    mode, path = line.rstrip("\\n").split("\\t")
    text = open(path, encoding="utf-8", errors="replace").read()
    override = os.environ.get("FAKE_ANSWER_" + os.path.basename(path).replace(".", "_"))
    if override:
        print(override)
    elif "SYNTAXERROR" in text or "BADMODULE" in text:
        print("ERR_SYNTAX")
    else:
        print("OK")
"""


class Corpus:
    def __init__(self, base):
        self.root = os.path.join(base, "corpus")
        self.work = base
        for name, text in TESTS.items():
            path = os.path.join(self.root, name)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w", encoding="utf-8", newline="") as handle:
                handle.write(text)
        self.files = {
            "classification": CLASSIFICATION, "tags": TAGS, "pending": "",
        }
        self.driver = os.path.join(base, "driver.py")
        with open(self.driver, "w") as handle:
            handle.write(DRIVER)
        os.chmod(self.driver, os.stat(self.driver).st_mode | stat.S_IXUSR)
        self.write_tables()
        self.resum()

    def write_tables(self):
        for key, text in self.files.items():
            with open(os.path.join(self.work, key + ".tsv"), "w", encoding="utf-8") as handle:
                handle.write(text)

    def resum(self):
        subprocess.run([sys.executable, GATE, "sums", "--root", self.root,
                        "--sums", os.path.join(self.work, "sums")],
                       check=True, stdout=subprocess.DEVNULL)

    def run(self, features="", env=None):
        e = dict(os.environ)
        e.update(env or {})
        args = [sys.executable, GATE, "check", "--root", self.root,
                "--sums", os.path.join(self.work, "sums"),
                "--classification", os.path.join(self.work, "classification.tsv"),
                "--tags", os.path.join(self.work, "tags.tsv"),
                "--pending", os.path.join(self.work, "pending.tsv"),
                "--driver", self.driver, "--features", features]
        done = subprocess.run(args, capture_output=True, text=True, env=e)
        return done.returncode, done.stdout, done.stderr


class GateTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp(prefix="glecma-t262-")
        self.c = Corpus(self.dir)

    def tearDown(self):
        shutil.rmtree(self.dir)

    def refuses(self, needle, **kwargs):
        code, out, err = self.c.run(**kwargs)
        self.assertNotEqual(code, 0, "the gate passed; stdout:\n" + out)
        self.assertIn(needle, err)

    def test_baseline_passes_and_counts(self):
        code, out, err = self.c.run()
        self.assertEqual(code, 0, err)
        # Pairs: harness 2; a 2; cr (onlyStrict) 1; neg 2; resolution 1;
        # fx 1 (imported by resolution); bad 1; importsbad 1. gated, its
        # fixture and its importer are out with the set empty.
        self.assertIn("pairs (file, mode): 11", out)
        self.assertIn("score: 11 passed of 11", out)
        self.assertIn("tag: synthetic-gated (feature syntheticfeature off)", out)
        self.assertIn("tag: a-later-edition", out)
        self.assertIn("fixture imported only by out-of-scope tests", out)

    def test_third_state_is_in_with_its_feature_on(self):
        code, out, err = self.c.run(features="syntheticfeature")
        self.assertEqual(code, 0, err)
        # gated (2 modes), its importer (module) and its fixture join.
        self.assertIn("pairs (file, mode): 15", out)
        self.assertIn("features on: syntheticfeature", out)
        self.assertNotIn("feature syntheticfeature off", out)

    def test_missing_file(self):
        os.unlink(os.path.join(self.c.root, "test/language/a.js"))
        self.refuses("1 missing file(s)")

    def test_altered_file(self):
        with open(os.path.join(self.c.root, "test/language/a.js"), "a") as handle:
            handle.write("// edited\n")
        self.refuses("1 altered file(s)")

    def test_extra_file(self):
        with open(os.path.join(self.c.root, "test/language/extra.js"), "w") as handle:
            handle.write("/*---\ndescription: e\n---*/\n")
        self.refuses("1 extra file(s)")

    def test_file_no_row_names(self):
        self.c.files["classification"] = CLASSIFICATION.replace("harness/\tinclude\tharness\t-\n", "")
        self.c.write_tables()
        self.refuses("harness/assert.js is named by no row")

    def test_stale_classification_row(self):
        self.c.files["classification"] = CLASSIFICATION + "nothing-here/\tout\tstale\tnever\n"
        self.c.write_tables()
        self.refuses("matches no file at this pin")

    def test_tag_with_no_row(self):
        self.c.files["tags"] = TAGS.replace("Symbol\tin\tsec-x\tdefines it\n", "")
        self.c.write_tables()
        self.refuses("the tag 'Symbol' has no row")

    def test_failing_pair_outside_the_ledger(self):
        self.refuses("not in the pending ledger",
                     env={"FAKE_ANSWER_a_js": "ERR_UNSUPPORTED"})

    def test_ledger_pair_that_passes(self):
        self.c.files["pending"] = "test/language/a.js\t*\tnot built\tlater\n"
        self.c.write_tables()
        self.refuses("now pass")

    def test_ledger_row_that_names_nothing(self):
        self.c.files["pending"] = "test/language/zzz.js\t*\tstale\tlater\n"
        self.c.write_tables()
        self.refuses("match no pair")

    def test_pending_pair_is_not_a_pass(self):
        self.c.files["pending"] = "test/language/a.js\t*\tnot built\tlater\n"
        self.c.write_tables()
        code, out, err = self.c.run(env={"FAKE_ANSWER_a_js": "ERR_UNSUPPORTED"})
        self.assertEqual(code, 0, err)
        self.assertIn("score: 9 passed of 11 pairs", out)
        self.assertIn("pending 2", out)

    def test_negative_refused_with_another_result_does_not_pass(self):
        # The refusal is ERR_UNSUPPORTED, not ERR_SYNTAX: not a pass.
        self.refuses("not in the pending ledger",
                     env={"FAKE_ANSWER_neg_js": "ERR_UNSUPPORTED"})
        self.c.files["pending"] = "test/language/neg.js\t*\tnot built\tlater\n"
        self.c.write_tables()
        code, out, err = self.c.run(env={"FAKE_ANSWER_neg_js": "ERR_UNSUPPORTED"})
        self.assertEqual(code, 0, err)
        self.assertIn("score: 9 passed of 11 pairs", out)

    def test_negative_accepted_does_not_pass(self):
        self.refuses("not in the pending ledger", env={"FAKE_ANSWER_neg_js": "OK"})

    def test_resolution_phase_negative_must_parse(self):
        self.refuses("not in the pending ledger",
                     env={"FAKE_ANSWER_resolution_js": "ERR_SYNTAX"})

    def test_fixture_that_must_be_refused(self):
        self.refuses("not in the pending ledger",
                     env={"FAKE_ANSWER_bad_FIXTURE_js": "OK"})

    def test_a_fixture_no_test_imports(self):
        path = os.path.join(self.c.root, "test/language/lonely_FIXTURE.js")
        with open(path, "w") as handle:
            handle.write("export var z;\n")
        self.c.resum()
        self.refuses("lonely_FIXTURE.js is imported by no file")

    def test_a_test_without_a_header(self):
        path = os.path.join(self.c.root, "test/language/nohead.js")
        with open(path, "w") as handle:
            handle.write("var q;\n")
        self.c.resum()
        self.refuses("has no test262 metadata header")

    def test_an_unknown_flag_is_refused_not_ignored(self):
        path = os.path.join(self.c.root, "test/language/flag.js")
        with open(path, "w") as handle:
            handle.write("/*---\ndescription: f\nflags: [somethingNew]\n---*/\n")
        self.c.resum()
        self.refuses("unknown flag")

    def test_no_pair_in_scope_is_a_failure(self):
        self.c.files["classification"] = "toplevel:\tout\tx\tnever\ntest/\tout\tx\tnever\nharness/\tout\tx\tnever\n"
        self.c.write_tables()
        self.refuses("in scope")


if __name__ == "__main__":
    unittest.main(verbosity=1)

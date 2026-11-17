#!/usr/bin/env python3
"""Self-test of tools/check-clauses.py and tools/ecma262/make-concordance.py.

Builds a small synthetic pair of editions (so a clause that moves, one that is
new and one that is gone can be seen in the concordance), a tag table and some
sources, and shows the gate refusing each way it must: a clause cited in a
comment, in the rule table or in a test that the concordance lacks; a tag row
that cites an unknown clause; an `out` tag whose absence probe is in the
edition's text; a stale concordance; a missing edition source.
"""
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
GATE = os.path.join(HERE, "check-clauses.py")
MAKE = os.path.join(HERE, "ecma262", "make-concordance.py")

SPEC16 = """<emu-intro id="sec-intro"><h1>Introduction</h1></emu-intro>
<emu-clause id="sec-one"><h1>One</h1>
  <emu-clause id="sec-a"><h1>Alpha</h1></emu-clause>
  <emu-clause id="sec-b"><h1>Beta ( _x_ )</h1></emu-clause>
</emu-clause>
<emu-clause id="sec-two"><h1>Two</h1></emu-clause>
<emu-annex id="sec-annex"><h1>Annex</h1>
  <emu-clause id="sec-web"><h1>Web</h1></emu-clause>
</emu-annex>
"""
# 17th: sec-a is gone, sec-new is new, sec-b moves down, sec-web is renamed
# sec-web2 (oldids), and an HTML comment holds a clause tag that must not count.
SPEC17 = """<emu-intro id="sec-intro"><h1>Introduction</h1></emu-intro>
<emu-clause id="sec-one"><h1>One</h1>
  <emu-clause id="sec-new"><h1>New</h1></emu-clause>
  <emu-clause id="sec-b"><h1>Beta ( _x_ )</h1></emu-clause>
  <!-- <emu-clause id="sec-commented"><h1>Not a clause</h1></emu-clause> -->
</emu-clause>
<emu-clause id="sec-two"><h1>Two</h1><p>private PrivateFieldAdd text</p></emu-clause>
<emu-annex id="sec-annex"><h1>Annex</h1>
  <emu-clause id="sec-web2" oldids="sec-web"><h1>Web</h1></emu-clause>
</emu-annex>
"""
TAGS = """\
Alpha\tin\tsec-b\tdefined
Beta\tout\tabsent:Gamma\tnot in the edition
Scoped\tout\tabsent:sec-two:IsExtensible\tnot in the clause
Synth\tin-when-on\tsynthfeature\tsynthetic
"""
FAILURE_C = """static const Row rules[] = {
  {GLECMA_RULE_X, "x", "1.1",
   "message"},
  {GLECMA_RULE_Y, "y",
      "2", "message"},
};
"""


class Root:
    def __init__(self, base):
        self.root = os.path.join(base, "lib")
        self.editions = os.path.join(base, "editions")
        for sub in ("tools/ecma262", "documentation", "tests", "src/core", "tests/unit"):
            os.makedirs(os.path.join(self.root, sub))
        shutil.copy(MAKE, os.path.join(self.root, "tools/ecma262/make-concordance.py"))
        with open(os.path.join(self.root, "tools/ecma262/EDITIONS"), "w") as handle:
            handle.write("c16 16th\nc17 17th\n")
        for commit, text in (("c16", SPEC16), ("c17", SPEC17)):
            os.makedirs(os.path.join(self.editions, commit))
            with open(os.path.join(self.editions, commit, "spec.html"), "w") as handle:
                handle.write(text)
        self.write("tests/data-test262-tags.tsv", TAGS)
        self.write("src/core/failure.c", FAILURE_C)
        self.write("src/a.c", "// Clause 1.1 and clauses 2, A.1 are cited.\n")
        self.regenerate()

    def write(self, rel, text):
        with open(os.path.join(self.root, rel), "w", encoding="utf-8") as handle:
            handle.write(text)

    def regenerate(self):
        out = subprocess.run([sys.executable, os.path.join(self.root, "tools/ecma262/make-concordance.py"),
                              os.path.join(self.editions, "c16/spec.html"),
                              os.path.join(self.editions, "c17/spec.html")],
                             check=True, capture_output=True, text=True).stdout
        self.write("documentation/clause-concordance.tsv", out)

    def run(self):
        done = subprocess.run([sys.executable, GATE, "--root", self.root,
                               "--editions", self.editions], capture_output=True, text=True)
        return done.returncode, done.stdout, done.stderr


class ConcordanceTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp(prefix="glecma-clauses-")
        self.r = Root(self.dir)

    def tearDown(self):
        shutil.rmtree(self.dir)

    def rows(self):
        out = {}
        with open(os.path.join(self.r.root, "documentation/clause-concordance.tsv")) as handle:
            for line in handle:
                if not line.startswith("#"):
                    c = line.rstrip("\n").split("\t")
                    out[c[0]] = c
        return out

    def test_the_numbers(self):
        rows = self.rows()
        self.assertEqual(rows["sec-one"][1:3], ["1", "1"])
        self.assertEqual(rows["sec-b"][1:3], ["1.2", "1.2"])   # a gone, new first
        self.assertEqual(rows["sec-a"][1:3], ["1.1", ""])       # only the 16th
        self.assertEqual(rows["sec-new"][1:3], ["", "1.1"])     # only the 17th
        self.assertEqual(rows["sec-web2"][1:3], ["A.1", "A.1"]) # followed oldids
        self.assertEqual(rows["sec-web2"][4], "was sec-web")
        self.assertNotIn("sec-web", rows)
        self.assertNotIn("sec-commented", rows)
        self.assertEqual(rows["sec-b"][3], "Beta")              # signature cut

    def test_passes(self):
        code, out, err = self.r.run()
        self.assertEqual(code, 0, err)

    def test_unknown_clause_in_a_comment(self):
        self.r.write("src/b.c", "// see clause 9.9 for this\n")
        code, out, err = self.r.run()
        self.assertNotEqual(code, 0)
        self.assertIn("src/b.c:1 cites clause 9.9", err)

    def test_a_clause_only_the_16th_has_is_not_citable(self):
        self.r.write("src/b.c", "// clause 1.1 is Alpha, which is gone... clause 1.3\n")
        code, out, err = self.r.run()
        self.assertNotEqual(code, 0)
        self.assertIn("clause 1.3", err)

    def test_unknown_clause_in_a_wrapped_comment(self):
        self.r.write("src/b.c", "// the rule comes from clause\n// 7.7 of the edition\n")
        code, out, err = self.r.run()
        self.assertNotEqual(code, 0)
        self.assertIn("7.7", err)

    def test_unknown_clause_in_the_rule_table(self):
        self.r.write("src/core/failure.c", FAILURE_C.replace('"1.1"', '"8.8"'))
        code, out, err = self.r.run()
        self.assertNotEqual(code, 0)
        self.assertIn("src/core/failure.c:2 cites clause 8.8", err)

    def test_unknown_clause_on_a_wrapped_rule_row(self):
        self.r.write("src/core/failure.c", FAILURE_C.replace('"2"', '"2.9"'))
        code, out, err = self.r.run()
        self.assertNotEqual(code, 0)
        self.assertIn("cites clause 2.9", err)

    def test_unknown_clause_in_a_test_assertion(self):
        self.r.write("tests/unit/t.cpp", 'EXPECT_STREQ(glecma_rule_clause(GLECMA_RULE_X), "4.4");\n')
        code, out, err = self.r.run()
        self.assertNotEqual(code, 0)
        self.assertIn("clause 4.4", err)

    def test_tag_cites_an_unknown_clause(self):
        self.r.write("tests/data-test262-tags.tsv", TAGS.replace("sec-b", "sec-nope"))
        code, out, err = self.r.run()
        self.assertNotEqual(code, 0)
        self.assertIn("cites sec-nope", err)

    def test_tag_cites_a_clause_only_the_16th_has(self):
        self.r.write("tests/data-test262-tags.tsv", TAGS.replace("sec-b", "sec-a"))
        code, out, err = self.r.run()
        self.assertNotEqual(code, 0)
        self.assertIn("cites sec-a", err)

    def test_out_tag_present_in_the_text(self):
        self.r.write("tests/data-test262-tags.tsv", TAGS.replace("absent:Gamma", "absent:Beta"))
        code, out, err = self.r.run()
        self.assertNotEqual(code, 0)
        self.assertIn("contains 'Beta'", err)

    def test_scoped_probe_present_in_its_clause(self):
        self.r.write("tests/data-test262-tags.tsv",
                     TAGS.replace("sec-two:IsExtensible", "sec-two:PrivateFieldAdd"))
        code, out, err = self.r.run()
        self.assertNotEqual(code, 0)
        self.assertIn("contains 'PrivateFieldAdd'", err)

    def test_stale_concordance(self):
        with open(os.path.join(self.r.root, "documentation/clause-concordance.tsv")) as handle:
            text = handle.read()
        self.r.write("documentation/clause-concordance.tsv",
                     text.replace("sec-two\t2\t2", "sec-two\t2\t3"))
        code, out, err = self.r.run()
        self.assertNotEqual(code, 0)
        self.assertIn("is not what", err)

    def test_missing_edition_source(self):
        os.unlink(os.path.join(self.r.editions, "c16/spec.html"))
        code, out, err = self.r.run()
        self.assertNotEqual(code, 0)
        self.assertIn("fetch.sh", err)


if __name__ == "__main__":
    unittest.main(verbosity=1)

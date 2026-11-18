#!/usr/bin/env python3
"""The Node differential's data: the pin parses and names the pinned image by
digest, the classification table loads and names only known classes, and the
generator is deterministic (the table names generated programs by index).
Offline: no oracle, no driver."""
import hashlib
import importlib.util
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("nd", os.path.join(HERE, "check-node-differential.py"))
nd = importlib.util.module_from_spec(spec)
spec.loader.exec_module(nd)

pin = nd.read_pin()
assert "@sha256:" in pin["image"] and len(pin["image"].split("@sha256:")[1]) == 64, pin
rows = nd.load_table(os.path.join(HERE, "..", "tests", "data-node-differences.tsv"))
assert rows, "the classification table is empty"
digests = []
for run in range(2):
    with tempfile.TemporaryDirectory() as out:
        subprocess.run([sys.executable, "-I", os.path.join(HERE, "gen-programs.py"), "--out", out, "--count", "300",
                        "--seed", str(nd.GENERATED_SEED)], check=True, stdout=subprocess.DEVNULL)
        h = hashlib.sha256()
        for name in sorted(os.listdir(out)):
            h.update(open(os.path.join(out, name), "rb").read())
        digests.append(h.hexdigest())
assert digests[0] == digests[1], "the generator is not deterministic"
print("test-check-node-differential: the pin, %d classification rows and the generator are well-formed" % len(rows))

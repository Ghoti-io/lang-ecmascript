/**
 * Node as an accept/refuse oracle for the parser (tools/check-node-differential.py).
 *
 * Reads "mode<TAB>path" lines on standard input, the lines the library's own
 * driver reads (tests/t262/t262_driver.c), and writes one answer per line on
 * standard output: ACCEPT, REFUSE (V8 raised a SyntaxError, then a tab and its
 * message) or OTHER (anything else it raised, which is no answer: a RangeError
 * from the engine's own stack, for example). Nothing is run: a script is only
 * compiled (vm.Script) and a module only parsed (vm.SourceTextModule), and
 * neither is evaluated or linked.
 *
 * The modes are the driver's: `sloppy` and `raw` a Script as written, `strict` a
 * Script with `"use strict";` and a newline put in front of the text, `module`
 * a Module. The version line, the first line written to standard error, names
 * the oracle: the version of node, of V8 and of the Unicode data it carries,
 * and the flags the process was started with.
 *
 * Copyright 2026 by Corey Pennycuff
 */
import { readFileSync } from "node:fs";
import vm from "node:vm";

process.stderr.write(
  `node ${process.version}, V8 ${process.versions.v8}, Unicode ${process.versions.unicode ?? "?"}, ` +
  `flags ${JSON.stringify(process.execArgv)}\n`);

const lines = readFileSync(0, "utf8").split("\n");
const out = [];
for (const line of lines) {
  if (line === "") {
    continue;
  }
  const tab = line.indexOf("\t");
  const mode = line.slice(0, tab);
  const path = line.slice(tab + 1);
  // Decoded as UTF-8 the way the library reads a file, except that an ill-formed
  // sequence becomes U+FFFD here; the comparison marks such inputs by name.
  let text = readFileSync(path, "utf8");
  if (mode === "strict") {
    text = '"use strict";\n' + text;
  }
  try {
    if (mode === "module") {
      new vm.SourceTextModule(text, { identifier: "oracle" });
    } else if (mode === "sloppy" || mode === "strict" || mode === "raw") {
      new vm.Script(text, { filename: "oracle" });
    } else {
      throw new Error("unknown mode " + mode);
    }
    out.push("ACCEPT");
  } catch (e) {
    if (e instanceof SyntaxError) {
      out.push("REFUSE\t" + String(e.message).split("\n")[0].replace(/\t/g, " "));
    } else {
      out.push("OTHER\t" + String(e && e.name) + ": " + String(e && e.message).split("\n")[0].replace(/\t/g, " "));
    }
  }
}
process.stdout.write(out.join("\n") + (out.length ? "\n" : ""));

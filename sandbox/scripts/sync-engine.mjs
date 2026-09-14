// Re-extracts the SocialLang engine from ../web/index.html's first <script>
// block into src/engine/sociallang.js, converting its IIFE-plus-global-assign
// wrapper into plain ESM exports. Run this after any change to
// sociallang/lang/{lexer,parser,interpreter,memory}.py that also needs
// mirroring into the JS port (see web/README.md) -- this sandbox and
// web/index.html share the exact same engine file lineage, just wrapped
// differently for each host (a <script> global there, an ES module here).
import { readFileSync, writeFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const __dirname = dirname(fileURLToPath(import.meta.url));
const webIndexPath = join(__dirname, "..", "..", "web", "index.html");
const outPath = join(__dirname, "..", "src", "engine", "sociallang.js");

const html = readFileSync(webIndexPath, "utf8");
const scripts = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map((m) => m[1]);
if (scripts.length === 0) throw new Error("no <script> blocks found in web/index.html");
const engineSrc = scripts[0];

const lines = engineSrc.split("\n");
const useStrictIdx = lines.findIndex((l) => l.trim() === '"use strict";');
const iifeIdx = useStrictIdx + 1;
if (useStrictIdx === -1 || lines[iifeIdx]?.trim() !== "(function (global) {") {
  throw new Error("engine script's expected IIFE wrapper header not found -- check web/index.html's structure");
}
const body = [...lines.slice(0, useStrictIdx), ...lines.slice(iifeIdx + 1)].join("\n");

const marker =
  "global.SocialLang = { tokenize, parse, LexError, ParseError, SLRuntimeError, Interpreter, Agent, Location, EventRec, assignAgents, runSource, SeededRandom, MockProvider, setupWorldLocations };";
if (!body.includes(marker)) {
  throw new Error("expected global.SocialLang export marker not found -- check web/index.html's structure");
}
let out = body.replace(
  marker,
  "export { tokenize, parse, LexError, ParseError, SLRuntimeError, Interpreter, Agent, Location, EventRec, assignAgents, runSource, SeededRandom, MockProvider, setupWorldLocations };",
);
out = out.trimEnd();
if (!out.endsWith("})(window);")) {
  throw new Error("expected trailing '})(window);' not found -- check web/index.html's structure");
}
out = out.slice(0, -"})(window);".length).trimEnd() + "\n";

writeFileSync(outPath, out, "utf8");
console.log(`wrote ${out.length} chars to ${outPath}`);

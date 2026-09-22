// Regenerates app.ico (exe + window icon) and icon.png from icon.svg.
// Uses sharp / png-to-ico from ../../sandbox/node_modules.
import { createRequire } from "node:module";
import { writeFile } from "node:fs/promises";
const require = createRequire(new URL("../../sandbox/package.json", import.meta.url));
const sharp = require("sharp");
const pngToIco = require("png-to-ico").default ?? require("png-to-ico");
const svg = new URL("./icon.svg", import.meta.url);
const sizes = [16, 24, 32, 48, 64, 128, 256];
const pngs = await Promise.all(sizes.map((s) => sharp(svg.pathname.slice(1), { density: 72 * s / 32 * 4 }).resize(s, s).png().toBuffer()));
await writeFile(new URL("./icon.png", import.meta.url), pngs[pngs.length - 1]);
await writeFile(new URL("./app.ico", import.meta.url), await pngToIco(pngs));
console.log("wrote app.ico and icon.png");

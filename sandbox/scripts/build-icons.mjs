// Regenerates build/icon.ico and build/icon.png (the Electron app icon) from
// public/favicon.svg -- the same night-sky sparkle mark used in the browser
// tab. Run this again after editing the favicon; electron-builder reads
// build/icon.ico (Windows) and build/icon.png (Linux/general) directly, no
// other config needed since electron-builder auto-detects files in build/.
import sharp from "sharp";
import pngToIco from "png-to-ico";
import { writeFile, unlink } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const __dirname = dirname(fileURLToPath(import.meta.url));
const root = join(__dirname, "..");
const svgPath = join(root, "public", "favicon.svg");
const tmpPngPath = join(root, "build", "icon-256.png");
const pngPath = join(root, "build", "icon.png");
const icoPath = join(root, "build", "icon.ico");

await sharp(svgPath).resize(256, 256).png().toFile(tmpPngPath);
await writeFile(pngPath, await sharp(tmpPngPath).toBuffer());
const ico = await pngToIco.default(tmpPngPath);
await writeFile(icoPath, ico);
await unlink(tmpPngPath);

console.log(`wrote ${pngPath} and ${icoPath}`);

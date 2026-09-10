/**
 * Copy the NAM wasm engine's runtime assets into public/nam/.
 *
 * The engine fetches two files at runtime: the AudioWorklet processor and the
 * wasm binary. Next's bundler does not reliably handle
 * `new URL(..., import.meta.url)` for AudioWorklet modules, so rather than
 * fight it we serve them as plain static files and point the engine at them
 * with assetBaseUrl: '/nam/' (see lib/audio/nam-rig.ts).
 *
 * Resolved through the package's own `exports` map, which publishes these two
 * paths deliberately — and notably does NOT publish ./package.json, so we
 * cannot resolve the package root and walk down to them.
 *
 * Runs from prebuild/predev, so Vercel picks it up with no extra config.
 */
import { copyFileSync, mkdirSync } from "node:fs";
import { createRequire } from "node:module";
import { basename, join } from "node:path";

const require = createRequire(import.meta.url);

const ASSETS = [
  "neural-amp-modeler-wasm/dist/engine/nam-worklet.js",
  "neural-amp-modeler-wasm/dist/engine/nam-engine.wasm",
];

const outDir = join(process.cwd(), "public", "nam");
mkdirSync(outDir, { recursive: true });

for (const spec of ASSETS) {
  const from = require.resolve(spec);
  const name = basename(from);
  copyFileSync(from, join(outDir, name));
  console.log(`nam assets: ${name} -> public/nam/${name}`);
}

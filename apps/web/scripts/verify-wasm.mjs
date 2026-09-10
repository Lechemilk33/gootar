/**
 * End-to-end check that the WebAssembly NAM engine actually makes sound.
 *
 * The whole web half rests on the claim that NAM runs in a browser. This
 * proves it against a real .nam file, in a real browser, by rendering audio
 * offline and inspecting the samples — rather than trusting the docs.
 *
 * Run with: node scripts/verify-wasm.mjs  (after `npm run build`)
 */
import { spawn } from "node:child_process";
import { copyFileSync, existsSync, mkdirSync, readdirSync, rmSync } from "node:fs";
import { join } from "node:path";
import { chromium } from "playwright";

/**
 * Find a usable Chromium.
 *
 * The preinstalled browser directory is version-suffixed and may not match the
 * version this Playwright build expects, so probe every candidate for an
 * executable that actually exists rather than trusting a naming convention.
 * Returns undefined to let Playwright use its own default.
 */
function findChromium() {
  const base = process.env.PLAYWRIGHT_BROWSERS_PATH || "/opt/pw-browsers";
  if (!existsSync(base)) return undefined;

  const candidates = readdirSync(base)
    .filter((d) => d.startsWith("chromium"))
    // Prefer full Chromium over headless_shell: AudioWorklet rendering needs
    // the complete audio stack.
    .sort((a, b) => Number(a.includes("headless")) - Number(b.includes("headless")))
    .flatMap((d) => [
      join(base, d, "chrome-linux", "chrome"),
      join(base, d, "chrome-linux", "headless_shell"),
    ]);

  return candidates.find((exe) => existsSync(exe));
}

const WEB = process.cwd();
const MODEL_SRC = join(
  WEB, "..", "..", "native", "libs", "NeuralAudio", "Utils", "Models", "BossWN-feather.nam",
);
const PUBLIC_DIR = join(WEB, "public", "__test");
const MODEL_URL = "/__test/model.nam";
const PORT = 3123;

if (!existsSync(MODEL_SRC)) {
  console.error("missing test model - run: git submodule update --init --recursive");
  process.exit(1);
}

mkdirSync(PUBLIC_DIR, { recursive: true });
copyFileSync(MODEL_SRC, join(PUBLIC_DIR, "model.nam"));

const server = spawn("npx", ["next", "start", "-p", String(PORT)], {
  cwd: WEB, stdio: "ignore", detached: false,
});

const cleanup = () => {
  server.kill("SIGTERM");
  rmSync(PUBLIC_DIR, { recursive: true, force: true });
};
process.on("exit", cleanup);

const waitForServer = async () => {
  for (let i = 0; i < 60; i++) {
    try {
      const r = await fetch(`http://127.0.0.1:${PORT}/`);
      if (r.ok) return;
    } catch { /* not up yet */ }
    await new Promise((r) => setTimeout(r, 500));
  }
  throw new Error("server did not start");
};

const main = async () => {
  await waitForServer();

  const browser = await chromium.launch({
    executablePath: findChromium(),
    args: ["--autoplay-policy=no-user-gesture-required", "--no-sandbox"],
  });
  const page = await browser.newPage();
  const logs = [];
  page.on("console", (m) => logs.push(`${m.type()}: ${m.text()}`));
  page.on("pageerror", (e) => logs.push(`pageerror: ${e.message}`));

  await page.goto(`http://127.0.0.1:${PORT}/`, { waitUntil: "load", timeout: 30000 });

  // Drive the published worklet and wasm directly, over the same protocol and
  // the same static asset URLs the app uses at runtime.
  const audio = await page.evaluate(async (modelUrl) => {
    const out = { steps: [] };
    try {
      const namJson = await (await fetch(modelUrl)).text();
      out.steps.push(`fetched model: ${namJson.length} bytes`);

      const sampleRate = 48000;
      const seconds = 0.25;
      const ctx = new OfflineAudioContext(1, sampleRate * seconds, sampleRate);

      await ctx.audioWorklet.addModule("/nam/nam-worklet.js");
      out.steps.push("worklet module registered");

      const wasmBytes = await (await fetch("/nam/nam-engine.wasm")).arrayBuffer();
      out.steps.push(`wasm fetched: ${wasmBytes.byteLength} bytes`);

      const node = new AudioWorkletNode(ctx, "nam-processor", {
        numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [1],
      });

      const request = (message, transfer = []) =>
        new Promise((resolve, reject) => {
          const id = Math.floor(Math.random() * 1e9);
          const onMessage = (e) => {
            if (e.data?.requestId !== id) return;
            node.port.removeEventListener("message", onMessage);
            e.data.ok ? resolve(e.data) : reject(new Error(e.data.error));
          };
          node.port.addEventListener("message", onMessage);
          node.port.start();
          node.port.postMessage({ ...message, requestId: id }, transfer);
        });

      await request({ type: "init", wasmBytes }, [wasmBytes]);
      out.steps.push("wasm instantiated in worklet");

      const loaded = await request({ type: "load-model", json: namJson, slimSize: -1 });
      out.modelInfo = loaded.modelInfo;
      out.steps.push("model loaded");

      // Drive a 220 Hz tone through it.
      const osc = ctx.createOscillator();
      osc.frequency.value = 220;
      const gain = ctx.createGain();
      gain.gain.value = 0.25;
      osc.connect(gain).connect(node).connect(ctx.destination);
      osc.start();

      const rendered = await ctx.startRendering();
      const data = rendered.getChannelData(0);

      // Skip the head: the worklet fades in and the model needs a moment.
      let sum = 0, peak = 0, nonFinite = 0;
      for (let i = Math.floor(data.length / 2); i < data.length; i++) {
        const v = data[i];
        if (!Number.isFinite(v)) { nonFinite++; continue; }
        sum += v * v;
        peak = Math.max(peak, Math.abs(v));
      }
      const n = data.length - Math.floor(data.length / 2);
      out.rms = Math.sqrt(sum / n);
      out.peak = peak;
      out.nonFinite = nonFinite;
      out.samples = data.length;
      out.ok = true;
    } catch (err) {
      out.error = String(err && err.message ? err.message : err);
    }
    return out;
  }, MODEL_URL);

  await browser.close();

  console.log("\n--- WASM NAM end-to-end ---");
  for (const s of audio.steps ?? []) console.log("  •", s);
  if (audio.error) {
    console.error("  FAILED:", audio.error);
    if (logs.length) console.error("  page logs:\n   ", logs.join("\n    "));
    process.exitCode = 1;
    return;
  }

  console.log(`  model info: ${JSON.stringify(audio.modelInfo)}`);
  console.log(`  rendered ${audio.samples} samples, rms=${audio.rms.toFixed(5)}, peak=${audio.peak.toFixed(5)}`);

  const problems = [];
  if (audio.nonFinite > 0) problems.push(`${audio.nonFinite} non-finite samples`);
  if (!(audio.rms > 1e-5)) problems.push("output is silent");
  if (!(audio.peak < 10)) problems.push("output is wildly out of range");

  if (problems.length) {
    console.error("  FAILED:", problems.join("; "));
    process.exitCode = 1;
  } else {
    console.log("  PASSED: NAM ran in the browser and produced audible, finite audio.");
  }
};

main().catch((e) => { console.error(e); process.exitCode = 1; });

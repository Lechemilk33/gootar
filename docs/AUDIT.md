# Plan audit

Everything below was checked against primary sources — upstream source code,
package registries, and the shipped `.d.ts` files — not recalled. Where a claim
in the original plan turned out to be right, that is stated too, because knowing
which parts are load-bearing matters as much as knowing which are wrong.

---

## 1. The open question: WebAssembly — **confirmed, and better than assumed**

NAM runs in the browser today, as a maintained, packaged, prebuilt library.

- **[`tone-3000/neural-amp-modeler-wasm`](https://github.com/tone-3000/neural-amp-modeler-wasm)**,
  MIT, on npm as `neural-amp-modeler-wasm` (v2.0.1 at time of writing, actively
  released — 80+ published versions).
- It is `NeuralAmpModelerCore` compiled with Emscripten, running in an
  AudioWorklet.
- The wasm binary is **415 KB**. Prebuilt and shipped in the package; there is
  no Emscripten toolchain to stand up.
- It supports A1/A2 architectures, slimmable models, and IR convolution.
- **It deliberately avoids SharedArrayBuffer**, so it needs no COOP/COEP
  headers. This is the detail that matters most for you: cross-origin isolation
  on Vercel is a genuine headache, and you get to skip it entirely.

### Consequence: delete the server-side rendering fallback

The fallback plan — render audio on Vercel — should be dropped, not kept in
reserve. It is slower, it costs compute, it makes auditioning feel like a
file-upload workflow instead of an instrument, and it is now solving a problem
that does not exist.

### The engine API is a better fit than the React component

The package's headline export is a `T3kPlayer` React component, which is not
what you want — you are building your own librarian, not embedding Tone3000's
player. But there is a second, framework-agnostic entry point:

```ts
import { NamEngine, NamNode, NamNodePool } from "neural-amp-modeler-wasm/engine";
```

That subpath is what this repo uses. Two consequences:

- **It sidesteps a peer-dependency conflict.** The package declares
  `react@^18` as a peer; Next 15 requires React 19. Since we never render its
  components, that conflict is cosmetic — resolved with `legacy-peer-deps` in
  `.npmrc`, documented there.
- **`NamNodePool` already exists**: an LRU pool of nodes, each keeping its
  model loaded, "for instant replay". Someone else already built the data
  structure your A/B feature needs.

### The one real caveat, from the engine's own docs

> `loadModel` — "The model is parsed on the audio thread; rendering through
> this node pauses for the duration (typically 100–300 ms) and fades back in
> click-free."

So loading a model *while listening* is audible in the browser. It fades rather
than clicks, but it is a gap. **Gapless A/B therefore means never loading during
playback** — preload each model into its own node and crossfade between them.
That is what `apps/web/lib/audio/nam-rig.ts` implements, and it is why the UI
has an explicit "Preload all" step.

Pleasantly, this mirrors the native design exactly: load off the audio path,
hand over atomically. The web half now prototypes the interaction before any C++
exists.

---

## 2. Signal chain — **your version is correct**, with four additions

Verified line-by-line against `NeuralAmpModeler::ProcessBlock`. Your order is
right, including the split gate. Four things the summary omitted that will
change your output if you miss them:

1. **Input is collapsed to mono before the model.** `kNumChannelsInternal` is 1.
   The whole chain is mono; stereo re-enters only at `_ProcessOutput`.
2. **Denormals are disabled around the entire block**, via `std::feholdexcept`
   / `disable_denormals()` / `std::feupdateenv`. Omit this and you get CPU
   spikes on decaying tails — the classic "it only glitches when I stop
   playing" bug.
3. **`_ApplyDSPStaging()` runs at the top of every block.** This is the stock
   plugin's hot-swap mechanism, already sitting in the code you were reading.
   It confirms the design and gives you a reference implementation.
4. **Input and output levels are applied *inside* `_ProcessInput` /
   `_ProcessOutput`**, not as separate stages — and output level interacts with
   `kOutputMode` and the model's own calibration metadata.

Full transcription with the source excerpt: [`SIGNAL-CHAIN.md`](./SIGNAL-CHAIN.md).

---

## 3. Two gotchas that are already solved for you

### `prewarm()` — NeuralAudio does it automatically

```cpp
NeuralModel* CreateFromFile(const std::filesystem::path& modelPath, bool doPrewarm = true);
```

It defaults to true. Do not hand-roll prewarming; you would be doing it twice.
`Prewarm()` stays exposed for the case where you build a model some other way.

### Sample rate — but not the way you described it

NeuralAudio does **not** resample. It scales WaveNet convolution dilations at
load time, and the README is explicit about the limit:

> "WaveNet models are altered … when the external sample rate is an **even
> multiple** of the model sample rate."

44.1 kHz is not an even multiple of 48 kHz, so this cannot correct it. Your
instinct was right and the fix is trivial: **run the device at 48 k.** For a
personal rig you control, this is a non-issue.

The part that *does* bite: **models are only altered on load.** If the ASIO
sample rate changes, every loaded model in the chain must be reloaded. That is a
real constraint on the hot-swap design, not a footnote — handle it as a
device-change event that re-stages everything.

Also from the README: **loading is explicitly not real-time safe**, and
`SetMaxAudioBufferSize()` must be set before processing because some models
allocate against it.

---

## 4. Where I disagree

### 4.1 Hot-swap is architecture, not milestone 6 — **this is the big one**

Your milestone list puts hot-swapping at step 6, after preset save/load. That
ordering is a trap. If milestones 2–5 are built the obvious way —

```cpp
void setModel(const std::string& path) {
    model = loader.CreateFromFile(path);   // allocates, file I/O, on the UI thread
}
```

— then milestone 6 is not "add a feature", it is "restructure the audio engine
and everything touching it". The naive version also *appears to work* while you
are testing with one model at a time, so nothing warns you.

The indirection costs one header, written now: **`native/src/ModelSwapper.h`**.
The audio thread calls `applyStaged()` at the top of each block and `current()`
to process; the loader thread calls `stage()` and `collectRetired()`. No
allocation, no locks, no file I/O on the audio thread, and destruction happens
on the loader thread after the audio thread has provably stopped using the old
model.

It ships with a concurrent stress test (`ModelSwapperTest.cpp`, 20 000 model
swaps against a live audio thread) that is **clean under ThreadSanitizer and
under AddressSanitizer+UBSan**, and CI runs all three on every change to
`native/`. Writing it first also revealed a genuine bug in the obvious
implementation: a load-then-CAS handover can adopt a pointer the loader thread
has already destroyed. The committed version uses a single atomic exchange, with
the reasoning in the comments.

**Build milestones 2–5 on top of this from the start.** Keep your milestone
order otherwise; just make step 6 a UI change rather than a rewrite.

### 4.2 "Share rigs by link" is broken unless models are identified by content hash

This is the architectural gap in the plan. The library lives on **your disk**;
the librarian lives in **the cloud**. A preset that says `C:\NAM\marshall.nam`
means nothing to anyone else — including to you on a different machine, or after
you reorganise a folder.

Uploading hundreds of `.nam` files to solve it is the wrong trade: it is slow,
it costs storage, and it still does not help the native player, which needs the
files locally regardless.

**Identify every model and IR by SHA-256 of its bytes.** Then:

- The cloud stores only *metadata* — tags, ratings, presets, notes. Hundreds of
  bytes per model instead of megabytes.
- The web librarian resolves hashes against files you pick locally; nothing is
  uploaded.
- The native player resolves the same hashes against a scanned folder, on a
  different drive, with different filenames.
- A shared rig resolves against whatever the recipient already owns, and can
  name a TONE3000 id for what they do not.
- Duplicate detection — the same capture saved under three names — falls out for
  free, and that is half of what "organise 500 models" actually means.

This is implemented in `packages/preset-schema` (`AssetRef`, `AssetResolver`)
and in `apps/web/lib/library/ingest.ts`, which de-duplicates by hash on import.

### 4.3 Start the web half with no backend at all

Vercel has no persistent filesystem, so "store presets" implies Postgres plus
Blob storage plus auth — a meaningful chunk of work that has nothing to do with
guitar tone.

Since models never leave the machine and presets are small JSON, **the entire
librarian works locally** (IndexedDB, File System Access API). Add a backend
only when you actually want share-by-link, and even then it only stores
metadata. That is a large scope reduction for zero feature loss, and it means
the web half is useful on day one instead of after auth is built.

The scaffold has no backend and no database.

### 4.4 The one local install you cannot avoid

CI-built binaries are right for *releases*. They are a bad loop for *developing*
real-time audio, and I would push back on the "laptop holds only git, an editor,
and the final binary" plan for the native half specifically.

You will be chasing dropouts, denormal spikes, buffer-size edge cases and ASIO
driver quirks — none of which reproduce on a GitHub runner, all of which need
your actual interface and your actual guitar. A 5–10 minute round trip per
attempt, with no debugger, will make milestones 1–2 miserable.

MSVC Build Tools + CMake is roughly 3–6 GB against your 168 GB free. The USB
SATA SSD at ~358 MB/s will make compiles slow but entirely tolerable for a
project this size. Keep everything else — Node, the whole web toolchain — off
the laptop as planned; that part of the constraint is sound and this repo is
structured for it.

### 4.5 Your CPU budget is far less tight than you think

The Raspberry Pi 5 numbers do not transfer. A Pi 5 is a Cortex-A76 with NEON; an
i7-8750H is six cores up to 4.1 GHz **with AVX2**, which is exactly what
Eigen/RTNeural vectorise against. A Standard WaveNet at ~70 % of a Pi core lands
in the low tens of percent of one i7 core.

Practically: milestone 7 (pedal → amp → more) is comfortably affordable, and so
is keeping several models preloaded for instant A/B — which is precisely what
hot-swapping wants. Do not design around a CPU ceiling you will not hit.

### 4.6 VST3 is not a milestone

With JUCE, Standalone and VST3 are two `FORMATS` entries on one target. Turn
both on at milestone 1 and step 8 disappears. `native/CMakeLists.txt` already
does this.

Relatedly: Steinberg relicensed VST3 to MIT and added a GPLv3 option for ASIO in
October 2025, so neither SDK is the obstacle it used to be. Not that it matters
for a private build — noted only because older guides still say otherwise.

---

## 5. Decisions I agree with, and why

- **NeuralAudio over NAM Core.** Right call. Hand-optimised static paths, a
  simpler API, automatic prewarm, and it sidesteps the Eigen alignment
  workaround that would otherwise disable vectorisation in your hot loop.
- **JUCE over iPlug2.** Right for you. `juce::dsp::Convolution` handles IR
  loading with its own background thread and thread-safe swapping, ASIO is a
  build flag, and the Standalone/VST3 duality is free.
- **Preset/audition workflow as the wedge.** Stompbox has solved chaining;
  competing there would be pointless. Nobody has solved "I have 400 captures
  and no idea which is which", and hashing plus tagging plus gapless A/B is a
  genuinely good answer to it.
- **The split noise gate.** Confirmed in the source, and worth keeping exactly
  as-is — it is a real design decision, not an accident.
- **48 kHz throughout.** Correct, and now for a precise reason (see §3).

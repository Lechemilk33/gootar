# Gootar

A preset-centric [Neural Amp Modeler](https://www.neuralampmodeler.com/) rig, in
two halves that share one preset format.

**Web librarian** — organise, tag, search and audition a library of `.nam`
captures against a DI loop, and A/B them without a dropout. Runs NAM as
WebAssembly in an AudioWorklet. Deploys to Vercel.

**Native player** — the real-time engine you plug a guitar into. JUCE, ASIO,
NeuralAudio. Hot-swaps models without interrupting playing.

The gap it targets: TONE3000 has ~493 model packs and 450k+ downloads. People
accumulate hundreds of `.nam` files and have no way to organise, audition or
compare them — the stock plugin makes you file-browse one at a time.

## Status

Both halves work.

| | |
|---|---|
| Preset schema | zod source of truth, 9 tests, emits JSON Schema for the C++ side |
| Web librarian | imports + hashes + tags models, gapless A/B against a DI loop, preset save/load. NAM-in-the-browser **verified end to end** against a real capture |
| Native player | full chain (gate / model / tone stack / IR / DC blocker / levels), model browser, hot-swap, preset I/O. Standalone **and** VST3 build |
| Signal chain | an ordered list of blocks, not a fixed sequence: reorder it, or run two captures in series, without a rewrite |
| Tuner | MPM pitch detection off the clean input, ±cents readout |
| Hot-swap | 20k swaps against a live audio thread, clean under TSan and ASan/UBSan |

**[How to use it → `docs/USAGE.md`](docs/USAGE.md)**

![The native player](docs/images/player.png)

![The web librarian](docs/images/librarian.png)

## Quick start

**Windows**, all of it in one go:

```powershell
git clone --recurse-submodules https://github.com/Lechemilk33/gootar
cd gootar
.\setup.ps1          # or double-click setup.bat
```

Installs what is missing, lists your ASIO drivers, finds your captures, builds
the player and the VST3, runs the tests. See
[`docs/DEV-SETUP.md`](docs/DEV-SETUP.md).

**Anywhere**, by hand:

```bash
npm install && npm run build && npm run dev   # librarian on :3000

cmake -S native -B native/build -DCMAKE_BUILD_TYPE=Release
cmake --build native/build --parallel
ctest --test-dir native/build --output-on-failure
```

`ctest` runs the concurrency stress test and the whole signal chain against the
real `.nam` files NeuralAudio ships — WaveNet Standard/Nano/Feather, A2, and
two LSTMs.

## Docs

- **[`docs/USAGE.md`](docs/USAGE.md)** — how to actually use it, start to finish
- **[`docs/DEV-SETUP.md`](docs/DEV-SETUP.md)** — building locally on Windows, and
  turning ASIO on (CI can't)
- [`docs/AUDIT.md`](docs/AUDIT.md) — what was verified against upstream source,
  and the places the original plan needed changing
- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — how the two halves fit
- [`docs/SIGNAL-CHAIN.md`](docs/SIGNAL-CHAIN.md) — the chain, transcribed from
  the stock plugin's `ProcessBlock`, with exact parameter ranges

## Layout

```
packages/preset-schema/   zod schemas -> types + JSON Schema; the shared format
apps/web/
  lib/audio/nam-rig.ts    gapless A/B over preloaded wasm NAM nodes
  lib/audio/chain.ts      the signal chain in Web Audio
  lib/library/            hashing, .nam parsing, IndexedDB
native/
  src/ModelSwapper.h      lock-free handover to the audio thread
  src/dsp/                the whole chain, with no JUCE in it
  src/app/                JUCE shell: browser UI, presets, library scan
  libs/                   JUCE, NeuralAudio, AudioDSPTools (submodules)
docs/                     usage, audit, architecture, signal chain
```

## Deploying the web half

The repo root carries a `vercel.json` that builds the workspace in order
(schema first, then the app) and points Vercel at `apps/web/.next`. Import the
repo with **Root Directory left at the repository root** — not `apps/web`, or
the shared schema package will not be built before the app that imports it.

## Milestones

1. ~~Standalone app, audio in → out~~ done
2. ~~Load a `.nam` and hear it~~ done, on top of `ModelSwapper`
3. ~~IR convolution~~ done
4. ~~Gate + EQ + levels in the right order~~ done
5. ~~Preset save/load~~ done
6. ~~Model browser + hot-swap while playing~~ done — the product
7. ~~Chain multiple models (pedal → amp)~~ done in the engine — two captures in
   series is a chain edit, and a test covers it. What is left is a UI for
   reordering the board, and persisting a custom order in the preset file

VST3 was never a milestone in the end: JUCE builds Standalone and VST3 as two
formats of one target, on from the start.

Not done: ASIO is off in CI because the SDK is a separate download
([USAGE](docs/USAGE.md) says how to turn it on), and the librarian is
local-first with no share-by-link backend.

## Built on

[NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore) ·
[NeuralAmpModelerPlugin](https://github.com/sdatkinson/NeuralAmpModelerPlugin) ·
[NeuralAudio](https://github.com/mikeoliphant/NeuralAudio) ·
[neural-amp-modeler-wasm](https://github.com/tone-3000/neural-amp-modeler-wasm) ·
[JUCE](https://juce.com)

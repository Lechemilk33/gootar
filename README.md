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

| | |
|---|---|
| Preset schema | working, tested, emits JSON Schema |
| Web librarian | scaffold builds and deploys; imports + hashes models, gapless A/B wired |
| Native player | model handover implemented and sanitiser-clean; no JUCE app yet |

## Quick start

```bash
npm install
npm run build        # schema, then web
npm run dev          # librarian at localhost:3000
npm test             # schema tests
```

Native side:

```bash
cmake -S native -B native/build -DCMAKE_BUILD_TYPE=Release
cmake --build native/build
ctest --test-dir native/build --output-on-failure
```

## Read this first

**[`docs/AUDIT.md`](docs/AUDIT.md)** — what was verified against upstream
source, and the four places the original plan needed changing. Most important:
hot-swap is architecture rather than a late milestone, and models must be
identified by content hash rather than path or share-by-link cannot work.

- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — how the two halves fit
- [`docs/SIGNAL-CHAIN.md`](docs/SIGNAL-CHAIN.md) — the chain, transcribed from
  the stock plugin's `ProcessBlock`, with exact parameter ranges

## Layout

```
packages/preset-schema/   zod schemas → types + JSON Schema; the shared format
apps/web/                 Next.js librarian
native/                   JUCE player + the lock-free model swapper
docs/                     audit, architecture, signal chain
```

## Deploying the web half

The repo root carries a `vercel.json` that builds the workspace in order
(schema first, then the app) and points Vercel at `apps/web/.next`. Import the
repo with **Root Directory left at the repository root** — not `apps/web`, or
the shared schema package will not be built before the app that imports it.

## Milestones

1. Standalone app, ASIO in → out passthrough
2. Load one `.nam` and hear it — **on top of `ModelSwapper`, not around it**
3. IR convolution
4. Gate + EQ + levels, in the order in `SIGNAL-CHAIN.md`
5. Preset save/load
6. Model browser + hot-swap while playing — the product
7. Chain multiple models (pedal → amp)

VST3 is no longer a milestone: JUCE builds Standalone and VST3 as two formats of
one target, enabled from the start.

## Built on

[NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore) ·
[NeuralAmpModelerPlugin](https://github.com/sdatkinson/NeuralAmpModelerPlugin) ·
[NeuralAudio](https://github.com/mikeoliphant/NeuralAudio) ·
[neural-amp-modeler-wasm](https://github.com/tone-3000/neural-amp-modeler-wasm) ·
[JUCE](https://juce.com)

# Architecture

Two halves, one preset format, and one rule that makes them fit together:
**models are identified by the hash of their bytes, never by their path.**

```
        ┌──────────────────────────────┐     ┌───────────────────────────────┐
        │  WEB LIBRARIAN (Vercel)      │     │  NATIVE PLAYER (Windows)      │
        │  Next.js + NAM wasm          │     │  JUCE + NeuralAudio + ASIO    │
        │                              │     │                               │
        │  organise · tag · search     │     │  plug a guitar in             │
        │  audition against a DI loop  │     │  real-time, low latency       │
        │  gapless A/B                 │     │  gapless hot-swap             │
        └──────────────┬───────────────┘     └───────────────┬───────────────┘
                       │                                     │
                       └──────────────┬──────────────────────┘
                                      │
                      ┌───────────────▼────────────────┐
                      │  @gootar/preset-schema         │
                      │  zod types  →  JSON Schema     │
                      │  AssetRef keyed by SHA-256     │
                      └────────────────────────────────┘

    model + IR files stay on local disk on both sides — only metadata travels
```

## Why hashes

A preset that names `C:\NAM\marshall.nam` is worthless on any other machine, in
any other folder layout, or after a rename. A preset that names
`sha256:3f9a…` resolves anywhere the bytes exist, and tells you honestly when
they do not.

Both halves implement the same contract:

```ts
interface AssetResolver {
  resolve(ref: AssetRef): Promise<Uint8Array | null>;
}
```

- **Web** — an IndexedDB record pointing at a `FileSystemFileHandle`, or a
  fetch from a TONE3000 URL.
- **Native** — a hash → path map built by scanning the library folder.

Falling out of this for free: duplicate detection (the same capture under three
filenames is one entry), share-by-link that works without shipping megabytes,
and honest "model missing" state instead of a silent wrong sound.

## Layout

```
packages/preset-schema/   zod schemas, shared types, emitted JSON Schema
apps/web/                 Next.js librarian → Vercel
  lib/audio/nam-rig.ts    gapless A/B over preloaded wasm NAM nodes
  lib/library/ingest.ts   hash + parse .nam files in the browser
native/                   JUCE player
  src/ModelSwapper.h      lock-free handover to the audio thread
docs/                     this, plus AUDIT.md and SIGNAL-CHAIN.md
```

## One source of truth for the format

The zod schemas in `packages/preset-schema/src` are authoritative. JSON Schema
is *generated* from them into `schema/gootar-preset-v1.schema.json` for the C++
side to read, and CI fails if the committed output has drifted
(`git diff --exit-code` in `.github/workflows/web.yml`). There is no second
hand-maintained copy of the format to fall out of sync.

## Gapless switching, both sides

The same shape, forced by the same constraint — you cannot parse a model on the
thread that is rendering audio.

| | Web | Native |
|---|---|---|
| Load | `NamNode.loadModel()`, one node per model, ahead of time | worker thread, `CreateFromFile()` |
| Hand over | crossfade gains between running nodes | `ModelSwapper::applyStaged()` |
| Free | `node.dispose()` off the audio path | `collectRetired()` on the loader thread |

Neither side loads a model while you are listening. That is the whole trick.

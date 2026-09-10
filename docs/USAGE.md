# Using Gootar

Two halves. The **player** is the thing you plug a guitar into; the
**librarian** is the web app for organising and comparing captures. They share
one preset file.

---

## 1. Get the player onto your laptop

Your machine has no compiler, so GitHub Actions builds the binary for you.

1. Push anything under `native/` (or open the **Actions** tab and run the
   **native** workflow by hand with *Run workflow*).
2. When the **windows build** job goes green, open it and download the
   **`gootar-windows-x64`** artifact from the Summary page.
3. Unzip it. You get:
   - `Gootar Player.exe` — the standalone app
   - `Gootar Player.vst3` — the plugin, if you'd rather run it in a DAW

Put the `.exe` anywhere. It needs no installer and writes nothing except a
small settings file under `%APPDATA%\Gootar`.

> **ASIO.** The CI build ships with ASIO **off**, because Steinberg's SDK is a
> separate download that isn't in this repo. Without it the app uses WASAPI,
> which is fine for checking that everything works but too laggy to actually
> play through.
>
> To turn ASIO on: download the ASIO SDK, drop it at
> `native/libs/asiosdk` (so `native/libs/asiosdk/common/iasiodrv.h` exists),
> change `-DGOOTAR_ENABLE_ASIO=OFF` to `ON` in
> `.github/workflows/native.yml`, and push. Then pick your interface's ASIO
> driver in **Options → Audio/MIDI Settings**.

---

## 2. First run

1. Launch `Gootar Player.exe`. On first run it looks for your captures in the
   usual places (`Documents\NAM Models`, `Documents\NAM`, `Music\NAM` and a
   few others) and loads them automatically. Set `GOOTAR_MODEL_DIR` to point
   somewhere else — an external drive, say.
2. **Options → Audio/MIDI Settings**: choose your interface, set the sample
   rate to **48000 Hz**, and the buffer to 64 or 128 samples.

   48 k matters. NAM models are trained at 48 k, and NeuralAudio can only
   correct for rates that are an even multiple of that — 44.1 k is not one, so
   at 44.1 k every model is subtly wrong in a way you cannot dial out.
3. Click **Model folder...** and point it at wherever your `.nam` files live.
   Subfolders are included.

   The first scan hashes every file, so a few hundred models take a moment.
   After that the hashes are cached and rescans are instant unless files
   actually changed.
4. Click any model in the list. That is the whole interaction — **it swaps
   without interrupting what you're playing.** Arrow keys work too, so you can
   hold a chord and walk down the list with the keyboard.

The orange bar marks what's currently loaded. The status line underneath shows
how many files matched, the model's sample rate, and which IR is loaded.

---

## 2b. The tuner

Top right, always live. It reads the **clean** signal before the model, which
matters: distortion piles on harmonics and squashes the dynamics a pitch
tracker needs, so a tuner fed the amp output reports a confidently wrong note.

The note goes green within ±5 cents, amber to ±20, red beyond. Green is
deliberately not "perfect" — a guitar will not hold better than a few cents,
and a tuner that demands perfection is one you stop looking at.

Under it: input and output meters (green, amber near clipping, red at it), the
signal chain as it currently stands, and details of the loaded capture. That
last panel flags a model running on the **dynamic** path rather than a
hand-optimised static one, which costs noticeably more CPU.

## 3. The controls

Left to right along the bottom, in signal order:

| Control | Range | What it does |
|---|---|---|
| **Input** | −20…20 dB | Level into the model. This is the "gain" knob — models react to input level the way the real amp does. |
| **Gate** | −100…0 dB | Noise gate threshold. Detects on your clean signal, applies after the model. |
| **Bass / Mid / Treble** | 0…10 | Tone stack after the model: bells at 150 Hz, 425 Hz, 1800 Hz. |
| **Output** | −40…40 dB | Final level. |
| **Gate / EQ / IR** | — | Bypass each stage. |
| **Output mode** | Raw / Normalized / Calibrated | *Normalized* (the default) uses each model's own loudness metadata so models don't jump in volume as you switch. Leave it there for A/B. *Raw* disables that. |

**Load IR...** takes a `.wav` impulse response. **No IR** clears it. `.wav`
files in your model folder also show up in the browser marked `IR`, so you can
click those to swap cabs the same way.

All of these match the stock NAM plugin exactly — same frequencies, same
ranges, same defaults — so a tone you dial here transfers.

---

## 3b. Pedals

The chain isn't fixed. **+ Pedal** adds one; click any block in the chain to
select it and its knobs appear underneath.

| | |
|---|---|
| **Drive / boost** | The important one. A capture models one amp at one setting — you can't turn its gain up. What you *can* do is hit it harder, exactly like a real pedal in front of a real amp, and because the capture responds to input level the way the original did, it works. |
| **Compressor** | Evens out picking. In front of the amp it makes quiet notes still push it into breakup; after, it tames the output. |
| **Delay** | Time, feedback, mix. |
| **Reverb** | Size, damping, mix. |

New pedals land where that kind of pedal belongs — drive and compressor in
front of the amp, delay and reverb after the cab. Use **<** and **>** to move
the selected one, **Remove** to delete it.

You can also add a **second amp capture**, which gives you the pedal-platform
into amp trick: one capture as a dirt pedal feeding another as the amp.

Moving a pedal never resets it — its knobs, and any capture loaded in a slot,
come with it.

## 4. Presets

**Save preset** writes a `.json`. **Load preset** reads one back.

The important part: a preset references models by the **SHA-256 of the file**,
not by path. So it survives you reorganising folders, renaming files, or
opening the preset on a different machine. If a preset names a model you don't
have, the app says which one is missing instead of silently playing nothing.

That same file is what the librarian reads and writes.

---

## 5. The librarian

For organising, tagging and comparing — the stuff that's miserable to do while
holding a guitar.

It uses a browser as its window, but it is a **local program**. It runs at
`127.0.0.1` — your own machine talking to itself — and the amp modelling runs
on your CPU inside the page. Nothing is uploaded and nothing is served to the
internet.

### Start it

Double-click **`librarian.bat`**, or:

```bash
npm run dev          # then open http://127.0.0.1:3000
```

Leave that window open while you use it; closing it stops the UI.

### Using it

1. **Import .nam files** — select them (or a whole folder's worth). They're
   hashed and parsed on the spot, on your machine.
2. **Load DI loop** — a dry guitar `.wav` you recorded. This is what you'll
   audition against.
3. **Preload** — loads every visible model into its own audio node.

   Do this before comparing. The wasm engine parses models on the audio thread
   and stalls 100–300 ms when it does, so loading mid-listen is audible.
   Preloaded models crossfade instantly instead.
4. **Play**, then click between models. Gapless.
5. Tag them, star them, filter by tag. Tags persist in your browser.
6. **Save .json** to get a preset you can open in the player.

The librarian has no noise gate — a pre-recorded DI has nothing to gate. If you
open a preset that has gate settings, they're preserved untouched when you save
it again.

---

## 6. The loop this is built for

1. Dump a pile of new captures into your model folder.
2. Open the librarian, import them, audition against your DI, tag the keepers
   (`marshall`, `high-gain`, `cleans`), star the two or three that are actually
   good.
3. Save a preset for each keeper.
4. Open the player, load the preset, plug in, and play.

---

## Building it yourself

**[`DEV-SETUP.md`](DEV-SETUP.md)** has the full Windows walkthrough — what to
install, how big it is, and how to switch ASIO on, which CI cannot do for you.

The short version:

```bash
git clone --recurse-submodules https://github.com/Lechemilk33/gootar
cd gootar

# web
npm install && npm run build && npm test --workspace=@gootar/preset-schema
node apps/web/scripts/verify-wasm.mjs      # proves NAM runs in a browser

# native
cmake -S native -B native/build -DCMAKE_BUILD_TYPE=Release
cmake --build native/build --parallel
ctest --test-dir native/build --output-on-failure
```

`ctest` runs the concurrency stress test and the full chain against the real
`.nam` files NeuralAudio ships. Both are clean under ThreadSanitizer and
AddressSanitizer/UBSan; CI runs all of it on every push to `native/`.

---

## When something's wrong

**No sound.** Check the status line says a model is loaded, then check
Audio/MIDI Settings has the right input. The standalone mutes input by default
to avoid feedback — there's a banner at the top with a **Settings...** button.

**Crackling or dropouts.** Raise the buffer size. A Standard WaveNet costs
maybe 10–20 % of one core on your i7, so you have plenty of headroom — if it's
struggling at 128 samples, something else is wrong (check you're on ASIO, not
WASAPI).

**Everything sounds slightly off.** Check the sample rate is 48000.

**A preset won't load its model.** The app names the file it can't find. Either
that model isn't in your library folder, or you need to rescan
(**Model folder...** again).

**The model list is empty after picking a folder.** The scan runs in the
background; the status line shows progress. If it finishes at 0, there are no
`.nam` or `.wav` files under that folder.

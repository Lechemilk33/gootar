# Setting up on your laptop

You were right to want this. Two reasons, one of which matters much more than
the other:

1. **ASIO.** CI builds with ASIO off, because Steinberg's SDK is a separate
   download that can't be committed. Locally you drop the SDK in once and
   you're playing through the thing. That's not a convenience — it's the
   difference between a binary you can test and a binary you can *use*.
2. Iterating on real-time audio through a 10-minute CI round trip, with no
   debugger and no way to hear the result until an artifact downloads, is
   miserable. Dropouts and driver quirks don't reproduce on a GitHub runner.

Keep CI anyway. It's still the right thing for producing a release build and
for catching the concurrency bugs you'd never spot by ear.

---

## What to install

| | Size | Why |
|---|---|---|
| [Git for Windows](https://git-scm.com/download/win) | ~350 MB | clone + submodules |
| [Visual Studio Build Tools](https://visualstudio.microsoft.com/downloads/) — **Desktop development with C++** workload | **~6–8 GB** | MSVC compiler, Windows SDK, and CMake (bundled — no separate install) |
| [Node.js LTS](https://nodejs.org) | ~100 MB | the web librarian |

Plus the repo itself with submodules: **~1.5–2 GB** (JUCE is most of it).

**Realistically ~10 GB all in.** You have ~168 GB free, so it fits, but that's a
lot more than "git and an editor" — worth knowing before you start rather than
halfway through the Visual Studio installer.

You do **not** need the full Visual Studio IDE. Build Tools is the smaller
install and has everything CMake needs.

### On the USB SSD

At ~358 MB/s the first full build will take a while — JUCE plus NeuralAudio
plus Eigen is a lot of headers. Expect something like 5–15 minutes cold.
Incremental builds after that are fast, because only your own files recompile.

If it's painful, `-G Ninja` instead of the Visual Studio generator is
noticeably quicker (`winget install Ninja-build.Ninja`).

---

## Clone

The submodules matter — JUCE, NeuralAudio and AudioDSPTools all live there:

```powershell
git clone --recurse-submodules https://github.com/Lechemilk33/gootar
cd gootar
```

Forgot the flag? `git submodule update --init --recursive`.

---

## Build the player

```powershell
cmake -S native -B native/build -A x64
cmake --build native/build --config Release --parallel
ctest --test-dir native/build -C Release --output-on-failure
```

The binaries land at:

```
native/build/GootarPlayer_artefacts/Release/Standalone/Gootar Player.exe
native/build/GootarPlayer_artefacts/Release/VST3/Gootar Player.vst3
```

`ctest` runs the concurrency stress test and the whole signal chain against the
real `.nam` files NeuralAudio ships. Run it after any DSP change — it catches
the kind of bug you cannot hear until you're already recording.

---

## Turn ASIO on

This is the point of building locally.

1. Download the **ASIO SDK** from Steinberg's developer site.
2. Unzip it to `native/libs/asiosdk`, so that this path exists:
   ```
   native/libs/asiosdk/common/iasiodrv.h
   ```
3. Reconfigure with the flag on:
   ```powershell
   cmake -S native -B native/build -A x64 -DGOOTAR_ENABLE_ASIO=ON
   cmake --build native/build --config Release --parallel
   ```
4. Launch, then **Options → Audio/MIDI Settings** and pick your interface's
   ASIO driver. Set **48000 Hz** and a 64- or 128-sample buffer.

`native/libs/asiosdk/` is gitignored, so the SDK stays out of the repo.

---

## Run the librarian

```powershell
npm install
npm run build
npm run dev          # http://localhost:3000
```

Chromium-based browsers are best here — Firefox and Safari have patchier
AudioWorklet behaviour.

---

## The loop once you're set up

```powershell
# after changing DSP
cmake --build native/build --config Release --parallel
ctest --test-dir native/build -C Release --output-on-failure
.\native\build\GootarPlayer_artefacts\Release\Standalone\"Gootar Player.exe"
```

Build, test, listen. That's the whole point of being local.

---

## If something goes wrong

**CMake can't find a compiler.** You installed Visual Studio Build Tools but
not the *Desktop development with C++* workload. Re-run the installer and tick
it.

**`cmake` isn't a command.** It ships inside the C++ workload but isn't always
on PATH. Either use the **Developer Command Prompt for VS**, or install CMake
standalone (`winget install Kitware.CMake`).

**Submodule errors mid-build.** `git submodule update --init --recursive`.

**Build succeeds, no ASIO option in the app.** `GOOTAR_ENABLE_ASIO` was off, or
the SDK isn't at the exact path above. Delete `native/build` and reconfigure —
CMake caches the flag.

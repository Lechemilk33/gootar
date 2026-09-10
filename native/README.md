# Gootar — native player

The real-time half: the thing you plug a guitar into. JUCE for the app shell,
ASIO and IR convolution; [NeuralAudio](https://github.com/mikeoliphant/NeuralAudio)
for model inference.

## What builds today

Nothing here depends on JUCE or NeuralAudio yet. What exists is the one piece
that is genuinely hard and that everything else has to be built around:

    src/ModelSwapper.h          lock-free model handover to the audio thread
    src/ModelSwapperTest.cpp    concurrent stress test for it

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Verify the concurrency claims rather than trusting them:

```
g++ -std=c++20 -O1 -g -fsanitize=thread          -Isrc src/ModelSwapperTest.cpp -o tsan  -pthread && ./tsan
g++ -std=c++20 -O1 -g -fsanitize=address,undefined -Isrc src/ModelSwapperTest.cpp -o asan -pthread && ./asan
```

Both are clean as committed, and CI runs them on every change to `native/`.

## Adding the dependencies

```
git submodule add https://github.com/juce-framework/JUCE      native/libs/JUCE
git submodule add https://github.com/mikeoliphant/NeuralAudio native/libs/NeuralAudio
```

`CMakeLists.txt` starts building `GootarPlayer` (Standalone **and** VST3, from
one target set) once both of those and `src/PluginProcessor.cpp` exist. Until
then it configures and builds fine without them, so a fresh clone is never
broken.

ASIO needs Steinberg's SDK dropped into `libs/asiosdk` (not committed) and
`-DGOOTAR_ENABLE_ASIO=ON`. Without it you still get a WASAPI standalone, which
is enough to prove DSP correctness — just not to play through.

## Things the engine will make you get right

- **Loading is not real-time safe.** NeuralAudio says so explicitly. Load on a
  worker thread, hand over with `ModelSwapper`.
- **Prewarm is already handled.** `CreateFromFile(path, doPrewarm = true)`
  defaults to true. Do not hand-roll it; do call `Prewarm()` yourself only if
  you construct models some other way.
- **Set the max buffer size before processing.** `SetMaxAudioBufferSize()` —
  some models allocate against it, and exceeding it is undefined.
- **Sample rate is applied at load.** `SetExternalSampleRate()` only affects
  models loaded *after* it. Changing the device rate means reloading every
  model in the chain.
- **Disable denormals around the block.** The stock plugin brackets its whole
  `ProcessBlock` with `std::feholdexcept` / `disable_denormals()` /
  `std::feupdateenv`. Skipping it shows up as CPU spikes on decaying tails.

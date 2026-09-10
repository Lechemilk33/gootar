# Signal chain

Transcribed from `NeuralAmpModeler::ProcessBlock` in
[sdatkinson/NeuralAmpModelerPlugin](https://github.com/sdatkinson/NeuralAmpModelerPlugin).
Both halves of Gootar must implement this order, or the same preset will sound
different in the browser and in the player.

```
  inputs
    │
    ├─ _ProcessInput ......... collapse to mono + apply kInputLevel
    │                          (and input calibration, see below)
    ├─ _ApplyDSPStaging ...... hot-swap staged model/IR in, on the audio thread
    │
    ├─ Noise gate TRIGGER .... detects on the CLEAN input
    │
    ├─ NAM MODEL ............. mModel->process(...)
    │
    ├─ Noise gate GAIN ....... applied AFTER the model
    │
    ├─ Tone stack ............ kToneBass / kToneMid / kToneTreble, if kEQActive
    │
    ├─ IR convolution ........ if an IR is loaded and kIRToggle
    │
    ├─ DC blocker ............ 5 Hz high-pass (kDCBlockerFrequency)
    │
    └─ _ProcessOutput ........ kOutputLevel + kOutputMode, back out to stereo
```

## The gate is split on purpose

The gate **detects** on the clean input and **applies its gain** after the
model. Gating post-distortion chatters, because a distorted signal barely
changes level as the note decays — the whole point of a NAM model is that it
compresses dynamics the way the real amp does. Detecting pre-model gives the
gate a signal that actually falls away.

Trigger parameters are hard-coded in `ProcessBlock`, not exposed:

```
time 0.01 · ratio 0.1 · openTime 0.005 · holdTime 0.01 · closeTime 0.05
```

Only the threshold (`kNoiseGateThreshold`) is a parameter. These constants live
in `packages/preset-schema/src/params.ts` as `DSP_CONSTANTS.gate`.

## Things easy to miss

- **The chain is mono.** `kNumChannelsInternal` is 1. Stereo exists only either
  side of the model.
- **Denormals are disabled around the whole block** and the FP environment is
  restored afterwards:
  ```cpp
  std::fenv_t fe_state;
  std::feholdexcept(&fe_state);
  disable_denormals();
  ...
  std::feupdateenv(&fe_state);
  ```
  Skipping this shows up as CPU spikes on decaying tails.
- **The DC blocker sits after the IR**, not before, and before output level.
- **Output level is not just a gain.** It combines with `kOutputMode`
  (Raw / Normalized / Calibrated) and the model's own level metadata. Presets
  must carry the mode, not a resolved gain, or they will not survive a model
  swap.

## Parameter ranges

Transcribed from the plugin's `InitParams`, kept exact so a Gootar preset
round-trips without silent clamping. Source of truth:
[`packages/preset-schema/src/params.ts`](../packages/preset-schema/src/params.ts).

| Parameter | Range | Default |
|---|---|---|
| `kInputLevel` | −20 … 20 dB | 0 |
| `kOutputLevel` | −40 … 40 dB | 0 |
| `kToneBass` / `kToneMid` / `kToneTreble` | 0 … 10 | 5 |
| `kNoiseGateThreshold` | −100 … 0 dB | −80 |
| `kInputCalibrationLevel` | −60 … 60 dBu | 12 |
| `kSlim` | 0 … 1 | 0 |
| `kOutputMode` | Raw / Normalized / Calibrated | **Normalized** |
| `kNoiseGateActive` / `kEQActive` / `kIRToggle` | bool | true |

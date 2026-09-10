/**
 * Parameter ranges transcribed from the stock NeuralAmpModelerPlugin.
 *
 * Source of truth: NeuralAmpModeler.cpp, InitParams block (verified against
 * sdatkinson/NeuralAmpModelerPlugin @ main).
 *
 *   GetParam(kInputLevel)          ->InitGain("Input",     0.0,  -20.0,  20.0, 0.1);
 *   GetParam(kToneBass)            ->InitDouble("Bass",    5.0,    0.0,  10.0, 0.1);
 *   GetParam(kToneMid)             ->InitDouble("Middle",  5.0,    0.0,  10.0, 0.1);
 *   GetParam(kToneTreble)          ->InitDouble("Treble",  5.0,    0.0,  10.0, 0.1);
 *   GetParam(kOutputLevel)         ->InitGain("Output",    0.0,  -40.0,  40.0, 0.1);
 *   GetParam(kNoiseGateThreshold)  ->InitGain("Threshold",-80.0, -100.0,  0.0, 0.1);
 *   GetParam(kNoiseGateActive)     ->InitBool(true);
 *   GetParam(kEQActive)            ->InitBool(true);
 *   GetParam(kIRToggle)            ->InitBool(true);
 *   GetParam(kOutputMode)          ->InitEnum("OutputMode", 1, {"Raw","Normalized","Calibrated"});
 *   GetParam(kInputCalibrationLevel)->InitDouble(12.0, -60.0, 60.0, 0.1, "dBu");
 *   GetParam(kSlim)                ->InitDouble("Slim",    0.0,    0.0,   1.0, 0.01);
 *
 * Keeping these exact means a Gootar preset round-trips to the stock plugin
 * without silent clamping.
 */

export const PARAM_RANGES = {
  inputLevelDb: { min: -20, max: 20, step: 0.1, default: 0 },
  outputLevelDb: { min: -40, max: 40, step: 0.1, default: 0 },
  tone: { min: 0, max: 10, step: 0.1, default: 5 },
  gateThresholdDb: { min: -100, max: 0, step: 0.1, default: -80 },
  inputCalibrationLevelDbu: { min: -60, max: 60, step: 0.1, default: 12 },
  slim: { min: 0, max: 1, step: 0.01, default: 0 },
} as const;

/** kOutputMode enum order matters — index 1 ("Normalized") is the plugin default. */
export const OUTPUT_MODES = ["raw", "normalized", "calibrated"] as const;
export type OutputMode = (typeof OUTPUT_MODES)[number];
export const DEFAULT_OUTPUT_MODE: OutputMode = "normalized";

/**
 * Fixed DSP constants baked into the stock plugin. Not user-facing params,
 * but the native player must match them or presets will not sound the same.
 */
export const DSP_CONSTANTS = {
  /** kDCBlockerFrequency — 5 Hz high-pass, applied after IR, before output gain. */
  dcBlockerHz: 5.0,
  /** Noise gate trigger params, hard-coded in ProcessBlock. */
  gate: {
    time: 0.01,
    ratio: 0.1,
    openTime: 0.005,
    holdTime: 0.01,
    closeTime: 0.05,
  },
  /** The plugin collapses input to mono before the model. */
  internalChannels: 1,
} as const;

/**
 * NAM models are trained at 48 kHz. NeuralAudio compensates by scaling WaveNet
 * dilations at load time, but ONLY when the host rate is an even multiple of
 * the model rate — 44.1 kHz is not, so it cannot be corrected. Run the device
 * at 48 k (or an even multiple) and the question disappears.
 */
export const NATIVE_MODEL_SAMPLE_RATE = 48000;

"use client";

import { NamRig } from "./nam-rig";

/**
 * The Gootar signal chain in the browser.
 *
 * Mirrors the native chain (docs/SIGNAL-CHAIN.md) so a preset dialled in here
 * sounds like the same preset in the player:
 *
 *   input gain -> model -> tone stack -> IR -> DC blocker -> output gain
 *
 * TWO DELIBERATE DIFFERENCES FROM THE NATIVE CHAIN
 *
 * 1. No noise gate. Web Audio has no gate node, and the native gate is a
 *    stateful two-part design that would need its own AudioWorklet. You are
 *    auditioning a clean pre-recorded DI here, not a live pickup with hum, so
 *    it would gate nothing. The native player has the real one.
 *
 * 2. The DC blocker is a 2-pole biquad rather than the native 1-pole filter.
 *    At 5 Hz the difference is inaudible; both simply remove DC.
 *
 * Everything else is an exact match, including the tone stack: Web Audio's
 * "peaking" biquad uses the same Audio EQ Cookbook formulas that
 * AudioDSPTools' Peaking filter does.
 */

export interface ChainParams {
  inputLevelDb: number;
  toneStackEnabled: boolean;
  bass: number;
  mid: number;
  treble: number;
  irEnabled: boolean;
  outputLevelDb: number;
  outputMode: "raw" | "normalized" | "calibrated";
}

export const DEFAULT_CHAIN_PARAMS: ChainParams = {
  inputLevelDb: 0,
  toneStackEnabled: true,
  bass: 5,
  mid: 5,
  treble: 5,
  irEnabled: true,
  outputLevelDb: 0,
  outputMode: "normalized",
};

const dbToGain = (db: number) => Math.pow(10, db / 20);

/** Ramp rather than jump, or every knob move clicks. */
const setSmooth = (param: AudioParam, value: number, ctx: BaseAudioContext) => {
  const now = ctx.currentTime;
  param.cancelScheduledValues(now);
  param.setValueAtTime(param.value, now);
  param.linearRampToValueAtTime(value, now + 0.02);
};

export class GootarChain {
  readonly input: GainNode;
  private readonly toneBass: BiquadFilterNode;
  private readonly toneMid: BiquadFilterNode;
  private readonly toneTreble: BiquadFilterNode;
  private readonly convolver: ConvolverNode;
  private readonly irWet: GainNode;
  private readonly irDry: GainNode;
  private readonly dcBlocker: BiquadFilterNode;
  private readonly output: GainNode;

  private params: ChainParams = { ...DEFAULT_CHAIN_PARAMS };
  private hasIR = false;

  private constructor(
    readonly context: AudioContext,
    readonly rig: NamRig,
  ) {
    this.input = context.createGain();

    // Tone stack: three peaking bells in series, matching the stock plugin's
    // frequencies, Q values and gain scalings exactly.
    this.toneBass = context.createBiquadFilter();
    this.toneBass.type = "peaking";
    this.toneBass.frequency.value = 150;
    this.toneBass.Q.value = 0.707;

    this.toneMid = context.createBiquadFilter();
    this.toneMid.type = "peaking";
    this.toneMid.frequency.value = 425;

    this.toneTreble = context.createBiquadFilter();
    this.toneTreble.type = "peaking";
    this.toneTreble.frequency.value = 1800;
    this.toneTreble.Q.value = 0.707;

    this.convolver = context.createConvolver();
    // NAM applies the IR as-is. Convolver normalisation would silently rescale
    // it and make every IR sound like a different level than in the plugin.
    this.convolver.normalize = false;

    this.irWet = context.createGain();
    this.irDry = context.createGain();

    this.dcBlocker = context.createBiquadFilter();
    this.dcBlocker.type = "highpass";
    this.dcBlocker.frequency.value = 5;
    this.dcBlocker.Q.value = 0.707;

    this.output = context.createGain();

    // input -> models -> tone stack -> [IR wet | dry] -> DC blocker -> output
    //
    // The tone stack is always in the path. Disabling it sets all three bells
    // to 0 dB, which for a peaking biquad is mathematically transparent
    // (b == a, so H(z) = 1) - no bypass branch, and therefore no chance of
    // summing two copies of the signal.
    this.input.connect(rig.input);

    rig.output.connect(this.toneBass);
    this.toneBass.connect(this.toneMid);
    this.toneMid.connect(this.toneTreble);

    this.toneTreble.connect(this.convolver);
    this.convolver.connect(this.irWet);
    this.irWet.connect(this.dcBlocker);

    this.toneTreble.connect(this.irDry);
    this.irDry.connect(this.dcBlocker);

    this.dcBlocker.connect(this.output);

    this.applyParams();
  }

  static async create(context: AudioContext): Promise<GootarChain> {
    const rig = await NamRig.create(context);
    return new GootarChain(context, rig);
  }

  connect(destination: AudioNode): void {
    this.output.connect(destination);
  }

  setParams(next: Partial<ChainParams>): void {
    this.params = { ...this.params, ...next };
    this.applyParams();
  }

  getParams(): ChainParams {
    return { ...this.params };
  }

  async setIR(buffer: AudioBuffer | null): Promise<void> {
    this.convolver.buffer = buffer;
    this.hasIR = buffer !== null;
    this.applyParams();
  }

  private applyParams(): void {
    const p = this.params;
    const ctx = this.context;

    setSmooth(this.input.gain, dbToGain(p.inputLevelDb), ctx);

    // Gain scalings from BasicNamToneStack::SetParam. Disabled means flat,
    // which for peaking bells is genuine transparency rather than a bypass.
    const on = p.toneStackEnabled;
    const midGain = on ? 3.0 * (p.mid - 5.0) : 0;
    setSmooth(this.toneBass.gain, on ? 4.0 * (p.bass - 5.0) : 0, ctx);
    setSmooth(this.toneMid.gain, midGain, ctx);
    setSmooth(this.toneTreble.gain, on ? 2.0 * (p.treble - 5.0) : 0, ctx);
    // Wider bell when boosting, so a mid bump sounds less honky. Upstream's
    // choice, kept so the two halves agree.
    this.toneMid.Q.value = midGain < 0 ? 1.5 : 0.7;

    const irOn = p.irEnabled && this.hasIR;
    setSmooth(this.irWet.gain, irOn ? 1 : 0, ctx);
    setSmooth(this.irDry.gain, irOn ? 0 : 1, ctx);

    let outputDb = p.outputLevelDb;
    if (p.outputMode !== "raw") {
      // NeuralAudio's GetRecommendedOutputDBAdjustment(): -18 - loudness.
      const info = this.rig.activeInfo;
      if (info?.hasLoudness) outputDb += -18 - info.loudness;
    }
    setSmooth(this.output.gain, dbToGain(outputDb), ctx);
  }

  /** Re-apply levels after a model switch, since normalisation depends on it. */
  refreshForActiveModel(): void {
    this.applyParams();
  }

  async dispose(): Promise<void> {
    await this.rig.dispose();
    this.output.disconnect();
  }
}

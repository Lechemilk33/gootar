"use client";

import {
  NamEngine,
  type NamNode,
  type NamModelInfo,
} from "neural-amp-modeler-wasm/engine";

/**
 * Gapless A/B over pre-loaded NAM models.
 *
 * The important constraint, from the engine's own docs: NamNode.loadModel()
 * parses on the audio thread and "rendering through this node pauses for the
 * duration (typically 100-300 ms)". So loading a model while you are listening
 * is audible, full stop.
 *
 * The way around it is to never load during playback. Each model gets its own
 * node, loaded ahead of time, all fed from the same source and summed through
 * per-slot gains. Switching is then a gain ramp between two already-running
 * nodes — sample-accurate and inaudible.
 *
 * This is the same shape the native player needs (pre-load on a worker thread,
 * hand over atomically); doing it here first means the web half proves the
 * interaction design before any C++ exists.
 */

/** Short enough to feel instant, long enough not to click. */
const DEFAULT_CROSSFADE_MS = 15;

export interface RigSlot {
  key: string;
  node: NamNode;
  gain: GainNode;
  info: NamModelInfo;
}

export class NamRig {
  private readonly slots = new Map<string, RigSlot>();
  private activeKey: string | null = null;

  /** Everything a caller should connect a source into. */
  readonly input: GainNode;
  /** Post-model sum; connect to a destination or further processing. */
  readonly output: GainNode;

  private constructor(
    readonly context: AudioContext,
    private readonly engine: NamEngine,
  ) {
    this.input = context.createGain();
    this.output = context.createGain();
  }

  static async create(context: AudioContext): Promise<NamRig> {
    // Assets are served as static files from public/nam/ — see
    // scripts/copy-nam-assets.mjs for why we don't let the bundler handle them.
    const engine = await NamEngine.attach(context, { assetBaseUrl: "/nam/" });
    return new NamRig(context, engine);
  }

  get loadedKeys(): string[] {
    return [...this.slots.keys()];
  }

  get active(): string | null {
    return this.activeKey;
  }

  /**
   * Parse and load a model into its own node, silent and ready.
   *
   * Call this for every model you might want to hear *before* you start
   * playing. Re-preloading an existing key is a no-op.
   */
  async preload(key: string, namFileContents: string): Promise<RigSlot> {
    const existing = this.slots.get(key);
    if (existing) return existing;

    const node = await this.engine.createNode();
    const info = await node.loadModel(namFileContents);

    const gain = this.context.createGain();
    // Silent until switched to, so preloading never changes what you hear.
    gain.gain.value = 0;

    this.input.connect(node);
    node.connect(gain);
    gain.connect(this.output);

    const slot: RigSlot = { key, node, gain, info };
    this.slots.set(key, slot);
    return slot;
  }

  /**
   * Crossfade to a preloaded model. Throws if the key was never preloaded —
   * silently loading here would reintroduce the 100-300 ms stall this class
   * exists to avoid.
   */
  switchTo(key: string, crossfadeMs: number = DEFAULT_CROSSFADE_MS): void {
    const target = this.slots.get(key);
    if (!target) {
      throw new Error(
        `NamRig: "${key}" is not preloaded. Call preload() before switchTo().`,
      );
    }
    if (this.activeKey === key) return;

    const now = this.context.currentTime;
    const end = now + crossfadeMs / 1000;

    for (const slot of this.slots.values()) {
      const to = slot.key === key ? 1 : 0;
      // Anchor at the current value first, or a ramp from an untouched
      // AudioParam jumps instead of sliding.
      slot.gain.gain.cancelScheduledValues(now);
      slot.gain.gain.setValueAtTime(slot.gain.gain.value, now);
      slot.gain.gain.linearRampToValueAtTime(to, end);
    }

    this.activeKey = key;
  }

  /** Mute every slot without unloading anything (for a bypass/dry compare). */
  muteAll(crossfadeMs: number = DEFAULT_CROSSFADE_MS): void {
    const now = this.context.currentTime;
    const end = now + crossfadeMs / 1000;
    for (const slot of this.slots.values()) {
      slot.gain.gain.cancelScheduledValues(now);
      slot.gain.gain.setValueAtTime(slot.gain.gain.value, now);
      slot.gain.gain.linearRampToValueAtTime(0, end);
    }
    this.activeKey = null;
  }

  async unload(key: string): Promise<void> {
    const slot = this.slots.get(key);
    if (!slot) return;
    this.slots.delete(key);
    if (this.activeKey === key) this.activeKey = null;
    try {
      slot.gain.disconnect();
      this.input.disconnect(slot.node);
    } catch {
      // Already torn down; disconnect() throws rather than no-oping.
    }
    await slot.node.dispose();
  }

  async dispose(): Promise<void> {
    await Promise.all([...this.slots.keys()].map((k) => this.unload(k)));
    this.input.disconnect();
    this.output.disconnect();
  }
}

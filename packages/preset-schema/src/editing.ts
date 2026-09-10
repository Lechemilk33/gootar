import { PresetSchema, type Preset } from "./preset.ts";
import { OUTPUT_MODES, type OutputMode } from "./params.ts";
import type { AssetRef } from "./assets.ts";

/**
 * The amp controls an editor is expected to manage.
 *
 * Deliberately excludes the gate and the pedalboard: the librarian has neither,
 * and an editor must not be able to silently drop what it cannot show.
 */
export interface AmpControls {
  inputLevelDb: number;
  toneStackEnabled: boolean;
  bass: number;
  mid: number;
  treble: number;
  irEnabled: boolean;
  outputLevelDb: number;
  outputMode: OutputMode;
}

export function ampControlsFromPreset(p: Preset): AmpControls {
  return {
    inputLevelDb: p.input.levelDb,
    toneStackEnabled: p.toneStack.enabled,
    bass: p.toneStack.bass,
    mid: p.toneStack.mid,
    treble: p.toneStack.treble,
    irEnabled: p.ir.enabled,
    outputLevelDb: p.output.levelDb,
    outputMode: p.output.mode,
  };
}

/**
 * Apply an editor's changes to a preset, keeping everything the editor does
 * not manage.
 *
 * This exists because "keep what you did not edit" is a rule about the FORMAT,
 * not about any one UI, and putting it in the UI is how it gets forgotten. It
 * already had: the gate was preserved by hand, then the pedalboard was added
 * and silently dropped on every save from the librarian - a preset would come
 * back with the amp intact and the pedals gone.
 *
 * Anything added to the format from here on is preserved by default, because
 * the base preset is spread first and only named fields are overwritten.
 */
export function applyAmpControls(opts: {
  base: Preset;
  controls: AmpControls;
  name?: string;
  tags?: string[];
  model?: AssetRef;
  ir?: AssetRef | null;
  now?: Date;
}): Preset {
  const { base, controls } = opts;

  return PresetSchema.parse({
    // Everything not named below survives untouched - gate, chain, notes, and
    // whatever the format grows next.
    ...base,
    name: opts.name ?? base.name,
    tags: opts.tags ?? base.tags,
    updatedAt: (opts.now ?? new Date()).toISOString(),

    input: { ...base.input, levelDb: controls.inputLevelDb },

    models: opts.model
      ? [{ ...base.models[0]!, ref: opts.model }, ...base.models.slice(1)]
      : base.models,

    toneStack: {
      enabled: controls.toneStackEnabled,
      bass: controls.bass,
      mid: controls.mid,
      treble: controls.treble,
    },

    ir: {
      enabled: controls.irEnabled,
      ref: opts.ir !== undefined ? opts.ir : base.ir.ref,
    },

    output: {
      levelDb: controls.outputLevelDb,
      mode: OUTPUT_MODES.includes(controls.outputMode)
        ? controls.outputMode
        : base.output.mode,
    },
  });
}

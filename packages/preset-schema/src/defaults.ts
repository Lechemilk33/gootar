import { PresetSchema, type Preset } from "./preset.ts";
import { DEFAULT_OUTPUT_MODE, NATIVE_MODEL_SAMPLE_RATE, PARAM_RANGES as P } from "./params.ts";
import type { AssetRef } from "./assets.ts";

/**
 * A preset with every knob at the stock plugin's default, holding one model.
 * Parsing through the schema guarantees defaults and validation agree.
 */
export function createPreset(opts: {
  name: string;
  model: AssetRef;
  ir?: AssetRef | null;
  id?: string;
  author?: string;
  now?: Date;
}): Preset {
  const now = (opts.now ?? new Date()).toISOString();
  return PresetSchema.parse({
    schemaVersion: 1,
    id: opts.id ?? crypto.randomUUID(),
    name: opts.name,
    tags: [],
    notes: "",
    ...(opts.author ? { author: opts.author } : {}),
    createdAt: now,
    updatedAt: now,
    sampleRate: NATIVE_MODEL_SAMPLE_RATE,
    input: {
      levelDb: P.inputLevelDb.default,
      calibrate: false,
      calibrationLevelDbu: P.inputCalibrationLevelDbu.default,
    },
    gate: { enabled: true, thresholdDb: P.gateThresholdDb.default },
    models: [
      {
        slotId: "slot-1",
        enabled: true,
        ref: opts.model,
        slim: P.slim.default,
      },
    ],
    toneStack: {
      enabled: true,
      bass: P.tone.default,
      mid: P.tone.default,
      treble: P.tone.default,
    },
    ir: { enabled: true, ref: opts.ir ?? null },
    output: { levelDb: P.outputLevelDb.default, mode: DEFAULT_OUTPUT_MODE },
  });
}

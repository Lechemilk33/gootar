import { z } from "zod";
import { AssetRefSchema, NamModelMetaSchema } from "./assets.ts";
import {
  DEFAULT_OUTPUT_MODE,
  NATIVE_MODEL_SAMPLE_RATE,
  OUTPUT_MODES,
  PARAM_RANGES as P,
} from "./params.ts";

const db = (r: { min: number; max: number; default: number }) =>
  z.number().min(r.min).max(r.max).default(r.default);

/**
 * One model in the chain. Milestones 1-6 use exactly one; milestone 7
 * (pedal -> amp) appends more. Modelling this as an array from day one is the
 * difference between "add a slot" and "redesign the preset format" later.
 */
export const ModelSlotSchema = z.object({
  /** Stable within the preset, so a UI can keep per-slot state across reorders. */
  slotId: z.string().min(1),
  enabled: z.boolean().default(true),
  ref: AssetRefSchema,
  /**
   * kSlim, 0..1. The stock plugin exposes this globally; per-slot is strictly
   * more expressive and collapses to the plugin's behaviour when all slots
   * share a value. Ignored by non-slimmable models.
   */
  slim: db(P.slim),
  /** Cached from the .nam file. Advisory only — never trust it over the file. */
  meta: NamModelMetaSchema.optional(),
});
export type ModelSlot = z.infer<typeof ModelSlotSchema>;

export const InputSectionSchema = z.object({
  levelDb: db(P.inputLevelDb),
  /** kCalibrateInput */
  calibrate: z.boolean().default(false),
  /** kInputCalibrationLevel, dBu */
  calibrationLevelDbu: db(P.inputCalibrationLevelDbu),
});

export const GateSectionSchema = z.object({
  /** kNoiseGateActive */
  enabled: z.boolean().default(true),
  /** kNoiseGateThreshold */
  thresholdDb: db(P.gateThresholdDb),
});

export const ToneStackSectionSchema = z.object({
  /** kEQActive */
  enabled: z.boolean().default(true),
  bass: db(P.tone),
  mid: db(P.tone),
  treble: db(P.tone),
});

export const IrSectionSchema = z.object({
  /** kIRToggle */
  enabled: z.boolean().default(true),
  /** null = no IR loaded; the toggle can still be on. */
  ref: AssetRefSchema.nullable().default(null),
});

export const OutputSectionSchema = z.object({
  levelDb: db(P.outputLevelDb),
  /** kOutputMode */
  mode: z.enum(OUTPUT_MODES).default(DEFAULT_OUTPUT_MODE),
});

/**
 * One stage of the signal chain, as stored in a preset.
 *
 * The chain is what makes a preset a *rig* rather than just an amp plus knob
 * positions: which pedals, in what order, set how. Without it, saving a preset
 * would quietly lose the board you just built, which is worse than not having
 * presets at all.
 *
 * Fixed stages (input level, gate, tone stack, cab, DC blocker, output) appear
 * here for their POSITION only. Their values live in the sections below, where
 * they can also be host-automated. Storing them twice would let the two copies
 * disagree.
 */
export const ChainBlockSchema = z.object({
  type: z.enum([
    "gain", "gate", "model", "toneStack", "ir", "dcBlocker",
    "drive", "compressor", "delay", "reverb",
  ]),
  /** Stable across reorders, so a block keeps its settings when it moves. */
  id: z.string().min(1).max(64),
  enabled: z.boolean().default(true),
  /** Pedal knobs. Free-form so a new pedal needs no schema change. */
  params: z.record(z.string(), z.number()).default({}),
  /** Which capture or cab this slot holds, for model and ir blocks. */
  ref: AssetRefSchema.nullable().default(null),
  slim: db(P.slim).optional(),
});
export type ChainBlock = z.infer<typeof ChainBlockSchema>;

/**
 * A Gootar preset.
 *
 * Section order in this object mirrors the real signal flow, which is taken
 * from NeuralAmpModeler::ProcessBlock — see docs/SIGNAL-CHAIN.md. The gate is
 * split: it triggers on the clean input and applies its gain after the model.
 */
export const PresetSchema = z.object({
  /**
   * 2 added the chain. Version 1 files still load: they describe the stock
   * chain, so a reader that finds no `chain` fills in the standard one.
   */
  schemaVersion: z.literal(2),
  id: z.uuid(),
  name: z.string().min(1).max(200),

  tags: z.array(z.string().min(1).max(64)).default([]),
  notes: z.string().max(4000).default(""),
  author: z.string().max(200).optional(),
  createdAt: z.iso.datetime(),
  updatedAt: z.iso.datetime(),

  /**
   * Rate this preset was dialled in at. Models are trained at 48 k; anything
   * that is not an even multiple of the model rate cannot be compensated.
   */
  sampleRate: z.number().positive().default(NATIVE_MODEL_SAMPLE_RATE),

  input: InputSectionSchema,
  gate: GateSectionSchema,
  models: z.array(ModelSlotSchema).min(1).max(8),
  toneStack: ToneStackSectionSchema,
  ir: IrSectionSchema,
  output: OutputSectionSchema,

  /**
   * The board, in signal order. Empty means "the stock chain", which is what a
   * version 1 preset and a plain amp-only rig both are.
   */
  chain: z.array(ChainBlockSchema).max(32).default([]),
});
export type Preset = z.infer<typeof PresetSchema>;

/**
 * A library index record: one .nam or .wav the user owns, plus the organising
 * metadata the stock plugin gives you nowhere to put.
 */
export const LibraryEntrySchema = z.object({
  schemaVersion: z.literal(1),
  ref: AssetRefSchema,
  kind: z.enum(["model", "ir"]),
  /** User-assigned. The entire point of the librarian. */
  tags: z.array(z.string().min(1).max(64)).default([]),
  favorite: z.boolean().default(false),
  rating: z.number().int().min(0).max(5).default(0),
  notes: z.string().max(4000).default(""),
  meta: NamModelMetaSchema.optional(),
  addedAt: z.iso.datetime(),
  lastAuditionedAt: z.iso.datetime().optional(),
});
export type LibraryEntry = z.infer<typeof LibraryEntrySchema>;

/** What a share-by-link URL actually carries. */
export const RigBundleSchema = z.object({
  schemaVersion: z.literal(2),
  name: z.string().min(1).max(200),
  description: z.string().max(4000).default(""),
  presets: z.array(PresetSchema).min(1).max(128),
  createdAt: z.iso.datetime(),
});
export type RigBundle = z.infer<typeof RigBundleSchema>;

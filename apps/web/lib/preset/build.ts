"use client";

import {
  PresetSchema,
  createPreset,
  type AssetRef,
  type LibraryEntry,
  type Preset,
} from "@gootar/preset-schema";

import type { ChainParams } from "../audio/chain";

/**
 * Presets are the contract between the two halves, so the mapping between what
 * the browser UI holds and what gets written to JSON lives in one place.
 *
 * The browser has no noise gate (see lib/audio/chain.ts), so gate settings are
 * carried through untouched rather than dropped: a preset saved here and opened
 * in the player must not silently lose its gate.
 */

export function chainParamsFromPreset(p: Preset): ChainParams {
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

export function buildPreset(opts: {
  name: string;
  params: ChainParams;
  model: AssetRef;
  ir?: AssetRef | null;
  tags?: string[];
  /** Preserve gate settings from a preset that was opened, if there was one. */
  basedOn?: Preset | null;
}): Preset {
  const base = createPreset({ name: opts.name, model: opts.model, ir: opts.ir ?? null });

  return PresetSchema.parse({
    ...base,
    tags: opts.tags ?? base.tags,
    input: {
      ...base.input,
      levelDb: opts.params.inputLevelDb,
    },
    // The browser cannot audition the gate, so whatever the source preset said
    // survives the round trip untouched.
    gate: opts.basedOn?.gate ?? base.gate,
    toneStack: {
      enabled: opts.params.toneStackEnabled,
      bass: opts.params.bass,
      mid: opts.params.mid,
      treble: opts.params.treble,
    },
    ir: {
      enabled: opts.params.irEnabled,
      ref: opts.ir ?? null,
    },
    output: {
      levelDb: opts.params.outputLevelDb,
      mode: opts.params.outputMode,
    },
  });
}

export function refFromEntry(entry: LibraryEntry): AssetRef {
  return entry.ref;
}

export function downloadPreset(preset: Preset): void {
  const blob = new Blob([JSON.stringify(preset, null, 2)], { type: "application/json" });
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = `${preset.name.replace(/[^\w.-]+/g, "_") || "preset"}.json`;
  document.body.appendChild(a);
  a.click();
  a.remove();
  // Revoke on the next tick so the click has definitely been handled.
  setTimeout(() => URL.revokeObjectURL(url), 0);
}

export function parsePresetFile(text: string): { preset?: Preset; error?: string } {
  let raw: unknown;
  try {
    raw = JSON.parse(text);
  } catch {
    return { error: "not valid JSON" };
  }
  const parsed = PresetSchema.safeParse(raw);
  if (!parsed.success) {
    const first = parsed.error.issues[0];
    return {
      error: first ? `${first.path.join(".") || "preset"}: ${first.message}` : "invalid preset",
    };
  }
  return { preset: parsed.data };
}

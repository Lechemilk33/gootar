"use client";

import {
  PresetSchema,
  applyAmpControls,
  ampControlsFromPreset,
  createPreset,
  type AmpControls,
  type AssetRef,
  type LibraryEntry,
  type Preset,
} from "@gootar/preset-schema";

import type { ChainParams } from "../audio/chain";

/**
 * Presets are the contract between the two halves, so the mapping between what
 * this UI holds and what gets written to JSON lives in one place.
 *
 * The librarian edits the amp controls and nothing else. It has no noise gate
 * and no pedalboard, so both are carried through untouched - a preset saved
 * here and opened in the player must come back with its rig intact. That rule
 * is enforced by applyAmpControls in the shared package rather than by hand
 * here, because doing it by hand is how the pedalboard got dropped once
 * already.
 */

export function chainParamsFromPreset(p: Preset): ChainParams {
  return ampControlsFromPreset(p);
}

export function buildPreset(opts: {
  name: string;
  params: ChainParams;
  model: AssetRef;
  ir?: AssetRef | null;
  tags?: string[];
  /** The preset that was opened, if any. Everything unedited comes from here. */
  basedOn?: Preset | null;
}): Preset {
  const base =
    opts.basedOn ??
    createPreset({ name: opts.name, model: opts.model, ir: opts.ir ?? null });

  return applyAmpControls({
    base,
    controls: opts.params as AmpControls,
    name: opts.name,
    tags: opts.tags,
    model: opts.model,
    ir: opts.ir ?? null,
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

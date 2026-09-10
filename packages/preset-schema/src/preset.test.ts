import { test } from "node:test";
import assert from "node:assert/strict";
import { PresetSchema } from "./preset.ts";
import { createPreset } from "./defaults.ts";
import { sha256Hex, collectAssetRefs, assetMatches } from "./identity.ts";
import { PARAM_RANGES } from "./params.ts";
import type { AssetRef } from "./assets.ts";

const modelRef: AssetRef = {
  sha256: "a".repeat(64),
  fileName: "JCM800_Lead.nam",
  sizeBytes: 1_500_000,
  sources: [{ kind: "local", relPath: "marshall/JCM800_Lead.nam" }],
};

test("default preset matches the stock plugin defaults", () => {
  const p = createPreset({ name: "Test", model: modelRef });
  assert.equal(p.input.levelDb, 0);
  assert.equal(p.output.levelDb, 0);
  assert.equal(p.output.mode, "normalized"); // kOutputMode default index 1
  assert.equal(p.gate.thresholdDb, -80);
  assert.equal(p.toneStack.bass, 5);
  assert.equal(p.input.calibrationLevelDbu, 12);
  assert.equal(p.sampleRate, 48000);
  assert.equal(p.models.length, 1);
});

test("out-of-range params are rejected, not clamped", () => {
  const p = createPreset({ name: "Test", model: modelRef });
  const tooHot = { ...p, input: { ...p.input, levelDb: 21 } };
  assert.equal(PresetSchema.safeParse(tooHot).success, false);
  const ok = { ...p, input: { ...p.input, levelDb: PARAM_RANGES.inputLevelDb.max } };
  assert.equal(PresetSchema.safeParse(ok).success, true);
});

test("a preset round-trips through JSON unchanged", () => {
  const p = createPreset({ name: "Round Trip", model: modelRef });
  const back = PresetSchema.parse(JSON.parse(JSON.stringify(p)));
  assert.deepEqual(back, p);
});

test("model refs require a real sha256", () => {
  const bad = { ...modelRef, sha256: "not-a-hash" };
  const p = createPreset({ name: "Test", model: modelRef });
  const broken = { ...p, models: [{ ...p.models[0]!, ref: bad }] };
  assert.equal(PresetSchema.safeParse(broken).success, false);
});

test("multi-model chains are valid without a schema change", () => {
  const p = createPreset({ name: "Pedal into amp", model: modelRef });
  const chained = {
    ...p,
    models: [
      p.models[0]!,
      { ...p.models[0]!, slotId: "slot-2", ref: { ...modelRef, sha256: "b".repeat(64) } },
    ],
  };
  const parsed = PresetSchema.parse(chained);
  assert.equal(parsed.models.length, 2);
});

test("sha256Hex agrees with the known digest of an empty input", async () => {
  const empty = await sha256Hex(new Uint8Array());
  assert.equal(empty, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
});

test("sha256Hex hashes only the view, not its backing buffer", async () => {
  const backing = new Uint8Array([1, 2, 3, 4, 5, 6, 7, 8]);
  const view = backing.subarray(0, 4);
  const standalone = new Uint8Array([1, 2, 3, 4]);
  assert.equal(await sha256Hex(view), await sha256Hex(standalone));
});

test("collectAssetRefs de-duplicates and includes the IR", () => {
  const p = createPreset({
    name: "Test",
    model: modelRef,
    ir: { sha256: "c".repeat(64), fileName: "greenback.wav", sources: [] },
  });
  const dup = { ...p, models: [p.models[0]!, { ...p.models[0]!, slotId: "slot-2" }] };
  assert.equal(collectAssetRefs(dup).length, 2); // one model + one IR
});

test("assetMatches is hash-based and case-insensitive on input", () => {
  assert.equal(assetMatches(modelRef, "A".repeat(64)), true);
  assert.equal(assetMatches(modelRef, "b".repeat(64)), false);
});

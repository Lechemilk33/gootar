/**
 * Emits JSON Schema from the zod definitions so the native (C++) side has a
 * machine-readable spec of the same format, without a second hand-written
 * source of truth that can drift.
 */
import { writeFileSync, mkdirSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { z } from "zod";
import { PresetSchema, LibraryEntrySchema, RigBundleSchema } from "../src/preset.ts";

const here = dirname(fileURLToPath(import.meta.url));
const outDir = join(here, "..", "schema");
mkdirSync(outDir, { recursive: true });

const bundle = {
  $schema: "https://json-schema.org/draft/2020-12/schema",
  $id: "https://gootar.local/schema/gootar-preset.schema.json",
  title: "Gootar preset format",
  description:
    "Preset, library entry and rig bundle formats shared by the Gootar web librarian and native player.",
  $defs: {
    Preset: z.toJSONSchema(PresetSchema, { io: "output" }),
    LibraryEntry: z.toJSONSchema(LibraryEntrySchema, { io: "output" }),
    RigBundle: z.toJSONSchema(RigBundleSchema, { io: "output" }),
  },
  oneOf: [{ $ref: "#/$defs/Preset" }],
};

const out = join(outDir, "gootar-preset.schema.json");
writeFileSync(out, JSON.stringify(bundle, null, 2) + "\n");
console.log(`wrote ${out}`);

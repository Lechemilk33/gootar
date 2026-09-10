import { z } from "zod";

/**
 * How a preset points at a .nam or .wav file.
 *
 * Deliberately NOT a filesystem path. The whole point of a shared format is
 * that the same preset resolves on the web librarian (browser, File System
 * Access handle or uploaded blob) and in the native player (a scanned folder
 * on a different machine, different drive letter, different filename).
 *
 * The primary key is the SHA-256 of the file bytes. Everything else is a hint
 * used to help a resolver find the file, or to show something useful in the UI
 * when it cannot.
 */
export const AssetSourceSchema = z.discriminatedUnion("kind", [
  /** Found by scanning a local folder. relPath is relative to the library root. */
  z.object({
    kind: z.literal("local"),
    relPath: z.string().min(1),
  }),
  /** Published on TONE3000, so any machine can re-fetch it. */
  z.object({
    kind: z.literal("tone3000"),
    modelId: z.string().min(1),
    url: z.url().optional(),
  }),
  /** Any other fetchable URL. */
  z.object({
    kind: z.literal("url"),
    url: z.url(),
  }),
]);
export type AssetSource = z.infer<typeof AssetSourceSchema>;

export const AssetRefSchema = z.object({
  /** Lowercase hex SHA-256 of the raw file bytes. The identity of the asset. */
  sha256: z
    .string()
    .regex(/^[0-9a-f]{64}$/, "sha256 must be 64 lowercase hex characters"),
  /** Original filename, for display and as a fallback match. */
  fileName: z.string().min(1),
  sizeBytes: z.number().int().nonnegative().optional(),
  /**
   * Where this asset can be found. A preset may list several: the author's
   * local path is useless to you, but the tone3000 entry next to it is not.
   */
  sources: z.array(AssetSourceSchema).default([]),
});
export type AssetRef = z.infer<typeof AssetRefSchema>;

/**
 * Metadata read out of the .nam file itself, cached in the library index so
 * the librarian can search and filter without reopening hundreds of files.
 */
export const NamModelMetaSchema = z.object({
  architecture: z.string().optional(),
  version: z.string().optional(),
  sampleRate: z.number().positive().optional(),
  /** NAM "input_level_dbu" / "output_level_dbu" calibration metadata, if present. */
  inputLevelDbu: z.number().optional(),
  outputLevelDbu: z.number().optional(),
  /** True for A2 / slimmable models, which accept a slim size. */
  slimmable: z.boolean().optional(),
});
export type NamModelMeta = z.infer<typeof NamModelMetaSchema>;

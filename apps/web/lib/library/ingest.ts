import {
  sha256Hex,
  type AssetRef,
  type LibraryEntry,
  type NamModelMeta,
} from "@gootar/preset-schema";

/**
 * Turning a pile of files on disk into library entries.
 *
 * Nothing here uploads anything. A .nam is read, hashed and parsed in the
 * browser; only the resulting metadata (a few hundred bytes) is ever a
 * candidate for syncing. The model bytes stay where they are — which is the
 * only way the same preset can also resolve on the native player later.
 */

/** Shape of the parts of a .nam file we care about. It is just JSON. */
interface RawNamFile {
  version?: unknown;
  architecture?: unknown;
  config?: { [k: string]: unknown } | null;
  metadata?: { [k: string]: unknown } | null;
  weights?: unknown;
}

const num = (v: unknown): number | undefined =>
  typeof v === "number" && Number.isFinite(v) ? v : undefined;
const str = (v: unknown): string | undefined =>
  typeof v === "string" && v.length > 0 ? v : undefined;

/**
 * Pull the searchable metadata out of a .nam without pretending to understand
 * every field. Anything missing stays undefined rather than guessed — a wrong
 * sample rate is worse than an unknown one.
 */
export function readNamMeta(json: unknown): NamModelMeta {
  const nam = (json ?? {}) as RawNamFile;
  const meta = (nam.metadata ?? {}) as Record<string, unknown>;
  const config = (nam.config ?? {}) as Record<string, unknown>;

  return {
    architecture: str(nam.architecture),
    version: str(nam.version),
    sampleRate: num(meta["sample_rate"]) ?? num(config["sample_rate"]),
    inputLevelDbu: num(meta["input_level_dbu"]),
    outputLevelDbu: num(meta["output_level_dbu"]),
    // A2 / slimmable models carry submodel thresholds; their presence is what
    // makes the slim control meaningful.
    slimmable: Array.isArray(config["submodels"])
      ? true
      : undefined,
  };
}

export interface IngestedModel {
  entry: LibraryEntry;
  /** Kept in memory for this session so audition needs no second read. */
  contents: string;
}

/** Hash + parse one .nam file. Throws only if the file is not valid JSON. */
export async function ingestNamFile(
  file: File,
  relPath?: string,
): Promise<IngestedModel> {
  const bytes = new Uint8Array(await file.arrayBuffer());
  const sha256 = await sha256Hex(bytes);
  const contents = new TextDecoder().decode(bytes);

  let meta: NamModelMeta = {};
  try {
    meta = readNamMeta(JSON.parse(contents));
  } catch {
    throw new Error(`${file.name} is not a readable .nam file (invalid JSON)`);
  }

  const ref: AssetRef = {
    sha256,
    fileName: file.name,
    sizeBytes: file.size,
    sources: relPath ? [{ kind: "local", relPath }] : [],
  };

  return {
    contents,
    entry: {
      schemaVersion: 1,
      ref,
      kind: "model",
      tags: [],
      favorite: false,
      rating: 0,
      notes: "",
      meta,
      addedAt: new Date().toISOString(),
    },
  };
}

/**
 * Ingest a selection of files, skipping anything that is not a .nam and
 * reporting per-file failures instead of aborting the whole batch — with
 * hundreds of models, one bad file should not cost you the import.
 */
export async function ingestFiles(files: File[]): Promise<{
  models: IngestedModel[];
  errors: { fileName: string; message: string }[];
}> {
  const models: IngestedModel[] = [];
  const errors: { fileName: string; message: string }[] = [];

  for (const file of files) {
    if (!file.name.toLowerCase().endsWith(".nam")) continue;
    try {
      // webkitRelativePath is set when the user picked a directory.
      const rel = (file as File & { webkitRelativePath?: string })
        .webkitRelativePath;
      models.push(await ingestNamFile(file, rel || undefined));
    } catch (err) {
      errors.push({
        fileName: file.name,
        message: err instanceof Error ? err.message : String(err),
      });
    }
  }

  // De-duplicate by hash: the same model under three different filenames is
  // one model, and this is exactly the mess the librarian exists to clean up.
  const byHash = new Map<string, IngestedModel>();
  for (const m of models) {
    const existing = byHash.get(m.entry.ref.sha256);
    if (!existing) {
      byHash.set(m.entry.ref.sha256, m);
    } else {
      existing.entry.ref.sources.push(...m.entry.ref.sources);
    }
  }

  return { models: [...byHash.values()], errors };
}

import type { AssetRef } from "./assets.ts";

/**
 * SHA-256 of raw file bytes, lowercase hex.
 *
 * Uses WebCrypto, which is present in browsers and in Node 18+, so the web
 * librarian and any Node tooling produce byte-identical ids. The native player
 * must produce the same digest over the same bytes (any standard SHA-256 will).
 */
export async function sha256Hex(bytes: ArrayBuffer | Uint8Array): Promise<string> {
  const buf =
    bytes instanceof Uint8Array
      ? // Copy into a plain ArrayBuffer: a Uint8Array may be a view onto a
        // larger buffer, and digest() would hash the whole thing.
        bytes.slice().buffer
      : bytes;
  const digest = await crypto.subtle.digest("SHA-256", buf as ArrayBuffer);
  return [...new Uint8Array(digest)]
    .map((b) => b.toString(16).padStart(2, "0"))
    .join("");
}

/**
 * Does this candidate file satisfy an AssetRef?
 *
 * Hash is authoritative. Filename is only ever a hint for *locating* a
 * candidate, never for confirming one — the whole reason for hashing is that
 * "Marshall.nam" means nothing across two libraries.
 */
export function assetMatches(ref: AssetRef, candidateSha256: string): boolean {
  return ref.sha256 === candidateSha256.toLowerCase();
}

/**
 * Contract both halves implement: turn an AssetRef into actual bytes.
 *
 *  - Web: look up an IndexedDB record -> FileSystemFileHandle, or fetch a
 *    tone3000/url source.
 *  - Native: look up a hash -> path map built by scanning the library folder.
 *
 * Returning null means "I do not have this asset", which the UI should surface
 * as a missing-model badge rather than an error.
 */
export interface AssetResolver {
  resolve(ref: AssetRef): Promise<Uint8Array | null>;
}

/** Every asset a preset needs, de-duplicated by hash. */
export function collectAssetRefs(preset: {
  models: { ref: AssetRef }[];
  ir: { ref: AssetRef | null };
}): AssetRef[] {
  const seen = new Map<string, AssetRef>();
  for (const slot of preset.models) seen.set(slot.ref.sha256, slot.ref);
  if (preset.ir.ref) seen.set(preset.ir.ref.sha256, preset.ir.ref);
  return [...seen.values()];
}

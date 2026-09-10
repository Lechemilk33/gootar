"use client";

import type { LibraryEntry, Preset } from "@gootar/preset-schema";

/**
 * Local-first storage for the librarian.
 *
 * Everything lives in the browser: model metadata, your tags, your presets.
 * There is no backend, no account and no upload. That is not a limitation —
 * it is what makes the tool useful on day one instead of after auth exists,
 * and the model files themselves have to stay local anyway for the native
 * player to find them.
 */

const DB_NAME = "gootar";
const DB_VERSION = 1;
const STORE_ENTRIES = "entries";
const STORE_PRESETS = "presets";

let dbPromise: Promise<IDBDatabase> | null = null;

function openDb(): Promise<IDBDatabase> {
  if (dbPromise) return dbPromise;

  dbPromise = new Promise((resolve, reject) => {
    const req = indexedDB.open(DB_NAME, DB_VERSION);
    req.onupgradeneeded = () => {
      const db = req.result;
      if (!db.objectStoreNames.contains(STORE_ENTRIES)) {
        // Keyed by content hash: the same capture imported twice is one row.
        db.createObjectStore(STORE_ENTRIES, { keyPath: "ref.sha256" });
      }
      if (!db.objectStoreNames.contains(STORE_PRESETS)) {
        db.createObjectStore(STORE_PRESETS, { keyPath: "id" });
      }
    };
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error ?? new Error("could not open database"));
  });

  return dbPromise;
}

function tx<T>(
  store: string,
  mode: IDBTransactionMode,
  run: (s: IDBObjectStore) => IDBRequest<T>,
): Promise<T> {
  return openDb().then(
    (db) =>
      new Promise<T>((resolve, reject) => {
        const t = db.transaction(store, mode);
        const req = run(t.objectStore(store));
        req.onsuccess = () => resolve(req.result);
        req.onerror = () => reject(req.error ?? new Error("database request failed"));
      }),
  );
}

// --- library entries -------------------------------------------------------

export async function getAllEntries(): Promise<LibraryEntry[]> {
  return tx<LibraryEntry[]>(STORE_ENTRIES, "readonly", (s) => s.getAll());
}

export async function putEntry(entry: LibraryEntry): Promise<void> {
  await tx(STORE_ENTRIES, "readwrite", (s) => s.put(entry));
}

/**
 * Merge freshly imported entries with what is already stored, keeping any
 * tags, rating and notes already attached to a hash. Re-importing a folder
 * must never wipe the organising work that is the whole point of the app.
 */
export async function mergeEntries(incoming: LibraryEntry[]): Promise<LibraryEntry[]> {
  const existing = new Map((await getAllEntries()).map((e) => [e.ref.sha256, e]));

  const merged = incoming.map((entry) => {
    const prev = existing.get(entry.ref.sha256);
    if (!prev) return entry;
    return {
      ...entry,
      tags: prev.tags,
      favorite: prev.favorite,
      rating: prev.rating,
      notes: prev.notes,
      addedAt: prev.addedAt,
      lastAuditionedAt: prev.lastAuditionedAt,
    };
  });

  const db = await openDb();
  await new Promise<void>((resolve, reject) => {
    const t = db.transaction(STORE_ENTRIES, "readwrite");
    const store = t.objectStore(STORE_ENTRIES);
    for (const entry of merged) store.put(entry);
    t.oncomplete = () => resolve();
    t.onerror = () => reject(t.error ?? new Error("could not save entries"));
  });

  for (const [hash, entry] of existing)
    if (!merged.some((m) => m.ref.sha256 === hash)) merged.push(entry);

  return merged;
}

export async function deleteEntry(sha256: string): Promise<void> {
  await tx(STORE_ENTRIES, "readwrite", (s) => s.delete(sha256));
}

// --- presets ---------------------------------------------------------------

export async function getAllPresets(): Promise<Preset[]> {
  return tx<Preset[]>(STORE_PRESETS, "readonly", (s) => s.getAll());
}

export async function putPreset(preset: Preset): Promise<void> {
  await tx(STORE_PRESETS, "readwrite", (s) => s.put(preset));
}

export async function deletePreset(id: string): Promise<void> {
  await tx(STORE_PRESETS, "readwrite", (s) => s.delete(id));
}

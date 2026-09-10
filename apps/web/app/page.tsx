"use client";

import { useCallback, useEffect, useMemo, useRef, useState } from "react";

import { NATIVE_MODEL_SAMPLE_RATE, type LibraryEntry, type Preset } from "@gootar/preset-schema";

import { GootarChain, DEFAULT_CHAIN_PARAMS, type ChainParams } from "@/lib/audio/chain";
import { ingestFiles, type IngestedModel } from "@/lib/library/ingest";
import { getAllEntries, mergeEntries, putEntry } from "@/lib/library/db";
import {
  buildPreset,
  chainParamsFromPreset,
  downloadPreset,
  parsePresetFile,
} from "@/lib/preset/build";
import { Knob } from "@/components/Knob";
import { TagInput } from "@/components/TagInput";

type Status = { kind: "idle" | "busy" | "ok" | "err"; message: string };

export default function LibrarianPage() {
  const [entries, setEntries] = useState<LibraryEntry[]>([]);
  const [contents, setContents] = useState<Map<string, string>>(new Map());
  const [preloaded, setPreloaded] = useState<Set<string>>(new Set());
  const [activeHash, setActiveHash] = useState<string | null>(null);
  const [selectedHash, setSelectedHash] = useState<string | null>(null);

  const [search, setSearch] = useState("");
  const [tagFilter, setTagFilter] = useState<string | null>(null);
  const [favOnly, setFavOnly] = useState(false);

  const [params, setParams] = useState<ChainParams>(DEFAULT_CHAIN_PARAMS);
  const [basedOn, setBasedOn] = useState<Preset | null>(null);
  const [presetName, setPresetName] = useState("New rig");

  const [playing, setPlaying] = useState(false);
  const [diName, setDiName] = useState<string | null>(null);
  const [irName, setIrName] = useState<string | null>(null);
  const [status, setStatus] = useState<Status>({ kind: "idle", message: "" });

  const chainRef = useRef<GootarChain | null>(null);
  const diBufferRef = useRef<AudioBuffer | null>(null);
  const sourceRef = useRef<AudioBufferSourceNode | null>(null);
  const modelInputRef = useRef<HTMLInputElement>(null);
  const diInputRef = useRef<HTMLInputElement>(null);
  const irInputRef = useRef<HTMLInputElement>(null);
  const presetInputRef = useRef<HTMLInputElement>(null);

  // Tags and ratings survive reloads; the model bytes never leave the machine.
  useEffect(() => {
    getAllEntries()
      .then((stored) => {
        if (stored.length) setEntries(stored);
      })
      .catch(() => {
        /* first run, or storage blocked — an empty library is fine */
      });
  }, []);

  useEffect(() => {
    return () => {
      sourceRef.current?.stop();
      void chainRef.current?.dispose();
    };
  }, []);

  const getChain = useCallback(async (): Promise<GootarChain> => {
    if (chainRef.current) return chainRef.current;
    // Models are trained at 48 k; ask for it so nothing resamples behind us.
    const ctx = new AudioContext({ sampleRate: NATIVE_MODEL_SAMPLE_RATE });
    const chain = await GootarChain.create(ctx);
    chain.connect(ctx.destination);
    chain.setParams(params);
    chainRef.current = chain;
    return chain;
  }, [params]);

  useEffect(() => {
    chainRef.current?.setParams(params);
  }, [params]);

  // --- library -------------------------------------------------------------

  const importModels = useCallback(async (fileList: FileList | null) => {
    if (!fileList?.length) return;
    setStatus({ kind: "busy", message: "Hashing and parsing…" });

    const { models, errors } = await ingestFiles([...fileList]);
    const merged = await mergeEntries(models.map((m: IngestedModel) => m.entry));

    setEntries(merged);
    setContents((prev) => {
      const next = new Map(prev);
      for (const m of models) next.set(m.entry.ref.sha256, m.contents);
      return next;
    });

    setStatus({
      kind: errors.length ? "err" : "ok",
      message: errors.length
        ? `Imported ${models.length}; skipped ${errors.length} (${errors[0]?.message})`
        : `Imported ${models.length}. Library holds ${merged.length}.`,
    });
  }, []);

  const updateEntry = useCallback(async (updated: LibraryEntry) => {
    setEntries((prev) =>
      prev.map((e) => (e.ref.sha256 === updated.ref.sha256 ? updated : e)),
    );
    await putEntry(updated);
  }, []);

  const allTags = useMemo(() => {
    const counts = new Map<string, number>();
    for (const e of entries)
      for (const t of e.tags) counts.set(t, (counts.get(t) ?? 0) + 1);
    return [...counts.entries()].sort((a, b) => b[1] - a[1] || a[0].localeCompare(b[0]));
  }, [entries]);

  const visible = useMemo(() => {
    const q = search.trim().toLowerCase();
    return entries.filter((e) => {
      if (favOnly && !e.favorite) return false;
      if (tagFilter && !e.tags.includes(tagFilter)) return false;
      if (!q) return true;
      return (
        e.ref.fileName.toLowerCase().includes(q) ||
        e.tags.some((t) => t.includes(q)) ||
        e.notes.toLowerCase().includes(q)
      );
    });
  }, [entries, search, tagFilter, favOnly]);

  const loadable = useMemo(
    () => visible.filter((e) => contents.has(e.ref.sha256)),
    [visible, contents],
  );

  // --- audio ---------------------------------------------------------------

  const preloadVisible = useCallback(async () => {
    if (!loadable.length) return;
    setStatus({ kind: "busy", message: "Preloading…" });
    const chain = await getChain();

    for (const entry of loadable) {
      const json = contents.get(entry.ref.sha256);
      if (!json) continue;
      await chain.rig.preload(entry.ref.sha256, json);
      setPreloaded(new Set(chain.rig.loadedKeys));
    }
    setStatus({
      kind: "ok",
      message: `${chain.rig.loadedKeys.length} loaded — switching is now gapless.`,
    });
  }, [loadable, contents, getChain]);

  const switchTo = useCallback(
    async (hash: string) => {
      const chain = await getChain();
      setSelectedHash(hash);

      if (!chain.rig.loadedKeys.includes(hash)) {
        const json = contents.get(hash);
        if (!json) {
          setStatus({
            kind: "err",
            message: "That model's file isn't in this session — re-import it to audition.",
          });
          return;
        }
        setStatus({ kind: "busy", message: "Loading (not preloaded — expect a gap)…" });
        await chain.rig.preload(hash, json);
        setPreloaded(new Set(chain.rig.loadedKeys));
      }

      chain.rig.switchTo(hash);
      // Output normalisation depends on the model's loudness metadata.
      chain.refreshForActiveModel();
      setActiveHash(hash);
      setStatus({ kind: "idle", message: "" });

      const entry = entries.find((e) => e.ref.sha256 === hash);
      if (entry) void updateEntry({ ...entry, lastAuditionedAt: new Date().toISOString() });
    },
    [contents, getChain, entries, updateEntry],
  );

  const loadDI = useCallback(
    async (fileList: FileList | null) => {
      const file = fileList?.[0];
      if (!file) return;
      setStatus({ kind: "busy", message: "Decoding DI…" });
      const chain = await getChain();
      diBufferRef.current = await chain.context.decodeAudioData(await file.arrayBuffer());
      setDiName(file.name);
      setStatus({
        kind: "ok",
        message: `DI ready — ${diBufferRef.current.duration.toFixed(1)}s loop.`,
      });
    },
    [getChain],
  );

  const loadIR = useCallback(
    async (fileList: FileList | null) => {
      const file = fileList?.[0];
      if (!file) return;
      const chain = await getChain();
      const buf = await chain.context.decodeAudioData(await file.arrayBuffer());
      await chain.setIR(buf);
      setIrName(file.name);
      setStatus({ kind: "ok", message: `IR loaded — ${file.name}` });
    },
    [getChain],
  );

  const play = useCallback(async () => {
    const chain = await getChain();
    const buf = diBufferRef.current;
    if (!buf) return;
    await chain.context.resume();

    sourceRef.current?.stop();
    const src = chain.context.createBufferSource();
    src.buffer = buf;
    src.loop = true;
    src.connect(chain.input);
    src.start();
    sourceRef.current = src;
    setPlaying(true);
  }, [getChain]);

  const stop = useCallback(() => {
    sourceRef.current?.stop();
    sourceRef.current = null;
    setPlaying(false);
  }, []);

  // --- presets -------------------------------------------------------------

  const savePreset = useCallback(() => {
    const entry = entries.find((e) => e.ref.sha256 === (activeHash ?? selectedHash));
    if (!entry) {
      setStatus({ kind: "err", message: "Pick a model first — a preset needs one." });
      return;
    }
    const preset = buildPreset({
      name: presetName,
      params,
      model: entry.ref,
      tags: entry.tags,
      basedOn,
    });
    downloadPreset(preset);
    setStatus({ kind: "ok", message: `Saved ${preset.name}.json` });
  }, [entries, activeHash, selectedHash, presetName, params, basedOn]);

  const openPreset = useCallback(
    async (fileList: FileList | null) => {
      const file = fileList?.[0];
      if (!file) return;
      const { preset, error } = parsePresetFile(await file.text());
      if (!preset) {
        setStatus({ kind: "err", message: `Could not read preset: ${error}` });
        return;
      }

      setParams(chainParamsFromPreset(preset));
      setPresetName(preset.name);
      setBasedOn(preset);

      const wanted = preset.models[0]?.ref.sha256;
      if (wanted && contents.has(wanted)) {
        await switchTo(wanted);
        setStatus({ kind: "ok", message: `Loaded ${preset.name}` });
      } else {
        setStatus({
          kind: "err",
          message: `Loaded ${preset.name}, but its model (${
            preset.models[0]?.ref.fileName ?? "unknown"
          }) isn't imported here.`,
        });
      }
    },
    [contents, switchTo],
  );

  const activeEntry = entries.find((e) => e.ref.sha256 === (selectedHash ?? activeHash));

  return (
    <div className="wrap">
      <header>
        <h1>Gootar</h1>
        <p>
          Import your .nam captures, tag them, and A/B against a DI loop with no
          dropout. Nothing uploads — models are hashed and read in your browser.
        </p>
      </header>

      <section className="panel">
        <div className="row">
          <input
            ref={modelInputRef}
            type="file"
            accept=".nam"
            multiple
            hidden
            onChange={(e) => void importModels(e.target.files)}
          />
          <button type="button" className="primary" onClick={() => modelInputRef.current?.click()}>
            Import .nam files
          </button>

          <input ref={diInputRef} type="file" accept="audio/*" hidden
                 onChange={(e) => void loadDI(e.target.files)} />
          <button type="button" onClick={() => diInputRef.current?.click()}>
            {diName ? "Change DI" : "Load DI loop"}
          </button>

          <input ref={irInputRef} type="file" accept="audio/*" hidden
                 onChange={(e) => void loadIR(e.target.files)} />
          <button type="button" onClick={() => irInputRef.current?.click()}>
            {irName ? "Change IR" : "Load IR"}
          </button>

          <button type="button" onClick={() => (playing ? stop() : void play())} disabled={!diName}>
            {playing ? "◼ Stop" : "▶ Play"}
          </button>

          <span className="spacer" />

          <button type="button" onClick={() => void preloadVisible()} disabled={!loadable.length}>
            Preload {loadable.length}
          </button>
        </div>
        <p className="hint">
          {diName ?? "no DI loaded"} · {irName ?? "no IR"} ·{" "}
          {preloaded.size} of {entries.length} models ready
        </p>
      </section>

      <div className="split">
        <section className="panel">
          <h2>Library</h2>

          <input
            className="search"
            placeholder="Search name, tag or note…"
            value={search}
            onChange={(e) => setSearch(e.target.value)}
          />

          <div className="filters">
            <button
              type="button"
              className={`chip${favOnly ? " on" : ""}`}
              onClick={() => setFavOnly((v) => !v)}
            >
              ★ favourites
            </button>
            {allTags.map(([tag, count]) => (
              <button
                key={tag}
                type="button"
                className={`chip${tagFilter === tag ? " on" : ""}`}
                onClick={() => setTagFilter((t) => (t === tag ? null : tag))}
              >
                {tag} <em>{count}</em>
              </button>
            ))}
          </div>

          {visible.length === 0 ? (
            <p className="empty">
              {entries.length === 0
                ? "No models yet. Import some .nam files to begin."
                : "Nothing matches those filters."}
            </p>
          ) : (
            <ul className="list">
              {visible.map((entry) => {
                const hash = entry.ref.sha256;
                const isActive = hash === activeHash;
                const isReady = preloaded.has(hash);
                const inSession = contents.has(hash);
                return (
                  <li
                    key={hash}
                    className={`item${isActive ? " active" : ""}${
                      hash === selectedHash ? " selected" : ""
                    }`}
                    onClick={() => void switchTo(hash)}
                  >
                    <button
                      type="button"
                      className={`star${entry.favorite ? " on" : ""}`}
                      aria-label="Toggle favourite"
                      onClick={(e) => {
                        e.stopPropagation();
                        void updateEntry({ ...entry, favorite: !entry.favorite });
                      }}
                    >
                      ★
                    </button>
                    <div className="grow">
                      <div className="name">{entry.ref.fileName}</div>
                      <div className="meta">
                        {entry.meta?.architecture ?? "unknown"}
                        {entry.meta?.sampleRate ? ` · ${entry.meta.sampleRate} Hz` : ""}
                        {entry.tags.length ? ` · ${entry.tags.join(", ")}` : ""}
                      </div>
                    </div>
                    <span className={`badge${isReady ? " ready" : ""}`}>
                      {isReady ? "ready" : inSession ? "loadable" : "no file"}
                    </span>
                  </li>
                );
              })}
            </ul>
          )}
        </section>

        <section className="panel">
          <h2>Rig</h2>

          <div className="knobs">
            <Knob label="Input" value={params.inputLevelDb} min={-20} max={20} unit=" dB"
                  onChange={(v) => setParams((p) => ({ ...p, inputLevelDb: v }))} />
            <Knob label="Bass" value={params.bass} min={0} max={10}
                  disabled={!params.toneStackEnabled}
                  onChange={(v) => setParams((p) => ({ ...p, bass: v }))} />
            <Knob label="Mid" value={params.mid} min={0} max={10}
                  disabled={!params.toneStackEnabled}
                  onChange={(v) => setParams((p) => ({ ...p, mid: v }))} />
            <Knob label="Treble" value={params.treble} min={0} max={10}
                  disabled={!params.toneStackEnabled}
                  onChange={(v) => setParams((p) => ({ ...p, treble: v }))} />
            <Knob label="Output" value={params.outputLevelDb} min={-40} max={40} unit=" dB"
                  onChange={(v) => setParams((p) => ({ ...p, outputLevelDb: v }))} />
          </div>

          <div className="row">
            <label className="toggle">
              <input type="checkbox" checked={params.toneStackEnabled}
                     onChange={(e) => setParams((p) => ({ ...p, toneStackEnabled: e.target.checked }))} />
              EQ
            </label>
            <label className="toggle">
              <input type="checkbox" checked={params.irEnabled}
                     onChange={(e) => setParams((p) => ({ ...p, irEnabled: e.target.checked }))} />
              IR
            </label>
            <select
              value={params.outputMode}
              onChange={(e) =>
                setParams((p) => ({ ...p, outputMode: e.target.value as ChainParams["outputMode"] }))
              }
            >
              <option value="raw">Raw</option>
              <option value="normalized">Normalized</option>
              <option value="calibrated">Calibrated</option>
            </select>
          </div>

          <p className="hint">
            No noise gate here — a pre-recorded DI has nothing to gate. The
            player has the real one, and its setting survives a round trip.
          </p>

          <h2 style={{ marginTop: 22 }}>Selected model</h2>
          {activeEntry ? (
            <>
              <div className="name">{activeEntry.ref.fileName}</div>
              <code className="hash">{activeEntry.ref.sha256}</code>
              <TagInput
                tags={activeEntry.tags}
                onChange={(tags) => void updateEntry({ ...activeEntry, tags })}
              />
            </>
          ) : (
            <p className="empty">Click a model to select it.</p>
          )}

          <h2 style={{ marginTop: 22 }}>Preset</h2>
          <div className="row">
            <input
              className="search grow"
              value={presetName}
              onChange={(e) => setPresetName(e.target.value)}
              placeholder="Preset name"
            />
          </div>
          <div className="row">
            <button type="button" className="primary" onClick={savePreset}>
              Save .json
            </button>
            <input ref={presetInputRef} type="file" accept=".json,application/json" hidden
                   onChange={(e) => void openPreset(e.target.files)} />
            <button type="button" onClick={() => presetInputRef.current?.click()}>
              Open .json
            </button>
          </div>
          <p className="hint">
            The same file opens in the native player. It points at models by
            hash, so it resolves wherever your files actually live.
          </p>
        </section>
      </div>

      {status.message && (
        <p className={`status ${status.kind}`}>{status.message}</p>
      )}
    </div>
  );
}

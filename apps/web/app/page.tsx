"use client";

import { useCallback, useEffect, useRef, useState } from "react";
import { NamRig } from "@/lib/audio/nam-rig";
import { ingestFiles, type IngestedModel } from "@/lib/library/ingest";
import { NATIVE_MODEL_SAMPLE_RATE } from "@gootar/preset-schema";

type Status = { kind: "idle" | "busy" | "ok" | "err"; message: string };

export default function LibrarianPage() {
  const [models, setModels] = useState<IngestedModel[]>([]);
  const [activeHash, setActiveHash] = useState<string | null>(null);
  const [preloaded, setPreloaded] = useState<Set<string>>(new Set());
  const [playing, setPlaying] = useState(false);
  const [diName, setDiName] = useState<string | null>(null);
  const [status, setStatus] = useState<Status>({ kind: "idle", message: "" });

  const modelInputRef = useRef<HTMLInputElement>(null);
  const diInputRef = useRef<HTMLInputElement>(null);
  const rigRef = useRef<NamRig | null>(null);
  const diBufferRef = useRef<AudioBuffer | null>(null);
  const sourceRef = useRef<AudioBufferSourceNode | null>(null);

  useEffect(() => {
    return () => {
      sourceRef.current?.stop();
      void rigRef.current?.dispose();
    };
  }, []);

  /** One AudioContext for the page, created on first user gesture. */
  const getRig = useCallback(async (): Promise<NamRig> => {
    if (rigRef.current) return rigRef.current;
    // Models are trained at 48 k; ask the browser for it so nothing resamples
    // behind our back.
    const ctx = new AudioContext({ sampleRate: NATIVE_MODEL_SAMPLE_RATE });
    const rig = await NamRig.create(ctx);
    rig.output.connect(ctx.destination);
    rigRef.current = rig;
    return rig;
  }, []);

  const onPickModels = useCallback(
    async (fileList: FileList | null) => {
      if (!fileList?.length) return;
      setStatus({ kind: "busy", message: "Hashing and parsing…" });
      const { models: found, errors } = await ingestFiles([...fileList]);

      setModels((prev) => {
        const byHash = new Map(prev.map((m) => [m.entry.ref.sha256, m]));
        for (const m of found) byHash.set(m.entry.ref.sha256, m);
        return [...byHash.values()];
      });

      setStatus({
        kind: errors.length ? "err" : "ok",
        message: errors.length
          ? `Imported ${found.length}; skipped ${errors.length} (${errors[0]?.message})`
          : `Imported ${found.length} model${found.length === 1 ? "" : "s"}.`,
      });
    },
    [],
  );

  const onPickDi = useCallback(
    async (fileList: FileList | null) => {
      const file = fileList?.[0];
      if (!file) return;
      setStatus({ kind: "busy", message: "Decoding DI loop…" });
      const rig = await getRig();
      const buf = await rig.context.decodeAudioData(await file.arrayBuffer());
      diBufferRef.current = buf;
      setDiName(file.name);
      setStatus({
        kind: "ok",
        message: `DI loop ready — ${buf.duration.toFixed(1)}s.`,
      });
    },
    [getRig],
  );

  /**
   * Preloading every model up front is what makes switching gapless. It costs
   * a few hundred ms per model once, instead of a dropout on every compare.
   */
  const preloadAll = useCallback(async () => {
    if (!models.length) return;
    setStatus({ kind: "busy", message: "Preloading models…" });
    const rig = await getRig();
    for (const m of models) {
      await rig.preload(m.entry.ref.sha256, m.contents);
      setPreloaded(new Set(rig.loadedKeys));
    }
    setStatus({
      kind: "ok",
      message: `${rig.loadedKeys.length} models loaded — switching is now gapless.`,
    });
  }, [models, getRig]);

  const play = useCallback(async () => {
    const rig = await getRig();
    const buf = diBufferRef.current;
    if (!buf) return;
    await rig.context.resume();

    sourceRef.current?.stop();
    const src = rig.context.createBufferSource();
    src.buffer = buf;
    src.loop = true;
    src.connect(rig.input);
    src.start();
    sourceRef.current = src;
    setPlaying(true);
  }, [getRig]);

  const stop = useCallback(() => {
    sourceRef.current?.stop();
    sourceRef.current = null;
    setPlaying(false);
  }, []);

  const select = useCallback(
    async (hash: string) => {
      const rig = await getRig();
      if (!rig.loadedKeys.includes(hash)) {
        const model = models.find((m) => m.entry.ref.sha256 === hash);
        if (!model) return;
        setStatus({ kind: "busy", message: "Loading (not preloaded — expect a gap)…" });
        await rig.preload(hash, model.contents);
        setPreloaded(new Set(rig.loadedKeys));
      }
      rig.switchTo(hash);
      setActiveHash(hash);
      setStatus({ kind: "idle", message: "" });
    },
    [models, getRig],
  );

  return (
    <div className="wrap">
      <header>
        <h1>Gootar — NAM Librarian</h1>
        <p>
          Import your .nam captures, audition them against a DI loop, and A/B
          without a dropout. Nothing is uploaded; models are hashed and read
          in the browser.
        </p>
      </header>

      <section className="panel">
        <h2>1 · Model library</h2>
        <div className="row">
          <input
            ref={modelInputRef}
            type="file"
            accept=".nam"
            multiple
            hidden
            onChange={(e) => void onPickModels(e.target.files)}
          />
          <button type="button" onClick={() => modelInputRef.current?.click()}>
            Add .nam files
          </button>
          <button
            type="button"
            onClick={() => void preloadAll()}
            disabled={!models.length}
          >
            Preload all ({models.length})
          </button>
        </div>
        <p className="hint">
          Pick a whole folder in Chrome by choosing the files inside it — the
          relative path is kept so the native player can find them again by
          hash.
        </p>
      </section>

      <section className="panel">
        <h2>2 · DI loop</h2>
        <div className="row">
          <input
            ref={diInputRef}
            type="file"
            accept="audio/*"
            hidden
            onChange={(e) => void onPickDi(e.target.files)}
          />
          <button type="button" onClick={() => diInputRef.current?.click()}>
            Load DI .wav
          </button>
          <button
            type="button"
            className="primary"
            onClick={() => (playing ? stop() : void play())}
            disabled={!diName}
          >
            {playing ? "Stop" : "Play loop"}
          </button>
          <span className="meta">{diName ?? "no DI loaded"}</span>
        </div>
      </section>

      <section className="panel">
        <h2>3 · Audition</h2>
        {models.length === 0 ? (
          <p className="empty">No models yet. Add some .nam files above.</p>
        ) : (
          <ul className="list">
            {models.map((m) => {
              const hash = m.entry.ref.sha256;
              const isActive = hash === activeHash;
              return (
                <li key={hash} className={`item${isActive ? " active" : ""}`}>
                  <div>
                    <div className="name">{m.entry.ref.fileName}</div>
                    <div className="meta">
                      {m.entry.meta?.architecture ?? "unknown arch"}
                      {m.entry.meta?.sampleRate
                        ? ` · ${m.entry.meta.sampleRate} Hz`
                        : ""}
                      {" · "}
                      <code className="hash">{hash.slice(0, 12)}</code>
                    </div>
                  </div>
                  <div className="spacer" />
                  <span className="meta">
                    {preloaded.has(hash) ? "ready" : "not loaded"}
                  </span>
                  <button type="button" onClick={() => void select(hash)}>
                    {isActive ? "Playing" : "Switch to"}
                  </button>
                </li>
              );
            })}
          </ul>
        )}
        {status.message && (
          <p className={status.kind === "err" ? "err" : status.kind === "ok" ? "ok" : "hint"}>
            {status.message}
          </p>
        )}
      </section>
    </div>
  );
}

/**
 * Runs Next with telemetry off.
 *
 * Next.js phones home with anonymous usage data by default. This is a local
 * tool - nothing about it should be talking to anyone - so the env var is set
 * here rather than left to whoever remembers to run `next telemetry disable`
 * on each machine.
 *
 * A tiny wrapper rather than a cross-env dependency: setting one environment
 * variable is not worth another package.
 */
import { spawn } from "node:child_process";

const command = process.argv[2];
if (!command) {
  console.error("usage: node scripts/run-next.mjs <dev|build|start>");
  process.exit(1);
}

const child = spawn(
  process.platform === "win32" ? "npx.cmd" : "npx",
  ["next", ...process.argv.slice(2)],
  {
    stdio: "inherit",
    env: { ...process.env, NEXT_TELEMETRY_DISABLED: "1" },
  },
);

child.on("exit", (code) => process.exit(code ?? 0));
child.on("error", (err) => {
  console.error(err);
  process.exit(1);
});

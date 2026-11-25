import { spawn } from "node:child_process";
import path from "node:path";
import os from "node:os";
import fs from "node:fs/promises";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, "..");
const configFile = path.join(here, "panel-config.json");
const rayaConfigFile = path.join(root, "raya-config.json");
const PORT = 8767;
const originalPanel = await fs.readFile(configFile);
const originalRaya = await fs.readFile(rayaConfigFile);
const before = JSON.parse(originalPanel.toString("utf8"));
const settings = JSON.parse(originalRaya.toString("utf8"));
const removableRoot = await fs.mkdtemp(path.join(os.tmpdir(), "raya-panel-remove-test-"));
const child = spawn(process.execPath, [path.join(here, "server.mjs")], {
  cwd: root,
  env: { ...process.env, RAYA_PANEL_PORT: String(PORT) },
  windowsHide: true,
  stdio: ["ignore", "pipe", "pipe"]
});
let output = "";
let stderr = "";
child.stdout.on("data", b => output += b.toString());
child.stderr.on("data", b => stderr += b.toString());
const wait = async (predicate, timeoutMs = 8000) => {
  const start = Date.now();
  while (Date.now() - start < timeoutMs) {
    if (predicate()) return;
    if (child.exitCode !== null) throw new Error("Panel exited early: " + stderr);
    await new Promise(r => setTimeout(r, 100));
  }
  throw new Error("Timed out waiting for panel startup: " + output + stderr);
};
let checks = 0;
try {
  await wait(() => output.includes("http://127.0.0.1:" + PORT + "/?token="));
  const match = output.match(new RegExp("http://127\\.0\\.0\\.1:" + PORT + "/\\?token=([a-f0-9]{64})"));
  if (!match) throw new Error("Session token URL was not emitted.");
  const token = match[1];
  const base = "http://127.0.0.1:" + PORT;
  const publicPage = await fetch(base + "/");
  if (publicPage.status !== 401) throw new Error("Dashboard HTML was exposed without the session URL token.");
  checks++;
  const page = await fetch(base + "/?token=" + token);
  const pageText = await page.text();
  if (page.status !== 200 || !pageText.includes(token) || pageText.includes("__PANEL_TOKEN__")) throw new Error("Local dashboard page/token rendering failed.");
  checks++;
  if (!pageText.includes('api("/config/remove-root"')) throw new Error("Dashboard remove button is not wired to the persistent remove-root API.");
  checks++;
  const unauth = await fetch(base + "/api/status");
  if (unauth.status !== 401) throw new Error("Unauthenticated API request was not rejected.");
  checks++;
  const auth = await fetch(base + "/api/status", { headers: { authorization: "Bearer " + token } });
  if (auth.status !== 200) throw new Error("Authenticated status request failed.");
  const status = await auth.json();
  if (status.panel.host !== "127.0.0.1" || status.config.allowedRoots.length !== before.allowedRoots.length) throw new Error("Status payload mismatch.");
  checks++;
  const goodSave = await fetch(base + "/api/config/save", {
    method: "POST",
    headers: { authorization: "Bearer " + token, "content-type": "application/json" },
    body: JSON.stringify({ allowedRoots: [root, removableRoot], settings, powershellEnabled: before.powershellEnabled })
  });
  if (goodSave.status !== 200) throw new Error("Valid locked-root configuration was rejected: " + await goodSave.text());
  checks++;
  const remove = await fetch(base + "/api/config/remove-root", {
    method: "POST",
    headers: { authorization: "Bearer " + token, "content-type": "application/json" },
    body: JSON.stringify({ root: removableRoot })
  });
  if (remove.status !== 200) throw new Error("Valid remove-root request failed: " + await remove.text());
  const removed = await remove.json();
  if (removed.removedRoot !== removableRoot || removed.allowedRoots.length !== 1 || removed.allowedRoots[0].toLowerCase() !== root.toLowerCase() || removed.restartRequired !== true) {
    throw new Error("Remove-root response did not preserve the locked root or report restart requirement.");
  }
  checks++;
  const afterRemove = await fs.readFile(configFile);
  const persisted = JSON.parse(afterRemove.toString("utf8"));
  if (persisted.allowedRoots.length !== 1 || persisted.allowedRoots[0].toLowerCase() !== root.toLowerCase()) throw new Error("Remove-root did not persist to panel-config.json.");
  checks++;
  const lockedRemove = await fetch(base + "/api/config/remove-root", {
    method: "POST",
    headers: { authorization: "Bearer " + token, "content-type": "application/json" },
    body: JSON.stringify({ root })
  });
  if (lockedRemove.status !== 400) throw new Error("Locked Raya root removal was not rejected.");
  const afterLockedRemove = await fs.readFile(configFile);
  if (!afterLockedRemove.equals(afterRemove)) throw new Error("Rejected locked-root removal modified panel-config.json.");
  checks++;
  const badSave = await fetch(base + "/api/config/save", {
    method: "POST",
    headers: { authorization: "Bearer " + token, "content-type": "application/json" },
    body: JSON.stringify({ allowedRoots: [root, path.parse(root).root], settings, powershellEnabled: false })
  });
  if (badSave.status !== 400) throw new Error("Drive-root allowlist attempt was not rejected.");
  const afterBad = await fs.readFile(configFile);
  if (!afterBad.equals(afterRemove)) throw new Error("Rejected config request modified panel-config.json.");
  checks++;
  const badSystem = await fetch(base + "/api/config/save", {
    method: "POST",
    headers: { authorization: "Bearer " + token, "content-type": "application/json" },
    body: JSON.stringify({ allowedRoots: [root, process.env.WINDIR || "C:\\\\Windows"], settings, powershellEnabled: false })
  });
  if (badSystem.status !== 400) throw new Error("Windows system-root allowlist attempt was not rejected.");
  const afterSystem = await fs.readFile(configFile);
  if (!afterSystem.equals(afterRemove)) throw new Error("Rejected system-root config request modified panel-config.json.");
  checks++;
  console.log(JSON.stringify({
    ok: true,
    checks,
    dashboardTokenRequired: true,
    unauthenticatedApiBlocked: true,
    authenticatedStatus: true,
    removeButtonWired: true,
    removeRootPersisted: true,
    lockedRootRemovalRejected: true,
    driveRootRejected: true,
    systemRootRejected: true,
    rejectedConfigNoMutation: true
  }));
} finally {
  if (child.exitCode === null) {
    child.kill();
    await Promise.race([
      new Promise(resolve => child.once("close", resolve)),
      new Promise(resolve => setTimeout(resolve, 2500))
    ]);
  }
  await fs.writeFile(configFile, originalPanel);
  await fs.writeFile(rayaConfigFile, originalRaya);
  await fs.rm(removableRoot, { recursive: true, force: true });
}
import { spawn } from "node:child_process";
import { randomBytes } from "node:crypto";
import { setTimeout as delay } from "node:timers/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const token = randomBytes(48).toString("base64");
const child = spawn(process.execPath, [path.join(here, "server.mjs"), "--http"], {
  cwd: here,
  env: { ...process.env, MCP_BEARER_TOKEN: token, MCP_HOST: "127.0.0.1", MCP_PORT: "18766" },
  windowsHide: true,
  stdio: ["ignore", "pipe", "pipe"]
});
let stderr = "";
child.stderr.on("data", b => { stderr += b.toString(); });
try {
  let ready = false;
  for (let i = 0; i < 30; i++) {
    if (stderr.includes("listening at")) { ready = true; break; }
    if (child.exitCode !== null) throw new Error("Server exited early: " + stderr);
    await delay(200);
  }
  if (!ready) throw new Error("HTTP server did not become ready: " + stderr);
  const url = "http://127.0.0.1:18766/mcp";
  const unauthorized = await fetch(url, { method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify({ jsonrpc: "2.0", id: 1, method: "initialize", params: { protocolVersion: "2025-03-26", capabilities: {}, clientInfo: { name: "smoke-test", version: "0.1.0" } } }) });
  if (unauthorized.status !== 401) throw new Error("Expected unauthorized HTTP 401, got " + unauthorized.status);
  const authorized = await fetch(url, {
    method: "POST",
    headers: { "content-type": "application/json", "accept": "application/json, text/event-stream", "authorization": "Bearer " + token },
    body: JSON.stringify({ jsonrpc: "2.0", id: 2, method: "initialize", params: { protocolVersion: "2025-03-26", capabilities: {}, clientInfo: { name: "smoke-test", version: "0.1.0" } } })
  });
  const body = await authorized.text();
  if (authorized.status < 200 || authorized.status >= 300) throw new Error("Authenticated initialize failed: HTTP " + authorized.status + " " + body);
  console.log(JSON.stringify({ ok: true, unauthorizedBlocked: unauthorized.status === 401, authenticatedInitializeStatus: authorized.status, responseContentType: authorized.headers.get("content-type") }, null, 2));
} finally {
  child.kill();
  await delay(300);
}


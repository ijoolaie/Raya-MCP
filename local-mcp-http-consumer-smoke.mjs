import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { randomBytes } from "node:crypto";
import { once } from "node:events";
import { createServer } from "node:net";
import path from "node:path";
import { setTimeout as delay } from "node:timers/promises";

const probe = createServer();
await new Promise(resolve => probe.listen(0, "127.0.0.1", resolve));
const port = probe.address().port;
await new Promise(resolve => probe.close(resolve));

const token = randomBytes(48).toString("base64");
const entry = path.resolve("node_modules/@ijoolaie/local-mcp/src/server.mjs");
const child = spawn(process.execPath, [entry, "--http"], {
  cwd: process.cwd(),
  env: {
    ...process.env,
    MCP_ALLOWED_ROOTS: process.cwd(),
    MCP_BEARER_TOKEN: token,
    MCP_HOST: "127.0.0.1",
    MCP_PORT: String(port)
  },
  windowsHide: true,
  stdio: ["ignore", "ignore", "pipe"]
});
let stderr = "";
child.stderr.on("data", chunk => { stderr += chunk.toString(); });

try {
  let ready = false;
  for (let i = 0; i < 50; i++) {
    if (stderr.includes("listening at")) { ready = true; break; }
    if (child.exitCode !== null) throw new Error("Local MCP HTTP child exited early.");
    await delay(100);
  }
  assert.equal(ready, true, "Local MCP HTTP server became ready");

  const url = `http://127.0.0.1:${port}/mcp`;
  const body = JSON.stringify({
    jsonrpc: "2.0",
    id: 1,
    method: "initialize",
    params: {
      protocolVersion: "2025-03-26",
      capabilities: {},
      clientInfo: { name: "isolated-local-http-smoke", version: "0.1.0" }
    }
  });
  const denied = await fetch(url, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body
  });
  const allowed = await fetch(url, {
    method: "POST",
    headers: {
      "content-type": "application/json",
      "accept": "application/json, text/event-stream",
      "authorization": `Bearer ${token}`
    },
    body
  });
  const responseText = await allowed.text();
  assert.equal(denied.status, 401);
  assert.equal(allowed.status, 200, responseText);
  assert.match(allowed.headers.get("content-type") || "", /text\/event-stream/);
  assert.ok(allowed.headers.get("mcp-session-id"), "Stateful HTTP session id must be issued.");
  process.stdout.write(JSON.stringify({
    ok: true,
    unauthenticatedStatus: denied.status,
    authenticatedInitializeStatus: allowed.status,
    contentType: allowed.headers.get("content-type")
  }) + "\n");
} finally {
  if (child.exitCode === null) {
    child.kill();
    await Promise.race([once(child, "exit"), delay(5000)]);
  }
}
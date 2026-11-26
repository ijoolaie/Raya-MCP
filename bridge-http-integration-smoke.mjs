import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { randomBytes } from "node:crypto";
import { once } from "node:events";
import { createServer } from "node:net";
import path from "node:path";
import { setTimeout as delay } from "node:timers/promises";
import { createBridgeConnector } from "@ijoolaie/bridge/connector";

const probe = createServer();
await new Promise(resolve => probe.listen(0, "127.0.0.1", resolve));
const port = probe.address().port;
await new Promise(resolve => probe.close(resolve));

const token = randomBytes(48).toString("base64");
const child = spawn(process.execPath, [path.join(process.cwd(), "server.mjs"), "--http"], {
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
const bridge = createBridgeConnector();

try {
  let ready = false;
  for (let i = 0; i < 60; i++) {
    if (stderr.includes("listening at")) { ready = true; break; }
    if (child.exitCode !== null) throw new Error("Local MCP HTTP child exited early.");
    await delay(100);
  }
  assert.equal(ready, true, "Local MCP HTTP endpoint became ready");
  try {
    await bridge.connectHttp({
      id: "local-http",
      url: `http://127.0.0.1:${port}/mcp`,
      headers: { authorization: `Bearer ${token}` },
      fetch: async (input, init) => {
        const response = await fetch(input, init);
        const method = init?.body ? (() => { try { return JSON.parse(init.body).method || "notification"; } catch { return "unknown"; } })() : "GET";
        process.stderr.write("BRIDGE_HTTP_TRACE " + method + " -> " + response.status + "\n");
        return response;
      }
    });
  } catch (error) {
    process.stderr.write("LOCAL_MCP_HTTP_SERVER_LOG: " + stderr + "\n");
    throw error;
  }
  const { tools } = await bridge.listTools("local-http");
  assert.ok(tools.length >= 40);
  assert.ok(tools.some(tool => tool.name === "workspace_roots"));
  const result = await bridge.callTool("local-http", "workspace_roots");
  assert.equal(result.isError, undefined);
  process.stdout.write(JSON.stringify({
    ok: true,
    transport: "streamable-http",
    toolCount: tools.length,
    workspaceRootsCall: true
  }) + "\n");
} finally {
  await bridge.closeAll().catch(() => {});
  if (child.exitCode === null) {
    child.kill();
    await Promise.race([once(child, "exit"), delay(5000)]);
  }
}
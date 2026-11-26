import assert from "node:assert/strict";
import fs from "node:fs/promises";
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

const token = randomBytes(32).toString("hex");
const endpointConfig = path.join(process.cwd(), ".bridge-endpoints-smoke.json");
const coreFixture = path.resolve("node_modules/@ijoolaie/mcp-core/examples/minimal-stdio-server.mjs");
await fs.writeFile(endpointConfig, JSON.stringify({
  version: 1,
  endpoints: [{
    id: "core-ping",
    name: "Core sample server",
    transport: "stdio",
    command: "node",
    args: [coreFixture],
    cwd: path.dirname(coreFixture)
  }]
}, null, 2));
const entry = path.resolve("node_modules/@ijoolaie/bridge/src/admin-panel/server.mjs");
const child = spawn(process.execPath, [entry], {
  cwd: process.cwd(),
  env: {
    ...process.env,
    RAYA_WORKSPACE_ROOT: process.cwd(),
    RAYA_PANEL_NO_BROWSER: "true",
    RAYA_PANEL_TOKEN: token,
    RAYA_PANEL_PORT: String(port),
    RAYA_PANEL_CONFIG_FILE: path.join(process.cwd(), "panel-config.json"),
    RAYA_CONFIG_FILE: path.join(process.cwd(), "raya-config.json"),
    RAYA_MCP_ENDPOINTS_FILE: endpointConfig
  },
  windowsHide: true,
  stdio: ["ignore", "pipe", "pipe"]
});
let stdout = "";
let stderr = "";
child.stdout.on("data", chunk => { stdout += chunk.toString(); });
child.stderr.on("data", chunk => { stderr += chunk.toString(); });

try {
  let ready = false;
  for (let i = 0; i < 50; i++) {
    if (stdout.includes("Admin Panel is ready")) { ready = true; break; }
    if (child.exitCode !== null) throw new Error("Bridge panel child exited early: " + stderr);
    await delay(100);
  }
  assert.equal(ready, true, "Bridge Admin Panel became ready");

  const base = `http://127.0.0.1:${port}`;
  const auth = { authorization: "Bearer " + token, "content-type": "application/json" };
  const deniedPage = await fetch(base + "/");
  const page = await fetch(base + "/?token=" + token);
  const pageText = await page.text();
  const deniedApi = await fetch(base + "/api/status");
  const deniedEndpoints = await fetch(base + "/api/mcp/endpoints");

  assert.equal(deniedPage.status, 401);
  assert.equal(page.status, 200);
  assert.ok(pageText.includes(token));
  assert.ok(pageText.includes("Raya MCP"));
  assert.equal(deniedApi.status, 401);
  assert.equal(deniedEndpoints.status, 401);

  const endpointResponse = await fetch(base + "/api/mcp/endpoints", { headers: auth });
  assert.equal(endpointResponse.status, 200);
  const endpointData = await endpointResponse.json();
  assert.equal(endpointData.endpoints.length, 1);
  assert.equal(endpointData.endpoints[0].state, "disconnected");
  assert.equal(JSON.stringify(endpointData).includes(coreFixture), false);

  const connected = await fetch(base + "/api/mcp/connect", { method: "POST", headers: auth, body: JSON.stringify({ id: "core-ping" }) });
  assert.equal(connected.status, 200, await connected.text());
  const toolsResponse = await fetch(base + "/api/mcp/tools", { method: "POST", headers: auth, body: JSON.stringify({ id: "core-ping" }) });
  assert.equal(toolsResponse.status, 200);
  const tools = await toolsResponse.json();
  assert.deepEqual(tools.tools.map(tool => tool.name), ["ping"]);
  const callResponse = await fetch(base + "/api/mcp/call", { method: "POST", headers: auth, body: JSON.stringify({ id: "core-ping", name: "ping", arguments: {} }) });
  assert.equal(callResponse.status, 200);
  const call = await callResponse.json();
  assert.equal(call.content?.[0]?.text, "pong");
  const disconnected = await fetch(base + "/api/mcp/disconnect", { method: "POST", headers: auth, body: JSON.stringify({ id: "core-ping" }) });
  assert.equal(disconnected.status, 200);

  process.stdout.write(JSON.stringify({
    ok: true,
    package: "@ijoolaie/bridge",
    localMcpRequired: false,
    dashboardTokenRequired: true,
    unauthenticatedApiBlocked: true,
    configuredEndpointListed: true,
    endpointConnected: true,
    remoteToolsDiscovered: tools.tools.length,
    remoteToolInvoked: call.content?.[0]?.text,
    endpointDisconnected: true
  }) + "\n");
} finally {
  if (child.exitCode === null) {
    child.kill();
    await Promise.race([once(child, "exit"), delay(5000)]);
  }
  await fs.rm(endpointConfig, { force: true });
}

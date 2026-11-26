import assert from "node:assert/strict";
import test from "node:test";
import {
  RAYA_MCP_CORE_API_VERSION,
  createCoreServer,
  createCoreClient,
  createStdioClientTransport,
  createStreamableHttpClientTransport,
  createStdioTransport,
  createStreamableHttpTransport
} from "../src/index.mjs";

test("Core exports a versioned API contract", () => {
  assert.equal(RAYA_MCP_CORE_API_VERSION, "0.1.0");
});

test("Core creates an MCP server and accepts capability registration", () => {
  let registered = false;
  const server = createCoreServer({
    name: "core-contract-test",
    version: "0.0.1",
    registerCapabilities(instance) {
      registered = true;
      instance.tool("core_probe", "Core API contract probe", {}, async () => ({
        content: [{ type: "text", text: "ok" }]
      }));
    }
  });

  assert.equal(registered, true);
  assert.equal(typeof server.connect, "function");
  assert.equal(typeof server.close, "function");
});

test("Core rejects invalid capability registration callbacks", () => {
  assert.throws(
    () => createCoreServer({ registerCapabilities: {} }),
    /registerCapabilities must be a function/
  );
});

test("Core creates supported MCP transport instances", () => {
  const stdio = createStdioTransport();
  const http = createStreamableHttpTransport({ sessionIdGenerator: undefined });

  assert.equal(typeof stdio.start, "function");
  assert.equal(typeof stdio.close, "function");
  assert.equal(typeof http.handleRequest, "function");
  assert.equal(typeof http.close, "function");
});
test("Core creates MCP clients and client transports", () => {
  const client = createCoreClient({ name: "bridge-test-client", version: "0.1.0" });
  const stdio = createStdioClientTransport({ command: process.execPath, args: [] });
  const http = createStreamableHttpClientTransport("https://mcp.example.test/mcp", {
    requestInit: { headers: { "x-test": "1" } }
  });

  assert.equal(typeof client.connect, "function");
  assert.equal(typeof client.close, "function");
  assert.equal(typeof stdio.start, "function");
  assert.equal(typeof stdio.close, "function");
  assert.equal(typeof http.start, "function");
  assert.equal(typeof http.close, "function");
  assert.throws(() => createStdioClientTransport({ args: [] }), /requires a command/);
});

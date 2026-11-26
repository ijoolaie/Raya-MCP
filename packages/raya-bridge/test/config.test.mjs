import assert from "node:assert/strict";
import fs from "node:fs";
import test from "node:test";
import { parseBridgeEndpointConfig, publicBridgeEndpoint, resolveBridgeEndpointConfig } from "../src/connector/config.mjs";

test("ships a valid example endpoint configuration", () => {
  const example = JSON.parse(fs.readFileSync(new URL("../mcp-endpoints.example.json", import.meta.url), "utf8"));
  assert.equal(parseBridgeEndpointConfig(example).length, 2);
});

test("parses STDIO and Streamable HTTP profiles without storing secret values", () => {
  const endpoints = parseBridgeEndpointConfig({
    version: 1,
    endpoints: [
      { id: "local", name: "Local tools", transport: "stdio", command: "node", args: ["server.mjs"], env: { SERVICE_TOKEN: "RAYA_SERVICE_TOKEN" } },
      { id: "remote", name: "Remote tools", transport: "streamable-http", url: "https://mcp.example.test/mcp", headersEnv: { Authorization: "RAYA_MCP_TOKEN" } }
    ]
  });
  assert.equal(endpoints.length, 2);
  assert.equal(endpoints[0].env.SERVICE_TOKEN, "RAYA_SERVICE_TOKEN");
  assert.equal(JSON.stringify(endpoints).includes("secret-value"), false);
});

test("resolves configured environment references only at connection time", () => {
  const [endpoint] = parseBridgeEndpointConfig({
    version: 1,
    endpoints: [{ id: "remote", name: "Remote tools", transport: "streamable-http", url: "https://mcp.example.test/mcp", headersEnv: { Authorization: "RAYA_MCP_TOKEN" } }]
  });
  const resolved = resolveBridgeEndpointConfig(endpoint, { RAYA_MCP_TOKEN: "test-secret" });
  assert.equal(resolved.headers.Authorization, "test-secret");
  assert.equal("headersEnv" in resolved, false);
  assert.equal(publicBridgeEndpoint(endpoint, [{ id: "remote", serverInfo: { name: "fixture" } }]).state, "connected");
  assert.equal(JSON.stringify(publicBridgeEndpoint(endpoint, [])).includes("test-secret"), false);
  assert.throws(() => resolveBridgeEndpointConfig(endpoint, {}), /RAYA_MCP_TOKEN/);
});

test("rejects duplicate ids, insecure remote HTTP, embedded credentials and unsafe headers", () => {
  assert.throws(() => parseBridgeEndpointConfig({ version: 1, endpoints: [
    { id: "same", name: "One", transport: "stdio", command: "node" },
    { id: "same", name: "Two", transport: "stdio", command: "node" }
  ] }), /Duplicate/);
  assert.throws(() => parseBridgeEndpointConfig({ version: 1, endpoints: [{ id: "bad", name: "Bad", transport: "streamable-http", url: "http://mcp.example.test/mcp" }] }), /HTTPS/);
  assert.throws(() => parseBridgeEndpointConfig({ version: 1, endpoints: [{ id: "bad", name: "Bad", transport: "streamable-http", url: "https://user:pass@example.test/mcp" }] }), /credentials/);
  assert.throws(() => parseBridgeEndpointConfig({ version: 1, endpoints: [{ id: "bad", name: "Bad", transport: "streamable-http", url: "https://mcp.example.test/mcp", headersEnv: { Host: "HOST_ENV" } }] }), /forbidden/);
});

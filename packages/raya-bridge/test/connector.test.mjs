import assert from "node:assert/strict";
import test from "node:test";
import { createServer } from "node:http";
import { randomUUID } from "node:crypto";
import path from "node:path";
import { fileURLToPath } from "node:url";
import {
  createCoreServer,
  createStreamableHttpTransport
} from "@ijoolaie/mcp-core";
import { createBridgeConnector } from "../src/connector/index.mjs";

const coreStdioExample = fileURLToPath(new URL("../../raya-mcp-core/examples/minimal-stdio-server.mjs", import.meta.url));

test("Bridge connects to a generic STDIO MCP server, discovers and invokes tools", async () => {
  const bridge = createBridgeConnector();
  const connectEndpoint = bridge.connectEndpoint;
  const connected = await connectEndpoint({
    id: "stdio-fixture",
    transport: "stdio",
    command: "node",
    args: [coreStdioExample],
    cwd: path.dirname(coreStdioExample),
    env: { RAYA_FIXTURE_MODE: "test" }
  });

  try {
    assert.equal(connected.transport, "stdio");
    const { tools } = await bridge.listTools("stdio-fixture");
    assert.deepEqual(tools.map(tool => tool.name), ["ping"]);
    const result = await bridge.callTool("stdio-fixture", "ping");
    assert.equal(result.content?.[0]?.text, "pong");
    assert.equal(JSON.stringify(bridge.listSessions()).includes("RAYA_FIXTURE_MODE"), false);
  } finally {
    await bridge.closeAll();
  }
  assert.deepEqual(bridge.listSessions(), []);
});

test("Bridge connects to a generic Streamable HTTP MCP server", async () => {
  const mcp = createCoreServer({
    name: "bridge-http-fixture",
    version: "0.1.0",
    registerCapabilities(server) {
      server.tool("ping", "Return pong.", {}, async () => ({
        content: [{ type: "text", text: "http-pong" }]
      }));
    }
  });
  const transport = createStreamableHttpTransport({ sessionIdGenerator: () => randomUUID() });
  await mcp.connect(transport);
  const http = createServer((req, res) => {
    if (new URL(req.url, "http://127.0.0.1").pathname !== "/mcp") {
      res.writeHead(404).end();
      return;
    }
    const chunks = [];
    req.on("data", chunk => chunks.push(chunk));
    req.on("end", async () => {
      try {
        const parsedBody = chunks.length ? JSON.parse(Buffer.concat(chunks).toString("utf8")) : undefined;
        await transport.handleRequest(req, res, parsedBody);
      } catch (error) {
        process.stderr.write("BRIDGE_HTTP_FIXTURE: " + (error?.stack || error) + "\n");
        if (!res.headersSent) res.writeHead(500);
        res.end(String(error?.message || error));
      }
    });
  });
  await new Promise(resolve => http.listen(0, "127.0.0.1", resolve));
  const port = http.address().port;
  const bridge = createBridgeConnector();

  try {
    const connected = await bridge.connectHttp({
      id: "http-fixture",
      url: `http://127.0.0.1:${port}/mcp?access_hint=not_returned`,
      headers: { authorization: "Bearer test-secret" }
    });
    assert.equal(connected.transport, "streamable-http");
    const { tools } = await bridge.listTools("http-fixture");
    assert.deepEqual(tools.map(tool => tool.name), ["ping"]);
    const result = await bridge.callTool("http-fixture", "ping");
    assert.equal(result.content?.[0]?.text, "http-pong");
    const safeSessions = JSON.stringify(bridge.listSessions());
    assert.equal(safeSessions.includes("test-secret"), false);
    assert.equal(safeSessions.includes("access_hint"), false);
  } finally {
    await bridge.closeAll();
    await mcp.close();
    await new Promise(resolve => http.close(resolve));
  }
});

test("Bridge rejects insecure remote HTTP, URL credentials and unsafe headers", async () => {
  const bridge = createBridgeConnector();
  await assert.rejects(
    bridge.connectHttp({ id: "remote-http", url: "http://192.0.2.10/mcp" }),
    /HTTPS/
  );
  await assert.rejects(
    bridge.connectHttp({ id: "url-credentials", url: "https://user:pass@mcp.example.test/mcp" }),
    /Credentials/
  );
  await assert.rejects(
    bridge.connectHttp({ id: "bad-header", url: "https://mcp.example.test/mcp", headers: { "x-test": "ok\r\ninjected: yes" } }),
    /single-line/
  );
  await assert.rejects(
    bridge.connectHttp({ id: "framing-header", url: "https://mcp.example.test/mcp", headers: { host: "evil.example" } }),
    /cannot be overridden/
  );
  assert.deepEqual(bridge.listSessions(), []);
});
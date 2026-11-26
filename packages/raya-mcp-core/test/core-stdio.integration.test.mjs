import assert from "node:assert/strict";
import test from "node:test";
import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StdioClientTransport, getDefaultEnvironment } from "@modelcontextprotocol/sdk/client/stdio.js";
import { fileURLToPath } from "node:url";

test("Core can serve a custom MCP tool over STDIO without Local MCP or Bridge", async () => {
  const fixture = fileURLToPath(new URL("../examples/minimal-stdio-server.mjs", import.meta.url));
  const transport = new StdioClientTransport({
    command: process.execPath,
    args: [fixture],
    env: getDefaultEnvironment()
  });
  const client = new Client({ name: "raya-core-standalone-client", version: "0.1.0" });

  try {
    await client.connect(transport);
    const { tools } = await client.listTools();
    assert.deepEqual(tools.map(tool => tool.name), ["ping"]);
    const result = await client.callTool({ name: "ping", arguments: {} });
    assert.notEqual(result.isError, true);
    assert.equal(result.content?.[0]?.text, "pong");
  } finally {
    await client.close().catch(() => {});
  }
});

import assert from "node:assert/strict";
import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StdioClientTransport, getDefaultEnvironment } from "@modelcontextprotocol/sdk/client/stdio.js";
import path from "node:path";

const entry = path.resolve("node_modules/@ijoolaie/local-mcp/src/server.mjs");
const transport = new StdioClientTransport({
  command: process.execPath,
  args: [entry],
  env: { ...getDefaultEnvironment(), MCP_ALLOWED_ROOTS: process.cwd() }
});
const client = new Client({ name: "packed-local-mcp-acceptance", version: "0.1.0" });

try {
  await client.connect(transport);
  const { tools } = await client.listTools();
  const names = new Set(tools.map(tool => tool.name));
  for (const name of ["workspace_roots", "file_read", "start_process", "read_url", "pdf_info"]) {
    assert.ok(names.has(name), "Missing expected tool: " + name);
  }
  assert.equal(tools.length, 47);
  const result = await client.callTool({ name: "workspace_roots", arguments: {} });
  assert.notEqual(result.isError, true);
  process.stdout.write(JSON.stringify({ ok: true, toolCount: tools.length, workspaceRootsCall: true }) + "\n");
} finally {
  await client.close().catch(() => {});
}

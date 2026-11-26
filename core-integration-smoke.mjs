import assert from "node:assert/strict";
import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StdioClientTransport, getDefaultEnvironment } from "@modelcontextprotocol/sdk/client/stdio.js";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.dirname(fileURLToPath(import.meta.url));
const transport = new StdioClientTransport({
  command: process.execPath,
  args: [path.join(root, "server.mjs")],
  env: { ...getDefaultEnvironment(), MCP_ALLOWED_ROOTS: root }
});
const client = new Client({ name: "raya-core-integration-smoke", version: "0.1.0" });

try {
  await client.connect(transport);
  const { tools } = await client.listTools();
  const names = new Set(tools.map(tool => tool.name));
  for (const required of ["workspace_roots", "file_read", "file_write", "get_config", "list_directory"]) {
    assert.ok(names.has(required), `Missing expected Local MCP tool: ${required}`);
  }
  assert.ok(tools.length >= 40, `Unexpectedly small tool inventory: ${tools.length}`);
  process.stdout.write(JSON.stringify({
    ok: true,
    server: "Raya Local MCP through Raya MCP Core",
    toolCount: tools.length,
    requiredToolsPresent: true
  }, null, 2) + "\n");
} finally {
  await client.close().catch(() => {});
}

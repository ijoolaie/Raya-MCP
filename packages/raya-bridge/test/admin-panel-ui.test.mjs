import assert from "node:assert/strict";
import fs from "node:fs";
import test from "node:test";
import vm from "node:vm";
import { fileURLToPath } from "node:url";

const htmlPath = fileURLToPath(new URL("../src/admin-panel/index.html", import.meta.url));
const html = fs.readFileSync(htmlPath, "utf8");
const script = html.match(/<script>([\s\S]*?)<\/script>/)?.[1];

test("Admin Panel inline JavaScript parses and MCP management UI controls exist", () => {
  assert.ok(script, "inline script exists");
  assert.doesNotThrow(() => new vm.Script(script, { filename: "admin-panel-inline.js" }));
  for (const id of ["page-mcp", "mcpEndpoints", "mcpRefreshBtn", "mcpToolPanel", "mcpToolSelect", "mcpToolArguments", "mcpCallBtn"]) {
    assert.ok(html.includes('id="' + id + '"'), "missing UI element " + id);
  }
  for (const route of ["/mcp/endpoints", "/mcp/connect", "/mcp/disconnect", "/mcp/tools", "/mcp/call"]) {
    assert.ok(script.includes(route), "missing API route " + route);
  }
});

import test from "node:test";
import assert from "node:assert/strict";
import { RAYA_STACK_MANIFEST, getStackProfile, listStackProfiles } from "../src/index.mjs";

test("declares all components at the pinned compatible version", () => {
  assert.equal(RAYA_STACK_MANIFEST.stackVersion, "0.1.0");
  assert.deepEqual(Object.keys(RAYA_STACK_MANIFEST.components).sort(), ["@ijoolaie/bridge", "@ijoolaie/local-mcp", "@ijoolaie/mcp-core"]);
  for (const component of Object.values(RAYA_STACK_MANIFEST.components)) assert.equal(component.version, "0.1.0");
});

test("exposes the four supported component selection profiles", () => {
  assert.deepEqual(listStackProfiles().sort(), ["bridge-only", "core-only", "full-stack", "local-mcp-only"]);
  assert.deepEqual(getStackProfile("core-only").install, ["@ijoolaie/mcp-core"]);
  assert.deepEqual(getStackProfile("local-mcp-only").install, ["@ijoolaie/local-mcp"]);
  assert.deepEqual(getStackProfile("bridge-only").install, ["@ijoolaie/bridge"]);
  assert.deepEqual(getStackProfile("full-stack").install, ["@ijoolaie/stack"]);
  assert.throws(() => getStackProfile("unknown"), /Unknown Raya MCP Stack profile/);
});

test("component dependencies do not point to Local MCP from Core or Bridge", () => {
  assert.deepEqual(RAYA_STACK_MANIFEST.components["@ijoolaie/mcp-core"].dependencies, []);
  assert.deepEqual(RAYA_STACK_MANIFEST.components["@ijoolaie/bridge"].dependencies, ["@ijoolaie/mcp-core"]);
  assert.deepEqual(RAYA_STACK_MANIFEST.components["@ijoolaie/local-mcp"].dependencies, ["@ijoolaie/mcp-core"]);
});

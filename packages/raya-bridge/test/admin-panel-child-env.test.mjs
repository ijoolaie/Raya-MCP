import assert from "node:assert/strict";
import test from "node:test";
import { buildTunnelEnvironment } from "../src/admin-panel/child-env.mjs";

test("tunnel environment preserves required Windows runtime variables and its explicit API key", () => {
  const result = buildTunnelEnvironment({
    PATH: "C:\\Windows\\System32",
    SystemRoot: "C:\\Windows",
    USERPROFILE: "C:\\Users\\tester",
    CONTROL_PLANE_API_KEY: "tunnel-key",
    GITHUB_TOKEN: "github-secret",
    AWS_SECRET_ACCESS_KEY: "aws-secret",
    NPM_TOKEN: "npm-secret",
    NODE_OPTIONS: "--require attacker.js",
    MCP_BEARER_TOKEN: "mcp-secret"
  });

  assert.equal(result.PATH, "C:\\Windows\\System32");
  assert.equal(result.SystemRoot, "C:\\Windows");
  assert.equal(result.USERPROFILE, "C:\\Users\\tester");
  assert.equal(result.CONTROL_PLANE_API_KEY, "tunnel-key");
  for (const key of ["GITHUB_TOKEN", "AWS_SECRET_ACCESS_KEY", "NPM_TOKEN", "NODE_OPTIONS", "MCP_BEARER_TOKEN"]) {
    assert.equal(Object.hasOwn(result, key), false, key + " must not reach the tunnel process");
  }
});

test("tunnel environment omits missing and non-string values", () => {
  const result = buildTunnelEnvironment({
    PATH: 123,
    TEMP: "",
    CONTROL_PLANE_API_KEY: null
  });
  assert.deepEqual(result, {});
});

test("tunnel environment rejects invalid source values", () => {
  assert.throws(() => buildTunnelEnvironment(null), /environment object/);
  assert.throws(() => buildTunnelEnvironment([]), /environment object/);
});

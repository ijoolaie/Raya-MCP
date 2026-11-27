import test from "node:test";
import assert from "node:assert/strict";
import { buildChildEnvironment } from "./child-env.mjs";

test("preserves only the documented Windows runtime variables", () => {
  const source = {
    PATH: "C:\\Windows\\System32",
    SystemRoot: "C:\\Windows",
    WINDIR: "C:\\Windows",
    TEMP: "C:\\Temp",
    TMP: "C:\\Temp",
    USERPROFILE: "C:\\Users\\Raya",
    APPDATA: "C:\\Users\\Raya\\AppData\\Roaming",
    LOCALAPPDATA: "C:\\Users\\Raya\\AppData\\Local",
    ProgramFiles: "C:\\Program Files",
    "ProgramFiles(x86)": "C:\\Program Files (x86)",
    ProgramData: "C:\\ProgramData",
    ComSpec: "C:\\Windows\\System32\\cmd.exe",
    PROCESSOR_ARCHITECTURE: "AMD64",
    PATHEXT: ".COM;.EXE;.BAT;.CMD",
    CONTROL_PLANE_API_KEY: "must-not-pass",
    GITHUB_TOKEN: "must-not-pass",
    AWS_SECRET_ACCESS_KEY: "must-not-pass",
    NPM_TOKEN: "must-not-pass",
    NODE_OPTIONS: "--require C:\\temp\\inject.js",
    MCP_BEARER_TOKEN: "must-not-pass",
    MCP_ALLOWED_ROOTS: "C:\\untrusted",
    MCP_ENABLE_POWERSHELL: "true"
  };

  const result = buildChildEnvironment(source, {
    allowedRoots: "C:\\work\\local-mcp-bridge;C:\\work\\project",
    powershellEnabled: false
  });

  for (const key of [
    "PATH", "SystemRoot", "WINDIR", "TEMP", "TMP", "USERPROFILE",
    "APPDATA", "LOCALAPPDATA", "ProgramFiles", "ProgramFiles(x86)",
    "ProgramData", "ComSpec", "PROCESSOR_ARCHITECTURE", "PATHEXT"
  ]) assert.equal(result[key], source[key], key);

  for (const key of [
    "CONTROL_PLANE_API_KEY", "GITHUB_TOKEN", "AWS_SECRET_ACCESS_KEY",
    "NPM_TOKEN", "NODE_OPTIONS", "MCP_BEARER_TOKEN"
  ]) assert.equal(Object.hasOwn(result, key), false, key);

  assert.equal(result.MCP_ALLOWED_ROOTS, "C:\\work\\local-mcp-bridge;C:\\work\\project");
  assert.equal(result.MCP_ENABLE_POWERSHELL, "false");
  assert.equal(Object.hasOwn(result, "MCP_ALLOWED_ROOTS"), true);
});

test("sets the PowerShell gate only from the validated panel setting", () => {
  assert.equal(
    buildChildEnvironment({}, { allowedRoots: "C:\\work\\raya", powershellEnabled: true }).MCP_ENABLE_POWERSHELL,
    "true"
  );
  assert.equal(
    buildChildEnvironment({}, { allowedRoots: "C:\\work\\raya", powershellEnabled: "true" }).MCP_ENABLE_POWERSHELL,
    "false"
  );
});

test("rejects invalid environment input and missing workspace roots", () => {
  assert.throws(() => buildChildEnvironment(null, { allowedRoots: "C:\\work", powershellEnabled: false }), TypeError);
  assert.throws(() => buildChildEnvironment({}, { allowedRoots: "", powershellEnabled: false }), TypeError);
});

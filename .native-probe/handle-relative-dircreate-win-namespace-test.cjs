"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const addon = require("./build/Release/napi_probe_raya_phase1_dircreate_pinned.node");

const base = fs.mkdtempSync(path.join(__dirname, "fixture-dircreate-win-namespace-"));
const root = path.join(base, "root");
const parentPath = path.join(root, "parent");
fs.mkdirSync(parentPath, { recursive: true });
fs.writeFileSync(path.join(root, "valid.txt"), "namespace-path-policy");
let parent = null;

function mustReject(label, action, expectedText) {
  let error = null;
  try { action(); } catch (e) { error = String(e.message || e); }
  assert.ok(error, label + " was unexpectedly accepted");
  if (expectedText) assert.ok(error.includes(expectedText), label + " rejected for unexpected reason: " + error);
  return { label, rejected: true, reason: error };
}

try {
  const handleBaseline = addon.processHandleCount();

  // A normal drive-absolute root and one trailing separator are intentionally supported.
  parent = addon.openParentRelative(root, "parent");
  assert.equal(addon.closeParentHandle(parent), true);
  parent = null;
  parent = addon.openParentRelative(root + "\\", "parent");
  assert.equal(addon.closeParentHandle(parent), true);
  parent = null;
  const validTrailingRoot = addon.openRelative(root + "\\", "valid.txt", 0, 128);
  assert.equal(validTrailingRoot.opened, true);
  assert.equal(validTrailingRoot.content.toString(), "namespace-path-policy");

  const invalidRoots = [
    ["extended-length DOS namespace", "\\\\?\\" + root],
    ["Win32 device namespace", "\\\\.\\" + root],
    ["NT object-manager DOS namespace", "\\??\\" + root],
    ["UNC server-share root", "\\\\server\\share\\folder"],
    ["drive-relative root", root.replace(/^([A-Za-z]:)\\/, "$1")],
    ["forward-slash root", root.replace(/\\/g, "/")],
    ["non-drive root prefix", "1:\\invalid-root"],
    ["ADS in root component", root + ":stream"],
    ["repeated root separator", root + "\\\\child"],
    ["dot root component", root + "\\.\\child"],
    ["dot-dot root component", root + "\\..\\outside"]
  ];

  const rootResults = [];
  for (const [label, value] of invalidRoots) {
    rootResults.push(mustReject(label + " (openParentRelative)", () => addon.openParentRelative(value, "parent")));
    rootResults.push(mustReject(label + " (openRelative)", () => addon.openRelative(value, "valid.txt")));
  }

  const invalidRelative = [
    ["drive-relative component", "C:relative"],
    ["drive-absolute component", "C:\\Windows"],
    ["UNC component", "\\\\server\\share\\file"],
    ["extended-length DOS component", "\\\\?\\C:\\Windows"],
    ["Win32 device component", "\\\\.\\PhysicalDrive0"],
    ["NT object-manager component", "\\??\\C:\\Windows"],
    ["ADS stream component", "valid.txt:secret"],
    ["nested ADS component", "folder:stream\\file"],
    ["dot-dot traversal", "parent\\..\\outside"],
    ["mixed repeated separators", "parent\\//child"],
    ["leading slash", "\\rooted"],
    ["trailing separator", "parent\\"]
  ];

  const relativeResults = [];
  for (const [label, value] of invalidRelative) {
    relativeResults.push(mustReject(label + " (openParentRelative)", () => addon.openParentRelative(root, value)));
    relativeResults.push(mustReject(label + " (openRelative)", () => addon.openRelative(root, value)));
  }

  assert.equal(addon.processHandleCount(), handleBaseline + 0,
    "namespace/path rejection or successful root tests leaked a HANDLE");

  console.log(JSON.stringify({
    ok: true,
    validRootForms: ["drive-absolute", "drive-absolute with one trailing separator"],
    rejectedRootCases: invalidRoots.length,
    rejectedRootApiCalls: rootResults.length,
    rejectedRelativeCases: invalidRelative.length,
    rejectedRelativeApiCalls: relativeResults.length,
    noHandleLeak: true,
    notes: [
      "UNC, extended-length DOS, Win32 device, and NT object-manager namespace prefixes are deliberately unsupported by this drive-rooted prototype.",
      "A single trailing root separator is accepted; repeated separators and trailing separators in relative paths are rejected.",
      "The tests establish fail-closed behavior on this Windows host, not compatibility with every filesystem provider."
    ]
  }, null, 2));
} finally {
  if (parent) { try { addon.closeParentHandle(parent); } catch {} }
  fs.rmSync(base, { recursive: true, force: true });
}

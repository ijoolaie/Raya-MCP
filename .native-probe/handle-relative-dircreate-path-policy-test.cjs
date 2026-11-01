"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const addon = require("./build/Release/napi_probe_raya_phase1_dircreate_pinned.node");

const base = fs.mkdtempSync(path.join(__dirname, "fixture-dircreate-path-policy-"));
const root = path.join(base, "root");
fs.mkdirSync(path.join(root, "valid-parent"), { recursive: true });
let parent = null;
try {
  const baseline = addon.processHandleCount();
  parent = addon.openParentRelative(root, "valid-parent");
  const afterOpen = addon.processHandleCount();

  const relativeCases = [
    ["trailing dot", "bad."],
    ["trailing space", "bad "],
    ["reserved CON", "CON"],
    ["reserved NUL with extension", "NUL.txt"],
    ["reserved COM1", "COM1"],
    ["reserved COM superscript one", "COM¹"],
    ["reserved LPT superscript two", "LPT²"],
    ["reserved basename with space before extension", "CON .txt"],
    ["reserved AUX with extension", "AUX.log"],
    ["invalid angle bracket", "bad<name>"],
    ["invalid quote", 'bad"name'],
    ["nested reserved device", "valid-parent\\NUL"],
    ["nested trailing dot", "valid-parent\\bad."],
    ["ADS syntax", "valid-parent:stream"],
    ["absolute path", "\\\\rooted"],
    ["drive-relative path", "C:relative"],
    ["dot component", "."],
    ["dot-dot component", ".."],
    ["repeated separator", "valid-parent\\\\nested"]
  ];

  const relativeResults = relativeCases.map(([name, value]) => {
    let error = null;
    try { addon.openParentRelative(root, value); }
    catch (e) { error = String(e.message || e); }
    assert.ok(error, name + " was unexpectedly accepted");
    return { name, rejected: true, reason: error };
  });

  const openRelativeCases = [
    ["trailing dot", "bad."],
    ["trailing space", "bad "],
    ["reserved CON", "CON"],
    ["reserved NUL with extension", "NUL.txt"],
    ["reserved COM superscript one", "COM¹"],
    ["reserved LPT superscript two", "LPT²"],
    ["reserved basename with space before extension", "CON .txt"],
    ["reserved AUX with extension", "AUX.log"],
    ["invalid angle bracket", "bad<name>"],
    ["invalid quote", 'bad"name'],
    ["nested reserved device", "valid-parent\\NUL"],
    ["nested trailing dot", "valid-parent\\bad."]
  ];
  const openRelativeResults = openRelativeCases.map(([name, value]) => {
    let error = null;
    try { addon.openRelative(root, value); }
    catch (e) { error = String(e.message || e); }
    assert.ok(error && error.includes("unsafe component"), "openRelative " + name + " was not rejected by policy");
    return { name, rejected: true };
  });

  const rootCases = [
    ["root trailing dot", root + "\\bad."],
    ["root trailing space", root + "\\bad "],
    ["root reserved CON", root + "\\CON"],
    ["root reserved COM1", root + "\\COM1"],
    ["root invalid angle bracket", root + "\\bad<name>"]
  ];

  const rootResults = rootCases.map(([name, value]) => {
    let error = null;
    try { addon.openParentRelative(value, "valid-parent"); }
    catch (e) { error = String(e.message || e); }
    assert.ok(error && error.includes("unsafe root path component"), name + " was not rejected by policy");
    return { name, rejected: true };
  });

  const malformedRootCases = [
    ["UNC root", "\\\\server\\share"],
    ["drive-relative root", "C:relative\\root"],
    ["forward-slash root", "C:/Windows"],
    ["repeated root separator", "C:\\\\Windows"],
    ["dot-dot root component", "C:\\Windows\\..\\outside"],
    ["ADS root component", "C:\\folder:stream"]
  ];
  const malformedRootResults = malformedRootCases.map(([name, value]) => {
    let error = null;
    try { addon.openParentRelative(value, "valid-parent"); }
    catch (e) { error = String(e.message || e); }
    assert.ok(error, name + " was unexpectedly accepted");
    return { name, rejected: true, reason: error };
  });

  assert.equal(addon.processHandleCount(), afterOpen, "invalid path rejection leaked a HANDLE");
  addon.closeParentHandle(parent);
  parent = null;
  assert.equal(addon.processHandleCount(), baseline, "HANDLE count did not return to baseline");

  console.log(JSON.stringify({
    ok: true,
    relativeComponentPolicy: relativeResults,
    openRelativePolicy: openRelativeResults,
    rootComponentPolicy: rootResults,
    malformedRootPolicy: malformedRootResults,
    noHandleLeak: true,
    handleCountReturnedToBaseline: true
  }, null, 2));
} finally {
  if (parent) { try { addon.closeParentHandle(parent); } catch {} }
  fs.rmSync(base, { recursive: true, force: true });
}

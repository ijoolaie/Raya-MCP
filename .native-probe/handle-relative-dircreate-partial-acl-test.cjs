"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { execFileSync } = require("node:child_process");
const addon = require("./build/Release/napi_probe_raya_phase1_dircreate_pinned.node");

function currentSid() {
  const output = execFileSync("whoami", ["/user", "/fo", "csv", "/nh"], { encoding: "utf8" });
  const match = output.match(/S-1-[0-9]+-(?:[0-9]+-)*[0-9]+/);
  assert.ok(match, "could not determine current user SID");
  return match[0];
}
function icacls(args) {
  return execFileSync("icacls", args, { encoding: "utf8", stdio: ["ignore", "pipe", "pipe"] });
}

const base = fs.mkdtempSync(path.join(__dirname, "fixture-dircreate-partial-acl-"));
const root = path.join(base, "root");
const parentPath = path.join(root, "parent");
fs.mkdirSync(parentPath, { recursive: true });
let parent = null;
let denyApplied = false;
let firstExists = false;
let error = null;
let sid = null;
try {
  sid = currentSid();
  const baseline = addon.processHandleCount();
  parent = addon.openParentRelative(root, "parent");
  const afterOpen = addon.processHandleCount();

  // Inherit-only deny: creation of "first" is allowed in parent, but "first"
  // inherits an AD denial, so creation of "second" inside it fails.
  icacls([parentPath, "/deny", "*" + sid + ":(CI)(IO)(AD)"]);
  denyApplied = true;

  try { addon.createDirectoryPathRelative(parent, "first\\second"); }
  catch (e) { error = String(e.message || e); }

  assert.ok(error, "second-level create unexpectedly succeeded despite inherited AD deny");
  firstExists = fs.existsSync(path.join(parentPath, "first"));
  const secondExists = fs.existsSync(path.join(parentPath, "first", "second"));
  assert.equal(firstExists, true, "test did not create the expected first component");
  assert.equal(secondExists, false, "unexpected second component exists");
  assert.match(error, /partial_commit/i, "error did not explicitly disclose partial commit");
  assert.match(error, /1 directory component/i, "error did not report the committed component count");
  assert.equal(addon.processHandleCount(), afterOpen, "failed recursive create leaked a HANDLE");

  icacls([parentPath, "/remove:d", "*" + sid]);
  denyApplied = false;
  if (firstExists) icacls([path.join(parentPath, "first"), "/reset", "/T", "/C"]);

  addon.closeParentHandle(parent);
  parent = null;
  assert.equal(addon.processHandleCount(), baseline, "HANDLE count did not return to baseline");

  fs.rmSync(base, { recursive: true, force: true });
  assert.equal(fs.existsSync(base), false, "disposable ACL fixture was not removed");

  console.log(JSON.stringify({
    ok: true,
    secondLevelPermissionDenied: true,
    firstComponentRemains: firstExists,
    secondComponentAbsent: true,
    errorExplicitlyReportsPartialCommit: true,
    committedComponentCount: 1,
    aclRestored: true,
    handleCountReturnedToBaseline: true,
    fixtureRemoved: true
  }, null, 2));
} finally {
  if (denyApplied && sid) {
    try { icacls([parentPath, "/remove:d", "*" + sid]); } catch {}
    const first = path.join(parentPath, "first");
    if (fs.existsSync(first)) {
      try { icacls([first, "/reset", "/T", "/C"]); } catch {}
    }
  }
  if (parent) { try { addon.closeParentHandle(parent); } catch {} }
  fs.rmSync(base, { recursive: true, force: true });
}

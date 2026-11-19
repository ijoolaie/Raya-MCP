"use strict";
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { execFileSync } = require("node:child_process");
const addon = require("./build/Release/napi_probe_raya_phase1_hardlink_guard.node");

function run(exe, args) {
  return execFileSync(exe, args, {
    encoding: "utf8", windowsHide: true, stdio: ["ignore", "pipe", "pipe"]
  });
}
function currentSid() {
  const out = run("whoami.exe", ["/user", "/fo", "csv", "/nh"]);
  const match = out.match(/"(S-1-[0-9-]+)"/);
  if (!match) throw new Error("Could not parse current user SID: " + out);
  return match[1];
}
function denied(error) {
  return /0xC0000022|STATUS_ACCESS_DENIED|access is denied/i.test(String(error && error.message));
}

const fixture = fs.mkdtempSync(path.join(os.tmpdir(), "raya-ra-diag-"));
const root = path.join(fixture, "root");
const parentPath = path.join(root, "parent");
const filePath = path.join(parentPath, "metadata-canary.txt");
const sid = currentSid();
const baseline = addon.processHandleCount();
let parent = null;
let denyApplied = false;
const result = {
  userSid: sid,
  processHandleBaseline: baseline,
  fixtureVolume: path.parse(fixture).root,
  scope: "disposable temp file only; no workspace/global ACL changes"
};

try {
  fs.mkdirSync(parentPath, { recursive: true });
  fs.writeFileSync(filePath, "METADATA-ACL-DIAGNOSTIC", { flag: "wx" });
  parent = addon.openParentRelative(root, "parent");
  result.afterParentOpenHandleCount = addon.processHandleCount();

  try {
    result.before = addon.inspectChildFromParent(parent, "metadata-canary.txt");
    result.beforeSucceeded = true;
  } catch (e) {
    result.beforeSucceeded = false;
    result.beforeError = String(e && e.message || e);
  }
  result.afterBeforeReadHandleCount = addon.processHandleCount();

  run("icacls.exe", [filePath, "/deny", "*" + sid + ":(RA)"]);
  denyApplied = true;
  result.exactAclListing = run("icacls.exe", [filePath]).trim();
  result.denyLineMatchesSidAndRA = result.exactAclListing
    .split(/\r?\n/)
    .some(line => line.includes(sid) && /DENY/i.test(line) && /\(RA\)/i.test(line));

  try {
    result.during = addon.inspectChildFromParent(parent, "metadata-canary.txt");
    result.duringSucceeded = true;
  } catch (e) {
    result.duringSucceeded = false;
    result.duringError = String(e && e.message || e);
    result.duringWasAccessDenied = denied(e);
  }
  result.afterDuringReadHandleCount = addon.processHandleCount();

  addon.closeParentHandle(parent);
  parent = null;
  result.afterParentCloseHandleCount = addon.processHandleCount();

  run("icacls.exe", [filePath, "/remove:d", "*" + sid]);
  denyApplied = false;
  result.aclRestored = true;

  parent = addon.openParentRelative(root, "parent");
  try {
    result.afterRestore = addon.inspectChildFromParent(parent, "metadata-canary.txt");
    result.afterRestoreSucceeded = true;
  } catch (e) {
    result.afterRestoreSucceeded = false;
    result.afterRestoreError = String(e && e.message || e);
  }
  addon.closeParentHandle(parent);
  parent = null;
  result.finalHandleCount = addon.processHandleCount();
  result.handleDeltaFromBaseline = result.finalHandleCount - baseline;
  result.handleCountReturnedToBaseline = result.handleDeltaFromBaseline === 0;
} catch (e) {
  result.fatalError = String(e && e.stack || e);
} finally {
  if (parent) {
    try { addon.closeParentHandle(parent); } catch (e) {
      result.parentCloseError = String(e && e.message || e);
    }
  }
  if (denyApplied) {
    try {
      run("icacls.exe", [filePath, "/remove:d", "*" + sid]);
      result.aclRestoredInFinally = true;
    } catch (e) {
      result.aclRestoreError = String(e && e.message || e);
    }
  }
  try {
    fs.rmSync(fixture, { recursive: true, force: true });
    result.fixtureRemoved = true;
  } catch (e) {
    result.fixtureCleanupError = String(e && e.message || e);
  }
  result.handleCountAfterCleanup = addon.processHandleCount();
  result.cleanupHandleDelta = result.handleCountAfterCleanup - baseline;
  console.log(JSON.stringify(result, null, 2));
}

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

const fixture = fs.mkdtempSync(path.join(os.tmpdir(), "raya-accesscheck-"));
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
  scope: "one disposable temp NTFS file; explicit user SID RA deny; no workspace/global ACL changes"
};

try {
  fs.mkdirSync(parentPath, { recursive: true });
  fs.writeFileSync(filePath, "ACCESS-CHECK-ACL-CANARY", { flag: "wx" });
  parent = addon.openParentRelative(root, "parent");
  result.before = addon.inspectChildFromParent(parent, "metadata-canary.txt");

  run("icacls.exe", [filePath, "/deny", "*" + sid + ":(RA)"]);
  denyApplied = true;
  result.exactAclListing = run("icacls.exe", [filePath]).trim();

  const psOutput = run("powershell.exe", [
    "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
    "-File", path.join(__dirname, "metadata-acl-accesscheck-20261102.ps1"),
    filePath
  ]);
  result.accessCheckAndGrantedHandles = JSON.parse(psOutput.trim());

  try {
    result.nativeAdapter = addon.inspectChildFromParent(parent, "metadata-canary.txt");
    result.nativeAdapterSucceeded = true;
  } catch (e) {
    result.nativeAdapterSucceeded = false;
    result.nativeAdapterError = String(e && e.message || e);
  }

  addon.closeParentHandle(parent);
  parent = null;
  run("icacls.exe", [filePath, "/remove:d", "*" + sid]);
  denyApplied = false;
  result.aclRestored = true;
  parent = addon.openParentRelative(root, "parent");
  result.afterRestore = addon.inspectChildFromParent(parent, "metadata-canary.txt");
  addon.closeParentHandle(parent);
  parent = null;
} catch (e) {
  result.fatalError = String(e && e.stack || e);
} finally {
  if (parent) {
    try { addon.closeParentHandle(parent); } catch (e) { result.parentCloseError = String(e && e.message || e); }
  }
  if (denyApplied) {
    try {
      run("icacls.exe", [filePath, "/remove:d", "*" + sid]);
      result.aclRestoredInFinally = true;
    } catch (e) { result.aclRestoreError = String(e && e.message || e); }
  }
  try { fs.rmSync(fixture, { recursive: true, force: true }); result.fixtureRemoved = true; }
  catch (e) { result.fixtureCleanupError = String(e && e.message || e); }
  result.finalHandleCount = addon.processHandleCount();
  result.handleDeltaFromBaseline = result.finalHandleCount - baseline;
  result.handleCountReturnedToBaseline = result.handleDeltaFromBaseline === 0;
  console.log(JSON.stringify(result, null, 2));
}

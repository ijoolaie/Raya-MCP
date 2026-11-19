"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { execFileSync } = require("node:child_process");
const addon = require("./build/Release/napi_probe_raya_phase1_hardlink_guard.node");

function icacls(target, ...args) {
  return execFileSync("icacls.exe", [target, ...args], {
    encoding: "utf8", windowsHide: true, stdio: ["ignore", "pipe", "pipe"]
  });
}
function currentSid() {
  const out = execFileSync("whoami.exe", ["/user", "/fo", "csv", "/nh"], {
    encoding: "utf8", windowsHide: true
  });
  const match = out.match(/"(S-1-[0-9-]+)"/);
  if (!match) throw new Error("Could not parse current user SID: " + out);
  return match[1];
}
function isAccessDenied(error) {
  return /0xC0000022|STATUS_ACCESS_DENIED|access is denied/i.test(String(error && error.message));
}

const fixture = fs.mkdtempSync(path.join(os.tmpdir(), "raya-medium-ra-acl-"));
const root = path.join(fixture, "root");
const parentPath = path.join(root, "parent");
const filePath = path.join(parentPath, "metadata-canary.txt");
const sid = currentSid();
const baseline = addon.processHandleCount();
let parent = null;
let denyApplied = false;

try {
  fs.mkdirSync(parentPath, { recursive: true });
  fs.writeFileSync(filePath, "MEDIUM-RA-ACL-CANARY", { flag: "wx" });

  parent = addon.openParentRelative(root, "parent");
  const before = addon.inspectChildFromParent(parent, "metadata-canary.txt");

  icacls(filePath, "/deny", "*" + sid + ":(RA)");
  denyApplied = true;
  const aclListing = icacls(filePath);
  let denied = false;
  let observedError = null;
  let during = null;
  try {
    during = addon.inspectChildFromParent(parent, "metadata-canary.txt");
  } catch (error) {
    denied = isAccessDenied(error);
    observedError = String(error && error.message || error);
    if (!denied) throw error;
  }

  addon.closeParentHandle(parent);
  parent = null;
  icacls(filePath, "/remove:d", "*" + sid);
  denyApplied = false;

  parent = addon.openParentRelative(root, "parent");
  const after = addon.inspectChildFromParent(parent, "metadata-canary.txt");
  addon.closeParentHandle(parent);
  parent = null;

  assert.equal(after.isRegularFile, true);
  assert.equal(addon.processHandleCount(), baseline, "native HANDLE count must return to baseline");

  console.log(JSON.stringify({
    ok: true,
    userSid: sid,
    aclListingContainsDeny: /DENY/i.test(aclListing),
    beforeAclMetadataReadSucceeded: !!before.isRegularFile,
    metadataReadDeniedUnderExplicitReadAttributesDeny: denied,
    metadataReadSucceededUnderExplicitReadAttributesDeny: !!during,
    metadataReadError: observedError,
    metadataReadSucceededAfterAclRestore: !!after.isRegularFile,
    handleCountReturnedToBaseline: true,
    scope: "one disposable temp NTFS file; explicit deny ACE removed; no workspace/global ACL changed"
  }, null, 2));
} finally {
  if (parent) { try { addon.closeParentHandle(parent); } catch {} }
  if (denyApplied) {
    try { icacls(filePath, "/remove:d", "*" + sid); } catch {}
  }
  fs.rmSync(fixture, { recursive: true, force: true });
}

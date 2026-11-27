"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { execFileSync } = require("node:child_process");
const addon = require("./build/Release/napi_probe_raya_phase1_dircreate_pinned.node");

function runIcacls(target, ...args) {
  return execFileSync("icacls.exe", [target, ...args], { encoding: "utf8", windowsHide: true });
}

function currentSid() {
  const output = execFileSync("whoami.exe", ["/user", "/fo", "csv", "/nh"], { encoding: "utf8", windowsHide: true });
  const match = output.match(/"(S-1-[0-9-]+)"/);
  if (!match) throw new Error("Could not parse current Windows user SID from whoami output: " + output);
  return match[1];
}

function isAccessDenied(error) {
  return /0xC0000022|STATUS_ACCESS_DENIED/i.test(String(error && error.message));
}

const fixture = fs.mkdtempSync(path.join(os.tmpdir(), "raya-live-acl-denial-"));
const root = path.join(fixture, "root");
const parentPath = path.join(root, "parent");
const protectedFile = path.join(parentPath, "protected.txt");
const sid = currentSid();
const baseline = addon.processHandleCount();
let parent = null;
let parentDenyApplied = false;
let fileDenyApplied = false;

try {
  fs.mkdirSync(parentPath, { recursive: true });
  fs.writeFileSync(protectedFile, "ACL-READ-CANARY", { flag: "wx" });

  // Deny file creation and subdirectory creation on this disposable directory,
  // and deny read-data on one disposable file. No global or user-profile ACLs
  // are modified.
  runIcacls(parentPath, "/deny", "*" + sid + ":(WD,AD)");
  parentDenyApplied = true;
  runIcacls(protectedFile, "/deny", "*" + sid + ":(RD)");
  fileDenyApplied = true;

  parent = addon.openParentRelative(root, "parent");

  let createFileDenied = false;
  let createFileError = null;
  try {
    addon.createChildExclusive(parent, "must-not-exist.txt");
  } catch (error) {
    createFileDenied = isAccessDenied(error);
    createFileError = error.message;
    if (!createFileDenied) throw error;
  }
  assert.equal(createFileDenied, true, "live DACL must deny FILE_CREATE / FILE_ADD_FILE");
  assert.equal(fs.existsSync(path.join(parentPath, "must-not-exist.txt")), false);

  let createDirectoryDenied = false;
  let createDirectoryError = null;
  try {
    addon.createDirectoryChildExclusive(parent, "must-not-exist-dir");
  } catch (error) {
    createDirectoryDenied = isAccessDenied(error);
    createDirectoryError = error.message;
    if (!createDirectoryDenied) throw error;
  }
  assert.equal(createDirectoryDenied, true, "live DACL must deny directory creation / FILE_ADD_SUBDIRECTORY");
  assert.equal(fs.existsSync(path.join(parentPath, "must-not-exist-dir")), false);

  let readDenied = false;
  let readError = null;
  try {
    addon.readChildFromParent(parent, "protected.txt", 1024);
  } catch (error) {
    readDenied = isAccessDenied(error);
    readError = error.message;
    if (!readDenied) throw error;
  }
  assert.equal(readDenied, true, "live DACL must deny FILE_READ_DATA through the held parent HANDLE");

  addon.closeParentHandle(parent);
  parent = null;

  // Remove only the explicit deny ACEs applied to this disposable fixture.
  runIcacls(protectedFile, "/remove:d", "*" + sid);
  fileDenyApplied = false;
  runIcacls(parentPath, "/remove:d", "*" + sid);
  parentDenyApplied = false;

  parent = addon.openParentRelative(root, "parent");
  const read = addon.readChildFromParent(parent, "protected.txt", 1024);
  assert.equal(read.content.toString(), "ACL-READ-CANARY");
  const createdFile = addon.createChildExclusive(parent, "allowed-after-restore.txt");
  assert.deepEqual(createdFile, { created: true, size: 0 });
  const createdDirectory = addon.createDirectoryChildExclusive(parent, "allowed-dir-after-restore");
  assert.equal(createdDirectory.created, true);

  addon.closeParentHandle(parent);
  parent = null;
  assert.equal(addon.processHandleCount(), baseline, "all native HANDLEs must return to baseline");

  console.log(JSON.stringify({
    ok: true,
    sid,
    createFileDeniedByLiveDacl: createFileDenied,
    createFileError,
    createDirectoryDeniedByLiveDacl: createDirectoryDenied,
    createDirectoryError,
    readDeniedByLiveDacl: readDenied,
    readError,
    permissionsRestored: true,
    readAndCreateSucceededAfterRestore: true,
    handleCountReturnedToBaseline: true,
    scope: "temporary fixture ACLs only; no global/user-profile ACLs changed"
  }, null, 2));
} finally {
  if (parent) {
    try { addon.closeParentHandle(parent); } catch {}
  }
  // Best-effort restoration before fixture cleanup if any assertion failed.
  try { if (fileDenyApplied) runIcacls(protectedFile, "/remove:d", "*" + sid); } catch {}
  try { if (parentDenyApplied) runIcacls(parentPath, "/remove:d", "*" + sid); } catch {}
  fs.rmSync(fixture, { recursive: true, force: true });
}

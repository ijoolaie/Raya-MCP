"use strict";
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { execFileSync } = require("node:child_process");
const addon = require("./build/Release/napi_probe_raya_phase1_rootapi_acl_diag.node");
function run(command, args) {
  return execFileSync(command, args, { encoding: "utf8", windowsHide: true });
}
function currentSid() {
  const out = run("whoami.exe", ["/user", "/fo", "csv", "/nh"]);
  const m = out.match(/"(S-1-[0-9-]+)"/);
  if (!m) throw new Error("Could not parse current user SID: " + out);
  return m[1];
}
function capture(fn) {
  try { return { succeeded: true, value: fn(), error: null }; }
  catch (e) { return { succeeded: false, value: null, error: String(e && e.message || e) }; }
}
const fixture = fs.mkdtempSync(path.join(os.tmpdir(), "raya-acl-handle-grant-"));
const sid = currentSid();
const baseline = addon.processHandleCount();
let parent = null, denyApplied = false, filePath = null;
try {
  const root = path.join(fixture, "root");
  const parentPath = path.join(root, "parent");
  filePath = path.join(parentPath, "canary.txt");
  fs.mkdirSync(parentPath, { recursive: true });
  fs.writeFileSync(filePath, "HANDLE-GRANT-CANARY", { flag: "wx" });
  parent = addon.openParentRelative(root, "parent");
  const before = capture(() => addon.inspectEntryMetadata(parent, "canary.txt"));
  run("icacls.exe", [filePath, "/deny", "*" + sid + ":(RA)"]);
  denyApplied = true;
  const acl = run("icacls.exe", [filePath]);
  const denyLine = acl.split(/\r?\n/).find(line => /\(DENY\).*\(RA\)/i.test(line)) || null;
  const during = capture(() => addon.inspectEntryMetadata(parent, "canary.txt"));
  const afterRead = capture(() => addon.readChildFromParent(parent, "canary.txt", 1024));
  addon.closeParentHandle(parent); parent = null;
  run("icacls.exe", [filePath, "/remove:d", "*" + sid]); denyApplied = false;
  parent = addon.openParentRelative(root, "parent");
  const afterRestore = capture(() => addon.inspectEntryMetadata(parent, "canary.txt"));
  addon.closeParentHandle(parent); parent = null;
  const finalCount = addon.processHandleCount();
  console.log(JSON.stringify({
    ok: !!denyLine && during.succeeded && afterRestore.succeeded && finalCount === baseline,
    userSid: sid,
    denyAceLine: denyLine,
    before: before.succeeded ? {
      grantedAccess: "0x" + before.value.diagnosticGrantedAccess.toString(16).padStart(8,"0"),
      ntQueryObjectStatus: "0x" + before.value.diagnosticNtQueryObjectStatus.toString(16).padStart(8,"0")
    } : before,
    during: during.succeeded ? {
      grantedAccess: "0x" + during.value.diagnosticGrantedAccess.toString(16).padStart(8,"0"),
      ntQueryObjectStatus: "0x" + during.value.diagnosticNtQueryObjectStatus.toString(16).padStart(8,"0"),
      metadataReturned: true,
      numberOfLinks: during.value.numberOfLinks,
      size: during.value.size
    } : during,
    contentReadWhileDenyRA: afterRead.succeeded ? {
      succeeded: true, content: afterRead.value.content.toString()
    } : afterRead,
    afterRestoreMetadataSucceeded: afterRestore.succeeded,
    handleCountBaseline: baseline,
    handleCountFinal: finalCount,
    handleCountReturnedToBaseline: finalCount === baseline,
    scope: "disposable NTFS file; diagnostic clone of exact rootapi addon; NtQueryObject ObjectBasicInformation on the internal metadata HANDLE; temporary explicit deny removed before cleanup"
  }, null, 2));
} finally {
  if (parent) { try { addon.closeParentHandle(parent); } catch {} }
  if (denyApplied && filePath) { try { run("icacls.exe", [filePath, "/remove:d", "*" + sid]); } catch {} }
  fs.rmSync(fixture, { recursive: true, force: true });
}

"use strict";
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { execFileSync } = require("node:child_process");
const addon = require("./build/Release/napi_probe_raya_phase1_rootapi.node");

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
const fixture = fs.mkdtempSync(path.join(os.tmpdir(), "raya-real-metadata-acl-"));
const sid = currentSid();
const baseline = addon.processHandleCount();
const results = [];
try {
  const root = path.join(fixture, "root");
  const parentPath = path.join(root, "parent");
  const filePath = path.join(parentPath, "canary.txt");
  fs.mkdirSync(parentPath, { recursive: true });
  fs.writeFileSync(filePath, "REAL-METADATA-CANARY", { flag: "wx" });

  let parent = null;
  let denyApplied = false;
  try {
    // Use the same root/parent handle APIs and metadata function as the adapter.
    parent = addon.openParentRelative(root, "parent");
    const before = capture(() => addon.inspectEntryMetadata(parent, "canary.txt"));

    run("icacls.exe", [filePath, "/deny", "*" + sid + ":(RA)"]);
    denyApplied = true;
    const acl = run("icacls.exe", [filePath]);
    const denyLine = acl.split(/\r?\n/).find(line => /\(DENY\).*\(RA\)/i.test(line)) || null;

    const duringMetadata = capture(() => addon.inspectEntryMetadata(parent, "canary.txt"));
    const duringContent = capture(() => addon.readChildFromParent(parent, "canary.txt", 1024));

    addon.closeParentHandle(parent);
    parent = null;
    run("icacls.exe", [filePath, "/remove:d", "*" + sid]);
    denyApplied = false;

    parent = addon.openParentRelative(root, "parent");
    const afterMetadata = capture(() => addon.inspectEntryMetadata(parent, "canary.txt"));
    const afterContent = capture(() => addon.readChildFromParent(parent, "canary.txt", 1024));

    results.push({
      denyAceLine: denyLine,
      denyAceVerified: !!denyLine,
      beforeMetadataSucceeded: before.succeeded,
      duringMetadataSucceeded: duringMetadata.succeeded,
      duringMetadataError: duringMetadata.error,
      duringContentReadSucceeded: duringContent.succeeded,
      duringContentReadError: duringContent.error,
      duringContent: duringContent.succeeded ? duringContent.value.content.toString() : null,
      afterRestoreMetadataSucceeded: afterMetadata.succeeded,
      afterRestoreContentSucceeded: afterContent.succeeded,
      afterRestoreContentMatches: afterContent.succeeded &&
        afterContent.value.content.toString() === "REAL-METADATA-CANARY",
      metadataFields: duringMetadata.succeeded ? Object.keys(duringMetadata.value) : []
    });

    addon.closeParentHandle(parent);
    parent = null;
  } finally {
    if (parent) { try { addon.closeParentHandle(parent); } catch {} }
    if (denyApplied) { try { run("icacls.exe", [filePath, "/remove:d", "*" + sid]); } catch {} }
  }

  const finalCount = addon.processHandleCount();
  console.log(JSON.stringify({
    ok: results.length === 1 && results[0].denyAceVerified &&
      results[0].afterRestoreMetadataSucceeded && results[0].afterRestoreContentMatches &&
      finalCount === baseline,
    userSid: sid,
    handleCountBaseline: baseline,
    handleCountFinal: finalCount,
    handleCountReturnedToBaseline: finalCount === baseline,
    results,
    scope: "one disposable temporary NTFS file; exact rootapi addon openParentRelative + inspectEntryMetadata; explicit deny ACE removed before fixture cleanup; no workspace or persistent ACL changed"
  }, null, 2));
} finally {
  fs.rmSync(fixture, { recursive: true, force: true });
}

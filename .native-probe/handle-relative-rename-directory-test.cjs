"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { execFileSync } = require("node:child_process");
const roots = require("./build/Release/napi_probe_raya_phase1_rootapi.node");
const rename = require("./build/Release/napi_probe_raya_phase1_rename_directory.node");

function icacls(target, ...args) { return execFileSync("icacls.exe", [target, ...args], { encoding: "utf8", windowsHide: true }); }
function currentSid() {
  const output = execFileSync("whoami.exe", ["/user", "/fo", "csv", "/nh"], { encoding: "utf8", windowsHide: true });
  const match = output.match(/"(S-1-[0-9-]+)"/);
  if (!match) throw new Error("Unable to identify current user SID");
  return match[1];
}
function denied(error) { return /0xC0000022|STATUS_ACCESS_DENIED/i.test(String(error?.message || error)); }

const fixture = fs.mkdtempSync(path.join(os.tmpdir(), "raya-dir-rename-"));
const rootPath = path.join(fixture, "root");
const sourcePath = path.join(rootPath, "source");
const destinationPath = path.join(rootPath, "destination");
const outside = path.join(fixture, "outside");
const sid = currentSid();
const baseline = roots.processHandleCount();
let sourceParent = null, destinationParent = null, crossParent = null;
let sourceDeleteDeny = false, destinationAddDeny = false;
let destinationSwapped = false, sourceSwapped = false;
let crossRoot = null;
const checks = [];

try {
  fs.mkdirSync(sourcePath, { recursive: true });
  fs.mkdirSync(destinationPath, { recursive: true });
  fs.mkdirSync(outside, { recursive: true });
  sourceParent = roots.openParentRelative(rootPath, "source");
  destinationParent = roots.openParentRelative(rootPath, "destination");
  fs.mkdirSync(path.join(sourcePath, "empty-dir"));
  const empty = rename.renameChildRelative(
    sourceParent, "empty-dir", destinationParent, "empty-moved"
  );
  assert.equal(empty.committed, true);
  assert.equal(empty.sourceIsDirectory, true);
  assert.equal(fs.statSync(path.join(destinationPath, "empty-moved")).isDirectory(), true);
  assert.equal(fs.existsSync(path.join(sourcePath, "empty-dir")), false);
  checks.push("empty directory rename succeeds");

  fs.mkdirSync(path.join(sourcePath, "tree", "nested"), { recursive: true });
  fs.writeFileSync(path.join(sourcePath, "tree", "nested", "payload.txt"), "DIRECTORY-TREE-CONTENT");
  const movedTree = rename.renameChildRelative(sourceParent, "tree", destinationParent, "tree-moved");
  assert.equal(movedTree.committed, true);
  assert.equal(movedTree.sourceIsDirectory, true);
  assert.equal(fs.readFileSync(path.join(destinationPath, "tree-moved", "nested", "payload.txt"), "utf8"), "DIRECTORY-TREE-CONTENT");
  assert.equal(fs.existsSync(path.join(sourcePath, "tree")), false);
  checks.push("non-empty directory tree moves between distinct held parent HANDLEs");

  fs.mkdirSync(path.join(sourcePath, "collision-dir"));
  fs.mkdirSync(path.join(destinationPath, "collision-dir"));
  assert.throws(() => rename.renameChildRelative(sourceParent, "collision-dir", destinationParent, "collision-dir"));
  assert.equal(fs.statSync(path.join(sourcePath, "collision-dir")).isDirectory(), true);
  assert.equal(fs.statSync(path.join(destinationPath, "collision-dir")).isDirectory(), true);
  checks.push("existing destination directory is not overwritten");

  fs.mkdirSync(path.join(sourcePath, "file-collision-dir"));
  fs.writeFileSync(path.join(destinationPath, "file-collision-dir"), "DESTINATION-FILE");
  assert.throws(() => rename.renameChildRelative(sourceParent, "file-collision-dir", destinationParent, "file-collision-dir"));
  assert.equal(fs.statSync(path.join(sourcePath, "file-collision-dir")).isDirectory(), true);
  assert.equal(fs.readFileSync(path.join(destinationPath, "file-collision-dir"), "utf8"), "DESTINATION-FILE");
  checks.push("directory source cannot overwrite destination file");

  fs.mkdirSync(path.join(outside, "outside-dir"));
  fs.writeFileSync(path.join(outside, "outside-dir", "secret.txt"), "OUTSIDE-DIRECTORY-SECRET");
  let directoryJunctionCreated = false;
  try { fs.symlinkSync(path.join(outside, "outside-dir"), path.join(sourcePath, "directory-link"), "junction"); directoryJunctionCreated = true; } catch {}
  if (directoryJunctionCreated) {
    assert.throws(() => rename.renameChildRelative(sourceParent, "directory-link", destinationParent, "directory-link-moved"));
    assert.equal(fs.readFileSync(path.join(outside, "outside-dir", "secret.txt"), "utf8"), "OUTSIDE-DIRECTORY-SECRET");
    assert.equal(fs.lstatSync(path.join(sourcePath, "directory-link")).isSymbolicLink(), true);
    checks.push("directory reparse leaf is rejected without following or moving target");
  } else {
    checks.push("directory reparse test skipped: host did not permit junction creation");
  }

  assert.throws(() => rename.renameChildRelative(sourceParent, "missing-dir", destinationParent, "missing-dir"));
  checks.push("missing directory source fails closed");
  for (const bad of ["", ".", "..", "nested\\leaf", "C:leaf", "trailing.", "trailing ", "CON", "NUL.txt"]) {
    assert.throws(() => rename.renameChildRelative(sourceParent, bad, destinationParent, "valid-dir"));
    assert.throws(() => rename.renameChildRelative(sourceParent, "valid-source", destinationParent, bad));
  }
  checks.push("unsafe source and destination leaf names rejected");

  // Directory-specific ACL rights: DELETE on source and FILE_ADD_SUBDIRECTORY on destination.
  fs.mkdirSync(path.join(sourcePath, "deny-destination"));
  fs.mkdirSync(path.join(sourcePath, "deny-source"));
  fs.writeFileSync(path.join(sourcePath, "deny-source", "marker.txt"), "ACL-DIRECTORY-SOURCE");
  const destinationAcl = icacls(destinationPath, "/deny", "*" + sid + ":(AD)");
  destinationAddDeny = true;
  assert.match(destinationAcl, /Successfully processed|processed file/i);
  assert.match(icacls(destinationPath), /\(DENY\)\(AD\)/);
  let destinationDenied = false;
  try { rename.renameChildRelative(sourceParent, "deny-destination", destinationParent, "blocked-dir"); }
  catch (error) { destinationDenied = denied(error); if (!destinationDenied) throw error; }
  assert.equal(destinationDenied, true, "destination FILE_ADD_SUBDIRECTORY denial must block directory rename");
  assert.equal(fs.statSync(path.join(sourcePath, "deny-destination")).isDirectory(), true);
  assert.equal(fs.existsSync(path.join(destinationPath, "blocked-dir")), false);
  icacls(destinationPath, "/remove:d", "*" + sid); destinationAddDeny = false;
  rename.renameChildRelative(sourceParent, "deny-destination", destinationParent, "allowed-after-destination-restore");
  assert.equal(fs.statSync(path.join(destinationPath, "allowed-after-destination-restore")).isDirectory(), true);
  checks.push("destination FILE_ADD_SUBDIRECTORY denial enforced and rename succeeds after restore");

  const sourceDeniedPath = path.join(sourcePath, "deny-source");
  const sourceAcl = icacls(sourceDeniedPath, "/deny", "*" + sid + ":(D)");
  sourceDeleteDeny = true;
  assert.match(sourceAcl, /Successfully processed|processed file/i);
  assert.match(icacls(sourceDeniedPath), /\(DENY\)\(D\)/);
  let sourceDenied = false;
  try { rename.renameChildRelative(sourceParent, "deny-source", destinationParent, "blocked-source-dir"); }
  catch (error) { sourceDenied = denied(error); if (!sourceDenied) throw error; }
  assert.equal(sourceDenied, true, "source directory DELETE denial must block rename");
  assert.equal(fs.existsSync(sourceDeniedPath), true);
  assert.equal(fs.existsSync(path.join(destinationPath, "blocked-source-dir")), false);
  icacls(sourceDeniedPath, "/remove:d", "*" + sid); sourceDeleteDeny = false;
  assert.equal(fs.readFileSync(path.join(sourceDeniedPath, "marker.txt"), "utf8"), "ACL-DIRECTORY-SOURCE");
  rename.renameChildRelative(sourceParent, "deny-source", destinationParent, "allowed-after-source-restore");
  assert.equal(fs.readFileSync(path.join(destinationPath, "allowed-after-source-restore", "marker.txt"), "utf8"), "ACL-DIRECTORY-SOURCE");
  checks.push("source directory DELETE denial enforced and rename succeeds after restore");

  // The held destination parent HANDLE must not be redirected by a path swap.
  roots.closeParentHandle(sourceParent); sourceParent = null;
  roots.closeParentHandle(destinationParent); destinationParent = null;
  fs.renameSync(destinationPath, path.join(rootPath, "destination-held"));
  fs.symlinkSync(outside, destinationPath, "junction");
  destinationSwapped = true;
  destinationParent = roots.openParentRelative(rootPath, "destination-held");
  sourceParent = roots.openParentRelative(rootPath, "source");
  fs.mkdirSync(path.join(sourcePath, "swap-destination"));
  rename.renameChildRelative(sourceParent, "swap-destination", destinationParent, "swap-destination-moved");
  assert.equal(fs.statSync(path.join(rootPath, "destination-held", "swap-destination-moved")).isDirectory(), true);
  assert.equal(fs.existsSync(path.join(outside, "swap-destination-moved")), false);
  checks.push("held destination directory HANDLE survives path-to-junction replacement");

  roots.closeParentHandle(sourceParent); sourceParent = null;
  roots.closeParentHandle(destinationParent); destinationParent = null;
  fs.rmdirSync(destinationPath);
  destinationSwapped = false;
  fs.renameSync(sourcePath, path.join(rootPath, "source-held"));
  fs.symlinkSync(outside, sourcePath, "junction");
  sourceSwapped = true;
  sourceParent = roots.openParentRelative(rootPath, "source-held");
  destinationParent = roots.openWorkspaceRoot(rootPath);
  fs.mkdirSync(path.join(rootPath, "source-held", "swap-source"));
  rename.renameChildRelative(sourceParent, "swap-source", destinationParent, "swap-source-moved");
  assert.equal(fs.statSync(path.join(rootPath, "swap-source-moved")).isDirectory(), true);
  assert.equal(fs.existsSync(path.join(outside, "swap-source-moved")), false);
  checks.push("held source directory HANDLE survives path-to-junction replacement");

  roots.closeParentHandle(sourceParent); sourceParent = null;
  roots.closeParentHandle(destinationParent); destinationParent = null;

  // Cross-volume directory move is rejected before mutation.
  crossRoot = fs.mkdtempSync(path.join("D:\\", "raya-dir-rename-cross-volume-"));
  fs.mkdirSync(path.join(crossRoot, "destination"));
  crossParent = roots.openWorkspaceRoot(crossRoot);
  fs.mkdirSync(path.join(rootPath, "cross-volume-dir"));
  const rootHandle = roots.openWorkspaceRoot(rootPath);
  assert.throws(() => rename.renameChildRelative(rootHandle, "cross-volume-dir", crossParent, "cross-volume-dir"), /cross_volume/);
  assert.equal(fs.statSync(path.join(rootPath, "cross-volume-dir")).isDirectory(), true);
  assert.equal(fs.existsSync(path.join(crossRoot, "cross-volume-dir")), false);
  roots.closeParentHandle(rootHandle);
  roots.closeParentHandle(crossParent); crossParent = null;
  fs.rmSync(crossRoot, { recursive: true, force: true });
  crossRoot = null;
  checks.push("cross-volume directory move rejected before mutation");

  assert.equal(roots.processHandleCount(), baseline);
  console.log(JSON.stringify({ok:true,checkCount:checks.length,checks,scope:"isolated directory-rename candidate; local NTFS fixtures only"},null,2));
} finally {
  if (sourceParent) { try { roots.closeParentHandle(sourceParent); } catch {} }
  if (destinationParent) { try { roots.closeParentHandle(destinationParent); } catch {} }
  if (crossParent) { try { roots.closeParentHandle(crossParent); } catch {} }
  try { if (destinationAddDeny) icacls(destinationPath, "/remove:d", "*" + sid); } catch {}
  try { if (sourceDeleteDeny) icacls(path.join(sourcePath, "deny-source"), "/remove:d", "*" + sid); } catch {}
  if (crossRoot) { try { fs.rmSync(crossRoot, { recursive: true, force: true }); } catch {} }
  if (destinationSwapped) { try { fs.rmdirSync(destinationPath); } catch {} }
  if (sourceSwapped) { try { fs.rmdirSync(sourcePath); } catch {} }
  fs.rmSync(fixture, { recursive: true, force: true });
}

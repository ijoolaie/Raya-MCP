"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { execFileSync } = require("node:child_process");
const roots = require("./build/Release/napi_probe_raya_phase1_rootapi.node");
const rename = require("./build/Release/napi_probe_raya_phase1_rename.node");
function icacls(target, ...args) { return execFileSync("icacls.exe", [target, ...args], { encoding: "utf8", windowsHide: true }); }
function currentSid() {
  const output = execFileSync("whoami.exe", ["/user", "/fo", "csv", "/nh"], { encoding: "utf8", windowsHide: true });
  const match = output.match(/"(S-1-[0-9-]+)"/);
  if (!match) throw new Error("Unable to identify current user SID");
  return match[1];
}
function denied(error) { return /0xC0000022|STATUS_ACCESS_DENIED|access is denied|error 5/i.test(String(error?.message || error)); }
const fixture = fs.mkdtempSync(path.join(os.tmpdir(), "raya-rename-acl-"));
const rootPath = path.join(fixture, "root");
const sourcePath = path.join(rootPath, "source");
const destinationPath = path.join(rootPath, "destination");
const sid = currentSid();
const baseline = roots.processHandleCount();
let sourceParent = null, destinationParent = null;
let sourceDeleteDeny = false, destinationAddDeny = false;
try {
  fs.mkdirSync(sourcePath, { recursive: true });
  fs.mkdirSync(destinationPath, { recursive: true });
  fs.writeFileSync(path.join(sourcePath, "deny-destination.txt"), "DESTINATION-DENY-SOURCE", { flag: "wx" });
  fs.writeFileSync(path.join(sourcePath, "deny-source.txt"), "SOURCE-DELETE-DENY", { flag: "wx" });
  sourceParent = roots.openParentRelative(rootPath, "source");
  destinationParent = roots.openParentRelative(rootPath, "destination");

  const destinationAcl = icacls(destinationPath, "/deny", "*" + sid + ":(WD)");
  destinationAddDeny = true;
  assert.match(destinationAcl, /Successfully processed|processed file/i);
  assert.match(icacls(destinationPath), /\(DENY\)\(WD\)/, "exact destination FILE_ADD_FILE deny ACE must be visible");
  let destinationDenied = false, destinationError = null;
  try { rename.renameChildRelative(sourceParent, "deny-destination.txt", destinationParent, "blocked.txt"); }
  catch (error) { destinationDenied = denied(error); destinationError = String(error.message || error); if (!destinationDenied) throw error; }
  assert.equal(destinationDenied, true, "rename must honor destination directory FILE_ADD_FILE denial");
  assert.equal(fs.readFileSync(path.join(sourcePath, "deny-destination.txt"), "utf8"), "DESTINATION-DENY-SOURCE");
  assert.equal(fs.existsSync(path.join(destinationPath, "blocked.txt")), false);
  icacls(destinationPath, "/remove:d", "*" + sid); destinationAddDeny = false;
  rename.renameChildRelative(sourceParent, "deny-destination.txt", destinationParent, "allowed-after-destination-restore.txt");
  assert.equal(fs.readFileSync(path.join(destinationPath, "allowed-after-destination-restore.txt"), "utf8"), "DESTINATION-DENY-SOURCE");
  assert.equal(fs.existsSync(path.join(sourcePath, "deny-destination.txt")), false);

  const sourceFile = path.join(sourcePath, "deny-source.txt");
  const sourceAcl = icacls(sourceFile, "/deny", "*" + sid + ":(D)");
  sourceDeleteDeny = true;
  assert.match(sourceAcl, /Successfully processed|processed file/i);
  assert.match(icacls(sourceFile), /\(DENY\)\(D\)/, "exact source DELETE deny ACE must be visible");
  let sourceDenied = false, sourceError = null;
  try { rename.renameChildRelative(sourceParent, "deny-source.txt", destinationParent, "blocked-source-delete.txt"); }
  catch (error) { sourceDenied = denied(error); sourceError = String(error.message || error); if (!sourceDenied) throw error; }
  assert.equal(sourceDenied, true, "rename must honor source FILE_DELETE denial");
  assert.equal(fs.existsSync(sourceFile), true, "source name must remain present while DELETE is denied");
  assert.equal(fs.existsSync(path.join(destinationPath, "blocked-source-delete.txt")), false);
  icacls(sourceFile, "/remove:d", "*" + sid); sourceDeleteDeny = false;
  assert.equal(fs.readFileSync(sourceFile, "utf8"), "SOURCE-DELETE-DENY", "source bytes must remain unchanged after ACL restoration");
  rename.renameChildRelative(sourceParent, "deny-source.txt", destinationParent, "allowed-after-source-restore.txt");
  assert.equal(fs.readFileSync(path.join(destinationPath, "allowed-after-source-restore.txt"), "utf8"), "SOURCE-DELETE-DENY");
  assert.equal(fs.existsSync(sourceFile), false);

  roots.closeParentHandle(sourceParent); sourceParent = null;
  roots.closeParentHandle(destinationParent); destinationParent = null;
  assert.equal(roots.processHandleCount(), baseline);
  console.log(JSON.stringify({ok:true,sid,destinationAddFileDenied:destinationDenied,destinationError,sourceDeleteDenied:sourceDenied,sourceError,bothOperationsSucceededAfterAclRestore:true,exactDenyAcesObserved:true,handleCountReturnedToBaseline:true,scope:"explicit ACEs on disposable fixture entries only; ACLs restored before cleanup"},null,2));
} finally {
  if (sourceParent) { try { roots.closeParentHandle(sourceParent); } catch {} }
  if (destinationParent) { try { roots.closeParentHandle(destinationParent); } catch {} }
  try { if (destinationAddDeny) icacls(destinationPath, "/remove:d", "*" + sid); } catch {}
  try { if (sourceDeleteDeny) icacls(path.join(sourcePath, "deny-source.txt"), "/remove:d", "*" + sid); } catch {}
  fs.rmSync(fixture, { recursive: true, force: true });
}

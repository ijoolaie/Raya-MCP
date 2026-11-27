"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const addon = require("./build/Release/napi_probe_raya_phase1_hardlink_guard.node");

const fixture = fs.mkdtempSync(path.join(os.tmpdir(), "raya-hardlink-guard-"));
const root = path.join(fixture, "root");
const parentPath = path.join(root, "parent");
const singlePath = path.join(parentPath, "single.txt");
const outsideAlias = path.join(fixture, "single-outside-alias.txt");
const movedPath = path.join(fixture, "single-moved-outside.txt");
const outsideSource = path.join(fixture, "outside-source.txt");
const insideAlias = path.join(parentPath, "inside-alias.txt");
const baseline = addon.processHandleCount();
let parent = null;

try {
  fs.mkdirSync(parentPath, { recursive: true });
  fs.writeFileSync(singlePath, "SINGLE-LINK-CONTENT", { flag: "wx" });
  parent = addon.openParentRelative(root, "parent");

  let linkCreatedWhileReadHandleOpen = false;
  let linkErrorCode = null;
  let renameBlockedWhileReadHandleOpen = false;
  let renameErrorCode = null;
  let concurrentLinkRejected = false;
  try {
    addon.readChildFromParent(parent, "single.txt", 1024, () => {
      try {
        fs.renameSync(singlePath, movedPath);
      } catch (error) {
        renameBlockedWhileReadHandleOpen = true;
        renameErrorCode = error.code || error.name;
      }
      try {
        fs.linkSync(singlePath, outsideAlias);
        linkCreatedWhileReadHandleOpen = true;
      } catch (error) {
        linkErrorCode = error.code || error.name;
      }
    });
  } catch (error) {
    concurrentLinkRejected = /hard-linked/.test(error.message);
  }
  assert.equal(renameBlockedWhileReadHandleOpen, true, "the held read HANDLE must prevent renaming the in-root name outside the workspace");
  assert.equal(linkCreatedWhileReadHandleOpen, true, "this NTFS host should characterize whether hardlink creation bypasses the sharing mode");
  assert.equal(concurrentLinkRejected, true, "read must fail closed when the link count changes before the read");
  assert.equal(fs.statSync(singlePath).nlink, 2);
  fs.unlinkSync(outsideAlias);
  assert.equal(fs.statSync(singlePath).nlink, 1);

  const read = addon.readChildFromParent(parent, "single.txt", 1024);
  assert.equal(read.content.toString(), "SINGLE-LINK-CONTENT");
  const relativeRead = addon.openRelative(root, "parent/single.txt", 0, 1024);
  assert.equal(relativeRead.content.toString(), "SINGLE-LINK-CONTENT");
  assert.equal(read.numberOfLinks, 1);
  const identity = addon.inspectChildFromParent(parent, "single.txt");
  assert.equal(identity.isRegularFile, true);
  assert.equal(identity.numberOfLinks, 1);

  // Once the guarded HANDLE is closed, a hard link can be created; subsequent
  // reads and identity inspection must reject the now multi-linked file.
  fs.linkSync(singlePath, outsideAlias);
  assert.equal(fs.statSync(singlePath).nlink, 2);
  assert.throws(() => addon.readChildFromParent(parent, "single.txt", 1024), /hard-linked|NTSTATUS/i);
  assert.throws(() => addon.inspectChildFromParent(parent, "single.txt"), /hard-linked|NTSTATUS/i);
  assert.throws(() => addon.openRelative(root, "parent/single.txt", 0, 1024), /disallowed type|relative component/i);

  // An outside-created file hard-linked into the workspace must also fail closed.
  fs.writeFileSync(outsideSource, "OUTSIDE-HARDLINK-CONTENT", { flag: "wx" });
  fs.linkSync(outsideSource, insideAlias);
  assert.equal(fs.statSync(insideAlias).nlink, 2);
  let aliasDeleteBlocked = false;
  let aliasDeleteError = null;
  let aliasReadRejected = false;
  try {
    addon.readChildFromParent(parent, "inside-alias.txt", 1024, () => {
      try {
        fs.unlinkSync(insideAlias);
      } catch (error) {
        aliasDeleteBlocked = true;
        aliasDeleteError = error.code || error.name;
      }
    });
  } catch (error) {
    aliasReadRejected = /hard-linked/.test(error.message);
  }
  assert.equal(aliasDeleteBlocked, true, "the open guarded HANDLE must prevent removing the in-root hard-link name before validation");
  assert.equal(aliasReadRejected, true, "the guarded read must reject the outside-backed alias");
  assert.equal(fs.existsSync(insideAlias), true);
  assert.equal(fs.statSync(insideAlias).nlink, 2);
  assert.throws(() => addon.readChildFromParent(parent, "inside-alias.txt", 1024), /hard-linked|NTSTATUS/i);
  assert.throws(() => addon.inspectChildFromParent(parent, "inside-alias.txt"), /hard-linked|NTSTATUS/i);
  assert.throws(() => addon.openRelative(root, "parent/inside-alias.txt", 0, 1024), /disallowed type|relative component/i);

  addon.closeParentHandle(parent);
  parent = null;
  assert.equal(addon.processHandleCount(), baseline, "all native HANDLEs must return to baseline");

  console.log(JSON.stringify({
    ok: true,
    singleLinkReadAllowed: true,
    singleLinkIdentityAllowed: true,
    hardLinkCreationSucceededWhileReadHandleHeld: linkCreatedWhileReadHandleOpen,
    concurrentReadRejectedAfterLinkCountChanged: concurrentLinkRejected,
    sharingError: linkErrorCode,
    renameBlockedWhileReadHandleOpen,
    renameErrorCode,
    outsideAliasRemovalBlockedWhileReadHandleOpen: aliasDeleteBlocked,
    outsideAliasRemovalError: aliasDeleteError,
    postCloseHardLinkCreationAllowedForProbe: true,
    preexistingMultiLinkReadRejected: true,
    preexistingMultiLinkIdentityRejected: true,
    openRelativeSingleLinkReadAllowed: true,
    preexistingMultiLinkOpenRejected: true,
    outsideFileAliasedIntoRootRejected: true,
    handleCountReturnedToBaseline: true,
    policy: "deny files with NumberOfLinks != 1; open read handles with FILE_SHARE_READ only to block write/delete/rename; recheck link count after open; hard-link creation can still add a new alias to an already in-root file, but does not redirect the held file object"
  }, null, 2));
} finally {
  if (parent) {
    try { addon.closeParentHandle(parent); } catch {}
  }
  fs.rmSync(fixture, { recursive: true, force: true });
}

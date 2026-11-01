"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const addon = require("./build/Release/napi_probe_raya_phase1_dircreate_pinned.node");

const fixture = fs.mkdtempSync(path.join(os.tmpdir(), "raya-hardlink-alias-"));
const root = path.join(fixture, "root");
const parentPath = path.join(root, "parent");
const outsidePath = path.join(fixture, "outside.txt");
const aliasPath = path.join(parentPath, "inside-link.txt");
const secret = "OUTSIDE-HARDLINK-CONTENT";
let parent = null;
const handleBaseline = addon.processHandleCount();

try {
  fs.mkdirSync(parentPath, { recursive: true });
  fs.writeFileSync(outsidePath, secret, { flag: "wx" });
  fs.linkSync(outsidePath, aliasPath);

  const insideStat = fs.statSync(aliasPath);
  const outsideStat = fs.statSync(outsidePath);
  assert.equal(insideStat.nlink, 2, "fixture must contain exactly two hard links");
  assert.equal(insideStat.dev, outsideStat.dev, "hard links must be on the same volume");
  assert.equal(insideStat.ino, outsideStat.ino, "inside alias and outside path must identify the same file");

  parent = addon.openParentRelative(root, "parent");
  const read = addon.readChildFromParent(parent, "inside-link.txt", 1024);
  const identity = addon.inspectChildFromParent(parent, "inside-link.txt");
  assert.equal(read.content.toString(), secret);
  assert.equal(identity.isRegularFile, true);
  assert.equal(identity.isReparsePoint, false);

  addon.closeParentHandle(parent);
  parent = null;
  assert.equal(addon.processHandleCount(), handleBaseline, "parent/child HANDLE count must return to baseline");

  process.stdout.write(JSON.stringify({
    ok: true,
    finding: "HARDLINK_ALIAS_ACCEPTED",
    volume: "current local NTFS volume",
    numberOfLinks: insideStat.nlink,
    sameFileIdentity: true,
    readThroughRootAccepted: true,
    outsideFileContentReadThroughInsideAlias: true,
    nativeIdentityReportsRegularNonReparseFile: true,
    handleCountReturnedToBaseline: true,
    interpretation: "The prototype enforces path/reparse containment but does not currently distinguish a file with an additional hard-link name outside the root. Whether this violates the intended trust boundary must be decided explicitly."
  }, null, 2) + "\n");
} finally {
  if (parent) {
    try { addon.closeParentHandle(parent); } catch {}
  }
  fs.rmSync(fixture, { recursive: true, force: true });
}

"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");

const addon = require("./build/Release/napi_probe_raya_phase1_hardlink_guarded_rootapi.node");
const fixture = fs.mkdtempSync(path.join(os.tmpdir(), "raya-rootapi-hardlink-guarded-"));
const root = path.join(fixture, "root");
const outside = path.join(fixture, "outside");
let parent = null;

function expectPolicyReject(label, operation) {
  assert.throws(operation, (error) => {
    assert.match(String(error && error.message), /hardlink_policy/i, label);
    return true;
  }, label + " must reject a multi-link regular file");
}

try {
  fs.mkdirSync(root);
  fs.mkdirSync(outside);
  fs.writeFileSync(path.join(root, "ordinary.txt"), "ORDINARY-INTERNAL-CONTENT");
  fs.writeFileSync(path.join(outside, "secret.txt"), "OUTSIDE-SECRET-HARDLINK-CANARY");

  parent = addon.openWorkspaceRoot(root);

  const ordinaryRead = addon.readChildFromParent(parent, "ordinary.txt", 1024);
  assert.equal(ordinaryRead.content.toString(), "ORDINARY-INTERNAL-CONTENT");
  const ordinaryTail = addon.readTailChildFromParent(parent, "ordinary.txt", 1024);
  assert.equal(ordinaryTail.content.toString(), "ORDINARY-INTERNAL-CONTENT");
  const ordinaryMetadata = addon.inspectEntryMetadata(parent, "ordinary.txt");
  assert.equal(ordinaryMetadata.numberOfLinks, 1);
  const ordinaryPage = addon.enumerateDirectoryPage(parent, 100, true);
  assert.ok(ordinaryPage.entries.some((entry) => entry.name === "ordinary.txt"));

  fs.linkSync(path.join(outside, "secret.txt"), path.join(root, "alias.txt"));
  assert.equal(fs.statSync(path.join(root, "alias.txt")).nlink, 2);

  expectPolicyReject("readChildFromParent", () =>
    addon.readChildFromParent(parent, "alias.txt", 1024));
  expectPolicyReject("readTailChildFromParent", () =>
    addon.readTailChildFromParent(parent, "alias.txt", 1024));
  expectPolicyReject("inspectEntryMetadata", () =>
    addon.inspectEntryMetadata(parent, "alias.txt"));
  expectPolicyReject("enumerateDirectoryPage", () =>
    addon.enumerateDirectoryPage(parent, 100, true));

  console.log(JSON.stringify({
    ok: true,
    localProvider: "NTFS",
    ordinarySingleLinkAccepted: true,
    ordinaryLinkCount: ordinaryMetadata.numberOfLinks,
    outsideBackedHardLinkCount: fs.statSync(path.join(root, "alias.txt")).nlink,
    readRejected: true,
    tailReadRejected: true,
    metadataRejected: true,
    enumerationRejected: true,
    policy: "reject regular files unless NumberOfLinks is exactly one"
  }, null, 2));
} finally {
  if (parent) {
    try { addon.closeParentHandle(parent); } catch {}
  }
  fs.rmSync(fixture, { recursive: true, force: true });
}
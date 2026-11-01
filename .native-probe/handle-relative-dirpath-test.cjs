"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const addon = require("./build/Release/napi_probe_raya_phase1_dircreate.node");
function main() {
  const fixture = fs.mkdtempSync(path.join(os.tmpdir(), "raya-recursive-dircreate-"));
  const root = path.join(fixture, "root"), outside = path.join(fixture, "outside");
  const parentPath = path.join(root, "parent");
  let parent = null;
  const checks = [];
  try {
    fs.mkdirSync(parentPath, { recursive: true });
    fs.mkdirSync(outside);
    fs.writeFileSync(path.join(outside, "sentinel.txt"), "OUTSIDE-UNCHANGED");
    fs.writeFileSync(path.join(parentPath, "regular.txt"), "FILE-COLLISION");
    parent = addon.openParentRelative(root, "parent");
    const baseline = addon.processHandleCount();
    const created = addon.createDirectoryPathRelative(parent, "alpha\\beta\\gamma");
    assert.deepEqual(created, { createdCount: 3, existingCount: 0, committed: true });
    assert.ok(fs.statSync(path.join(parentPath, "alpha", "beta", "gamma")).isDirectory());
    assert.equal(addon.processHandleCount(), baseline);
    checks.push("three nested directories created relative to held parent HANDLE");
    const existing = addon.createDirectoryPathRelative(parent, "alpha\\beta\\gamma");
    assert.deepEqual(existing, { createdCount: 0, existingCount: 3, committed: true });
    assert.equal(addon.processHandleCount(), baseline);
    checks.push("existing plain directories opened and reused without replacement");
    fs.mkdirSync(path.join(parentPath, "existing"));
    const mixed = addon.createDirectoryPathRelative(parent, "existing\\new-child");
    assert.deepEqual(mixed, { createdCount: 1, existingCount: 1, committed: true });
    checks.push("mixed existing/new path reports exact counts");
    assert.throws(() => addon.createDirectoryPathRelative(parent, "regular.txt\\child"), /existing directory component could not be safely opened/i);
    assert.equal(fs.readFileSync(path.join(parentPath, "regular.txt"), "utf8"), "FILE-COLLISION");
    assert.equal(addon.processHandleCount(), baseline);
    checks.push("regular-file collision fails closed and preserves file bytes");
    fs.symlinkSync(outside, path.join(parentPath, "junction"), "junction");
    assert.throws(() => addon.createDirectoryPathRelative(parent, "junction\\escaped"), /existing directory component could not be safely opened|plain non-reparse directory/i);
    assert.equal(fs.existsSync(path.join(outside, "escaped")), false);
    assert.equal(fs.readFileSync(path.join(outside, "sentinel.txt"), "utf8"), "OUTSIDE-UNCHANGED");
    assert.equal(addon.processHandleCount(), baseline);
    checks.push("junction rejected without following or changing outside target");
    assert.throws(() => addon.createDirectoryPathRelative(parent, "must-not-exist\\CON"), /unsafe or empty directory component/i);
    assert.equal(fs.existsSync(path.join(parentPath, "must-not-exist")), false);
    assert.throws(() => addon.createDirectoryPathRelative(parent, "trailing\\"), /unsafe or empty directory component|must not end with a separator/i);
    assert.throws(() => addon.createDirectoryPathRelative(parent, "double\\\\separator"), /unsafe or empty directory component/i);
    assert.equal(addon.processHandleCount(), baseline);
    checks.push("full path validated before mutation; unsafe/empty/trailing components rejected");
    const closeBefore = addon.closeFailureCount();
    addon.failNextCloseForTest();
    assert.throws(() => addon.createDirectoryPathRelative(parent, "partial\\second"), /partial_commit/i);
    assert.equal(addon.closeFailureCount(), closeBefore + 1);
    assert.ok(fs.statSync(path.join(parentPath, "partial")).isDirectory());
    assert.ok(fs.statSync(path.join(parentPath, "partial", "second")).isDirectory());
    assert.equal(addon.processHandleCount(), baseline);
    checks.push("intermediate HANDLE close failure reports partial commit and releases remaining HANDLEs");
    const finalCloseBefore = addon.closeFailureCount();
    addon.failNextCloseForTest();
    assert.throws(() => addon.createDirectoryPathRelative(parent, "final-committed"), /committed_but_cleanup_failed/i);
    assert.equal(addon.closeFailureCount(), finalCloseBefore + 1);
    assert.ok(fs.statSync(path.join(parentPath, "final-committed")).isDirectory());
    assert.equal(addon.processHandleCount(), baseline);
    checks.push("final close failure reports committed directory without claiming rollback");
    addon.closeParentHandle(parent); parent = null;
    console.log(JSON.stringify({ ok: true, checks }, null, 2));
  } finally {
    if (parent) { try { addon.closeParentHandle(parent); } catch {} }
    fs.rmSync(fixture, { recursive: true, force: true });
  }
}
try { main(); } catch (error) { console.error(error); process.exitCode = 1; }

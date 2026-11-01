"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const addon = require("./build/Release/napi_probe_raya_phase1_dircreate_pinned.node");

async function waitForBaseline(baseline) {
  for (let i = 0; i < 120; i++) {
    global.gc();
    await new Promise(resolve => setTimeout(resolve, 5));
    if (addon.processHandleCount() === baseline) return true;
  }
  return false;
}

async function main() {
  const base = fs.mkdtempSync(path.join(__dirname, "fixture-dircreate-napi-failure-"));
  const root = path.join(base, "root");
  fs.mkdirSync(path.join(root, "parent"), { recursive: true });
  try {
    const baseline = addon.processHandleCount();

    addon.failParentRefAllocationForTest();
    let allocationError = "";
    try { addon.openParentRelative(root, "parent"); }
    catch (error) { allocationError = String(error.message || error); }
    assert.match(allocationError, /out of memory creating parent handle owner/i);
    assert.equal(addon.processHandleCount(), baseline,
      "parent-owner allocation failure leaked the still-locally-owned HANDLE");

    addon.failParentOpenCreateObjectForTest();
    addon.openParentRelative(root, "parent");
    assert.equal(addon.processHandleCount(), baseline,
      "create_object failure leaked the still-locally-owned parent HANDLE");

    addon.failParentOpenWrapForTest();
    addon.openParentRelative(root, "parent");
    assert.equal(addon.processHandleCount(), baseline,
      "wrap failure leaked the still-locally-owned parent HANDLE");

    addon.failParentOpenedPropertyForTest();
    addon.openParentRelative(root, "parent");
    assert.equal(await waitForBaseline(baseline), true,
      "post-wrap property failure did not eventually release the finalizer-owned HANDLE");

    const renamed = path.join(root, "parent-after-failure");
    fs.renameSync(path.join(root, "parent"), renamed);
    fs.renameSync(renamed, path.join(root, "parent"));

    console.log(JSON.stringify({
      ok: true,
      parentOwnerAllocationFailureReleasedHandle: true,
      createObjectFailureReleasedHandle: true,
      wrapFailureReleasedHandle: true,
      postWrapPropertyFailureReleasedHandleByFinalizer: true,
      renameAllowedAfterFailure: true,
      handleCountReturnedToBaseline: true,
      fixtureRemoved: true
    }, null, 2));
  } finally {
    fs.rmSync(base, { recursive: true, force: true });
  }
}

main().catch(error => {
  console.error(error);
  process.exitCode = 1;
});

"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const addon = require("./build/Release/napi_probe_raya_phase1_dircreate_pinned.node");

async function main() {
  const base = fs.mkdtempSync(path.join(__dirname, "fixture-dircreate-finalizer-"));
  const root = path.join(base, "root");
  const parentPath = path.join(root, "parent");
  fs.mkdirSync(parentPath, { recursive: true });
  try {
    const baseline = addon.processHandleCount();
    let parent = addon.openParentRelative(root, "parent");
    assert.ok(parent && parent.opened);
    assert.equal(addon.processHandleCount(), baseline + 1, "wrapped object did not own exactly one HANDLE");

    parent = null;
    let released = false;
    for (let i = 0; i < 100; i++) {
      global.gc();
      await new Promise(resolve => setTimeout(resolve, 5));
      if (addon.processHandleCount() === baseline) {
        released = true;
        break;
      }
    }
    assert.equal(released, true, "GC finalizer did not release the wrapped parent HANDLE");

    const renamed = path.join(base, "root", "parent-after-finalize");
    fs.renameSync(parentPath, renamed);
    fs.renameSync(renamed, parentPath);
    assert.equal(addon.processHandleCount(), baseline, "HANDLE count changed after rename check");

    fs.rmSync(base, { recursive: true, force: true });
    assert.equal(fs.existsSync(base), false, "finalizer fixture was not removed");
    console.log(JSON.stringify({
      ok: true,
      wrappedHandleReleasedByFinalizer: true,
      renameAllowedAfterFinalization: true,
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

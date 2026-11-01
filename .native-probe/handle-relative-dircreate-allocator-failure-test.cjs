"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const addon = require("./build/Release/napi_probe_raya_phase1_dircreate_pinned.node");

function assertHeapBaseline(baseline, label) {
  const current = addon.allocationStats();
  assert.equal(current.liveBlocks, baseline.liveBlocks, `${label}: tracked native allocation block leak`);
  assert.equal(current.liveBytes, baseline.liveBytes, `${label}: tracked native allocation byte leak`);
}

function injectAndAssertFailure(nth, operation, baseline, handleBaseline, label) {
  addon.failAllocationNthForTest(nth);
  let failure = null;
  try { operation(); } catch (error) { failure = error; }
  assert.ok(failure, `${label}: allocation #${nth} should have failed the operation`);
  assertHeapBaseline(baseline, `${label} allocation #${nth}`);
  assert.equal(addon.processHandleCount(), handleBaseline, `${label} allocation #${nth}: HANDLE leak`);
}

async function waitForHeapBaseline(baseline) {
  for (let i = 0; i < 120; i++) {
    global.gc();
    await new Promise(resolve => setTimeout(resolve, 5));
    const current = addon.allocationStats();
    if (current.liveBlocks === baseline.liveBlocks && current.liveBytes === baseline.liveBytes) return true;
  }
  return false;
}

async function main() {
  const fixture = fs.mkdtempSync(path.join(__dirname, "fixture-dircreate-allocator-"));
  const root = path.join(fixture, "root");
  const parentPath = path.join(root, "parent");
  fs.mkdirSync(parentPath, { recursive: true });
  fs.writeFileSync(path.join(parentPath, "read.txt"), "allocator-fault-injection");
  fs.writeFileSync(path.join(root, "read.txt"), "open-relative-allocator-fault");
  let parent = null;

  try {
    const handleBaseline = addon.processHandleCount();
    const heapBaseline = addon.allocationStats();

    for (let nth = 1; nth <= 5; nth++) {
      injectAndAssertFailure(nth, () => addon.openParentRelative(root, "parent"),
        heapBaseline, handleBaseline, "openParentRelative");
    }

    parent = addon.openParentRelative(root, "parent");
    const parentHeapBaseline = addon.allocationStats();
    const parentHandleBaseline = addon.processHandleCount();

    for (let nth = 1; nth <= 3; nth++) {
      injectAndAssertFailure(nth,
        () => addon.createDirectoryPathRelative(parent, "should-not-exist\\nested"),
        parentHeapBaseline, parentHandleBaseline, "createDirectoryPathRelative");
    }
    assert.equal(fs.existsSync(path.join(parentPath, "should-not-exist")), false,
      "allocation failure must happen before recursive directory creation");

    for (let nth = 1; nth <= 3; nth++) {
      injectAndAssertFailure(nth,
        () => addon.readChildFromParent(parent, "read.txt", 1024),
        parentHeapBaseline, parentHandleBaseline, "readChildFromParent");
    }

    for (let nth = 1; nth <= 5; nth++) {
      injectAndAssertFailure(nth,
        () => addon.openRelative(root, "read.txt", 0, 1024),
        parentHeapBaseline, parentHandleBaseline, "openRelative");
    }

    for (let nth = 1; nth <= 2; nth++) {
      injectAndAssertFailure(nth,
        () => addon.inspectChildFromParent(parent, "read.txt"),
        parentHeapBaseline, parentHandleBaseline, "inspectChildFromParent");
    }
    for (let nth = 1; nth <= 2; nth++) {
      injectAndAssertFailure(nth,
        () => addon.createChildExclusive(parent, "not-created.txt"),
        parentHeapBaseline, parentHandleBaseline, "createChildExclusive");
    }
    for (let nth = 1; nth <= 2; nth++) {
      injectAndAssertFailure(nth,
        () => addon.createDirectoryChildExclusive(parent, "not-created-dir"),
        parentHeapBaseline, parentHandleBaseline, "createDirectoryChildExclusive");
    }

    assert.equal(addon.closeParentHandle(parent), true);
    parent = null;
    assert.equal(await waitForHeapBaseline(heapBaseline), true,
      "parent HANDLE finalizer did not release its tracked owner allocation");

    console.log(JSON.stringify({
      ok: true,
      allocationFailurePointsTested: {
        openParentRelative: 5,
        createDirectoryPathRelative: 3,
        readChildFromParent: 3,
        openRelative: 5,
        inspectChildFromParent: 2,
        createChildExclusive: 2,
        createDirectoryChildExclusive: 2
      },
      trackedNativeHeapReturnedToBaselineAfterEveryFailure: true,
      processHandleCountReturnedToBaselineAfterEveryFailure: true,
      recursiveCreateDidNotBeginAfterValidationAllocationFailure: true,
      finalLiveAllocationBlocks: addon.allocationStats().liveBlocks,
      finalLiveAllocationBytes: addon.allocationStats().liveBytes,
      fixtureRemoved: true
    }, null, 2));
  } finally {
    if (parent) {
      try { addon.closeParentHandle(parent); } catch {}
    }
    fs.rmSync(fixture, { recursive: true, force: true });
  }
}

main().catch(error => {
  console.error(error);
  process.exitCode = 1;
});

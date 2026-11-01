"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const { spawn } = require("node:child_process");
const addon = require("./build/Release/napi_probe_rootchain_acceptance.node");
async function main() {
const base = fs.mkdtempSync(path.join(__dirname, "fixture-"));
const root = path.join(base, "root");
const outside = path.join(base, "outside");
fs.mkdirSync(path.join(root, "safe", "nested"), { recursive: true });
fs.mkdirSync(outside);
fs.writeFileSync(path.join(root, "safe", "nested", "ok.txt"), "within-root");
fs.writeFileSync(path.join(outside, "secret.txt"), "outside-secret-with-a-different-length");
const outcomes = [];
const closedWorkers = new WeakSet();
let child = null;
let rootRaceChild = null;
let primaryError = null;
try {
const finalizerProbe = spawn(process.execPath, ["--expose-gc", "--input-type=commonjs", "-e", `
(async () => {
  const fs = require("node:fs");
  const os = require("node:os");
  const path = require("node:path");
  const addon = require("./build/Release/napi_probe_rootchain_acceptance.node");
  if (typeof global.gc !== "function") throw new Error("explicit GC is unavailable");
  const base = fs.mkdtempSync(path.join(os.tmpdir(), "raya-parent-finalizer-"));
  try {
    const root = path.join(base, "root");
    fs.mkdirSync(path.join(root, "parent"), { recursive: true });
    const baseline = addon.processHandleCount();
    let parent = addon.openParentRelative(root, "parent");
    const opened = addon.processHandleCount();
    if (opened !== baseline + 1) throw new Error("expected exactly one transferred HANDLE: " + baseline + " -> " + opened);
    const closeFailuresBeforeFinalizer = addon.closeFailureCount();
    addon.failNextCloseForTest();
    parent = null;
    let afterFinalizer = addon.processHandleCount();
    for (let i = 0; i < 100 && afterFinalizer !== baseline; i++) {
      global.gc();
      await new Promise(resolve => setImmediate(resolve));
      afterFinalizer = addon.processHandleCount();
    }
    if (afterFinalizer !== baseline) throw new Error("external finalizer did not close HANDLE: baseline=" + baseline + ", current=" + afterFinalizer);
    const closeFailuresAfterFinalizer = addon.closeFailureCount();
    if (closeFailuresAfterFinalizer !== closeFailuresBeforeFinalizer + 1) {
      throw new Error("finalizer close failure was not observable: before=" + closeFailuresBeforeFinalizer + ", after=" + closeFailuresAfterFinalizer);
    }
    console.log(JSON.stringify({ ok: true, baseline, opened, afterFinalizer, finalizerCloseFailureObserved: true }));
  } finally {
    fs.rmSync(base, { recursive: true, force: true });
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
`], { cwd: __dirname, stdio: ["ignore", "pipe", "pipe"] });
let finalizerStdout = "";
let finalizerStderr = "";
finalizerProbe.stdout.on("data", chunk => { finalizerStdout += chunk.toString(); });
finalizerProbe.stderr.on("data", chunk => { finalizerStderr += chunk.toString(); });
const finalizerExit = await new Promise((resolve, reject) => {
  let timedOut = false;
  const timer = setTimeout(() => {
    timedOut = true;
    finalizerProbe.kill();
  }, 10000);
  finalizerProbe.once("error", error => { clearTimeout(timer); reject(error); });
  finalizerProbe.once("close", code => {
    clearTimeout(timer);
    if (timedOut) reject(new Error("parent external-finalizer probe timed out and child was reaped"));
    else resolve(code);
  });
});
assert.equal(finalizerExit, 0, "parent external-finalizer probe failed: " + finalizerStderr + finalizerStdout);
const finalizerReportLine = finalizerStdout.split(/\\r?\\n/).find(line => line.startsWith("{"));
assert.ok(finalizerReportLine, "parent external-finalizer probe returned no report: " + finalizerStdout);
const finalizerReport = JSON.parse(finalizerReportLine);
assert.equal(finalizerReport.ok, true);
assert.equal(finalizerReport.afterFinalizer, finalizerReport.baseline);
outcomes.push("N-API external finalizer closed the transferred parent HANDLE after explicit GC; injected post-close failure was counted and child-process handle count returned exactly to baseline");
  const safe = addon.openRelative(root, "safe\\nested\\ok.txt");
  assert.equal(safe.opened, true);
  assert.equal(safe.isDirectory, false);
  assert.equal(safe.size, 11);
  outcomes.push("nested file opened by relative handle walk");

  const trailingRoot = addon.openRelative(root + "\\", "safe\\nested\\ok.txt");
  assert.equal(trailingRoot.size, 11);
  assert.throws(() => addon.openRelative("relative-root", "safe\\nested\\ok.txt"));
  assert.throws(() => addon.openRelative(root.replace("\\Users", "\\\\Users"), "safe\\nested\\ok.txt"));
  assert.throws(() => addon.openRelative(root + "\\..\\", "safe\\nested\\ok.txt"));
  assert.throws(() => addon.openRelative(root + "\\stream:ads", "safe\\nested\\ok.txt"));
  assert.throws(() => addon.openRelative("\\\\server\\share\\root", "safe\\nested\\ok.txt"));
  assert.throws(() => addon.openRelative("\\\\?\\C:\\root", "safe\\nested\\ok.txt"));
  assert.throws(() => addon.openRelative("\\\\?\\Volume{00000000-0000-0000-0000-000000000000}\\", "safe\\nested\\ok.txt"));
  assert.throws(() => addon.openRelative(path.join(base, "missing-root"), "safe\\nested\\ok.txt"));
  const fileAsRoot = path.join(base, "not-a-directory");
  fs.writeFileSync(fileAsRoot, "x");
  assert.throws(() => addon.openRelative(fileAsRoot, "safe\\nested\\ok.txt"));
  const handlesBeforeFailures = addon.processHandleCount();
  for (let i = 0; i < 100; i++) {
    assert.throws(() => addon.openRelative(root + "\\..\\", "safe\\nested\\ok.txt"));
  }
  const handlesAfterFailures = addon.processHandleCount();
  assert.equal(handlesAfterFailures, handlesBeforeFailures,
    `process handle count changed after repeated invalid-root failures: before=${handlesBeforeFailures}, after=${handlesAfterFailures}`);
  outcomes.push("root-chain parser rejects relative, repeated-separator, dot-dot, ADS, UNC, device, volume-GUID, missing, and non-directory roots; 100 repeated parser failures showed no process-handle-count increase");
  const handlesBeforeWalkFailures = addon.processHandleCount();
  for (let i = 0; i < 100; i++) {
    assert.throws(() => addon.openRelative(path.join(root, "missing-root-suffix"), "safe\\\\nested\\\\ok.txt"));
  }
  const handlesAfterWalkFailures = addon.processHandleCount();
  assert.equal(handlesAfterWalkFailures, handlesBeforeWalkFailures,
    `process handle count changed after repeated root-walk failures: before=${handlesBeforeWalkFailures}, after=${handlesAfterWalkFailures}`);
  outcomes.push("100 failures after opening multiple configured-root components showed no process-handle-count increase");

  // Parent-handle ownership gate: caller owns the returned native handle until
  // explicit close; rename/reparse replacement must not change its file identity.
  const parentSafe = path.join(root, "parent-safe");
  const parentHeld = path.join(root, "parent-held");
  fs.mkdirSync(parentSafe);
  fs.writeFileSync(path.join(parentSafe, "marker.txt"), "PARENT-IDENTITY");
  fs.writeFileSync(path.join(parentSafe, "collision.txt"), "DO-NOT-OVERWRITE");
  fs.writeFileSync(path.join(outside, "marker.txt"), "OUTSIDE-MARKER");
  if (typeof global.gc === "function") {
    for (let i = 0; i < 5; i++) { global.gc(); await new Promise(resolve => setImmediate(resolve)); }
  }
  const parentHandlesBefore = addon.processHandleCount();
  let parentRef = null;
  try {
    parentRef = addon.openParentRelative(root, "parent-safe");
    assert.equal(parentRef.opened, true);
    const collisionHandlesBefore = addon.processHandleCount();
    assert.throws(() => addon.createChildExclusive(parentRef, "collision.txt"),
      /exclusive child creation blocked/i, "FILE_CREATE must reject an existing leaf");
    assert.equal(fs.readFileSync(path.join(parentSafe, "collision.txt"), "utf8"), "DO-NOT-OVERWRITE",
      "exclusive create modified the existing collision target");
    assert.equal(addon.processHandleCount(), collisionHandlesBefore,
      "FILE_CREATE collision path leaked a child HANDLE");
    assert.throws(() => addon.createChildExclusive(parentRef, "trailing-dot."));
    assert.throws(() => addon.createChildExclusive(parentRef, "CON.txt"));
    outcomes.push("FILE_CREATE rejects existing collision without changing bytes and rejects unsafe leaf names");
    // Native ownership is bound to the object via napi_wrap, not a mutable JS property.
    parentRef._nativeParentHandle = { forged: true };
    delete parentRef._nativeParentHandle;
    assert.equal(addon.inspectParentHandle(parentRef).isDirectory, true,
      "mutating/removing a legacy-looking property detached native ownership");
    const alias = { _nativeParentHandle: parentRef._nativeParentHandle };
    assert.throws(() => addon.inspectParentHandle(alias),
      "copying a legacy-looking property must not alias native ownership");
    outcomes.push("napi_wrap/napi_unwrap ownership survives mutation/deletion of a legacy-looking JS property; a copied plain object cannot alias the native owner");
    const identityBefore = addon.inspectParentHandle(parentRef);
    assert.equal(identityBefore.isDirectory, true);
    assert.equal(identityBefore.isReparsePoint, false);
    fs.renameSync(parentSafe, parentHeld);
    fs.symlinkSync(outside, parentSafe, "junction");
    const identityAfter = addon.inspectParentHandle(parentRef);
    assert.deepEqual(identityAfter, identityBefore,
      "held parent handle identity changed after rename and junction replacement");
    const createHandlesBefore = addon.processHandleCount();
    const created = addon.createChildExclusive(parentRef, "created-under-held-parent.txt");
    assert.equal(created.created, true);
    assert.equal(created.size, 0);
    assert.equal(fs.statSync(path.join(parentHeld, "created-under-held-parent.txt")).size, 0,
      "new file was not created under the renamed held parent");
    assert.equal(fs.existsSync(path.join(outside, "created-under-held-parent.txt")), false,
      "new file escaped through the old-path junction");
    assert.equal(addon.processHandleCount(), createHandlesBefore,
      "exclusive create leaked its child HANDLE");
    const createdIdentity = addon.inspectChildFromParent(parentRef, "created-under-held-parent.txt");
    const heldParentIdentity = addon.inspectParentHandle(parentRef);
    assert.equal(createdIdentity.isRegularFile, true);
    assert.equal(createdIdentity.isReparsePoint, false);
    assert.equal(createdIdentity.size, 0);
    assert.equal(createdIdentity.fileId.length, 32);
    assert.equal(createdIdentity.volumeSerialNumber, heldParentIdentity.volumeSerialNumber,
      "created child volume identity differs from its held parent volume");
    assert.deepEqual(addon.inspectChildFromParent(parentRef, "created-under-held-parent.txt"), createdIdentity,
      "child file ID / volume identity changed on a repeated handle-relative readback");
    assert.notEqual(createdIdentity.fileId, "00000000000000000000000000000000",
      "created child returned an all-zero file identifier");
    const independentStat = fs.statSync(path.join(parentHeld, "created-under-held-parent.txt"), { bigint: true });
    const fileIdBytes = createdIdentity.fileId.match(/../g);
    const fileIdLow64 = BigInt("0x" + fileIdBytes.slice(0, 8).reverse().join(""));
    assert.equal(independentStat.size, 0n);
    assert.equal(independentStat.ino, fileIdLow64,
      "Node bigint stat inode does not match the low 64 bits of FILE_ID_INFO");
    assert.throws(() => addon.inspectChildFromParent(parentRef, "missing-identity.txt"));
    assert.throws(() => addon.inspectChildFromParent(parentRef, "..\\\\marker.txt"));
    assert.throws(() => addon.inspectChildFromParent(parentRef, "identity.txt:stream"));
    outcomes.push("new child FILE_ID_INFO and volume serial read back through held parent; regular-file metadata, stable identity, and independent Node bigint stat inode comparison verified");
    outcomes.push("FILE_CREATE created a new zero-byte file relative to the held parent after ancestor rename and old-path junction replacement; no outside file or handle leak");

    const createRaceLeaf = "same-name-concurrent.txt";
    const raceToken = "create-race-" + process.pid + "-" + Date.now();
    const readyFiles = Array.from({ length: 8 }, (_, i) => path.join(base, raceToken + "-ready-" + i));
    const reportFiles = Array.from({ length: 8 }, (_, i) => path.join(base, raceToken + "-report-" + i));
    const goFile = path.join(base, raceToken + "-go");
    const createRaceScript = `
      const fs = require("node:fs");
      const addon = require("./build/Release/napi_probe_rootchain_acceptance.node");
      const root = process.argv[1], parentName = process.argv[2], leaf = process.argv[3];
      const ready = process.argv[4], go = process.argv[5], reportPath = process.argv[6];
      fs.writeFileSync(ready, "ready");
      const wait = new Int32Array(new SharedArrayBuffer(4));
      while (!fs.existsSync(go)) Atomics.wait(wait, 0, 0, 5);
      let parent, report;
      try {
        parent = addon.openParentRelative(root, parentName);
        const result = addon.createChildExclusive(parent, leaf);
        report = { result: "created", size: result.size };
      } catch (error) {
        report = { result: "collision", message: error.message };
      } finally { if (parent) addon.closeParentHandle(parent); }
      fs.writeFileSync(reportPath, JSON.stringify(report));
    `;
    const raceWorkers = Array.from({ length: 8 }, (_, i) => spawn(process.execPath,
      ["-e", createRaceScript, root, "parent-held", createRaceLeaf, readyFiles[i], goFile, reportFiles[i]],
      { cwd: __dirname, stdio: "ignore" }));
    const readyDeadline = Date.now() + 15000;
    while (!readyFiles.every(file => fs.existsSync(file))) {
      if (Date.now() > readyDeadline) {
        for (const proc of raceWorkers) proc.kill();
        throw new Error("create race workers did not all reach the filesystem barrier");
      }
      await new Promise(resolve => setTimeout(resolve, 10));
    }
    fs.writeFileSync(goFile, "go");
    const raceExitCodes = await Promise.all(raceWorkers.map(proc => new Promise((resolve, reject) => {
      proc.once("error", reject);
      proc.once("close", resolve);
    })));
    assert.deepEqual(raceExitCodes, Array(8).fill(0), "all concurrent FILE_CREATE workers must exit cleanly");
    const raceReports = reportFiles.map(file => JSON.parse(fs.readFileSync(file, "utf8")));
    raceWorkers.length = 0;
    if (typeof global.gc !== "function") throw new Error("native probe harness must run with --expose-gc for deterministic child-process HANDLE cleanup");
    for (let i = 0; i < 5; i++) {
      global.gc();
      await new Promise(resolve => setImmediate(resolve));
    }
    assert.equal(raceReports.filter(report => report.result === "created").length, 1,
      "exactly one concurrent FILE_CREATE must succeed");
    assert.equal(raceReports.filter(report => report.result === "collision").length, 7,
      "all losing concurrent FILE_CREATE calls must collide");
    assert.equal(fs.statSync(path.join(parentHeld, createRaceLeaf)).size, 0);
    outcomes.push("8 independent Node processes synchronized on one FILE_CREATE leaf: exactly one created, seven collided");

    const loopHandlesBefore = addon.processHandleCount();
    for (let i = 0; i < 30; i++) {
      const leaf = "repeat-create-" + i + ".txt";
      assert.equal(addon.createChildExclusive(parentRef, leaf).created, true);
      assert.throws(() => addon.createChildExclusive(parentRef, leaf), /exclusive child creation blocked/i);
    }
    assert.equal(addon.processHandleCount(), loopHandlesBefore,
      "repeated FILE_CREATE success/collision loops changed process handle count");
    outcomes.push("30 repeated FILE_CREATE success/collision pairs completed without process HANDLE growth");

    const unicodeLeaf = "café-漢字-😀.txt";
    assert.equal(addon.createChildExclusive(parentRef, unicodeLeaf).created, true);
    assert.equal(fs.statSync(path.join(parentHeld, unicodeLeaf)).size, 0);
    assert.throws(() => addon.createChildExclusive(parentRef, "ads-target.txt:stream"));
    assert.throws(() => addon.createChildExclusive(parentRef, "ads-target.txt::$DATA"));
    assert.throws(() => addon.createChildExclusive(parentRef, "ads-target.txt:stream:$DATA"));
    assert.throws(() => addon.createChildExclusive(parentRef, "bad\\u0001name.txt"));
    const caseFoldLeaf = "CaseFold-Atomic.txt";
    assert.equal(addon.createChildExclusive(parentRef, caseFoldLeaf).created, true);
    assert.throws(() => addon.createChildExclusive(parentRef, "casefold-atomic.TXT"),
      /exclusive child creation blocked/i, "case-insensitive FILE_CREATE must collide with an existing differently-cased leaf");
    assert.equal(fs.existsSync(path.join(parentHeld, caseFoldLeaf)), true);
    assert.equal(fs.existsSync(path.join(parentHeld, "casefold-atomic.TXT")), true,
      "Windows case-insensitive lookup should resolve the existing case-folded leaf");
    const composedName = "café-normalization.txt";
    const decomposedName = "café-normalization.txt";
    assert.equal(addon.createChildExclusive(parentRef, composedName).created, true);
    let normalizationOutcome = "distinct";
    try {
      assert.equal(addon.createChildExclusive(parentRef, decomposedName).created, true);
      assert.notDeepEqual(addon.inspectChildFromParent(parentRef, composedName).fileId,
        addon.inspectChildFromParent(parentRef, decomposedName).fileId,
        "distinct normalization forms must not alias one file identity");
    } catch (error) {
      if (!/exclusive child creation blocked/i.test(error.message)) throw error;
      normalizationOutcome = "collided";
    }
    outcomes.push("Unicode BMP/supplementary creation passed; ADS aliases/control names rejected; ASCII case-fold collision preserved the existing file; canonical Unicode forms were " + normalizationOutcome + " on this filesystem");

    const postCreateHandlesBefore = addon.processHandleCount();
    const postCreateFailuresBefore = addon.closeFailureCount();
    const ambiguousLeaf = "created-despite-close-status-failure.txt";
    addon.failNextCloseForTest();
    assert.throws(() => addon.createChildExclusive(parentRef, ambiguousLeaf), /CloseHandle failed after exclusive child creation/i);
    assert.equal(addon.closeFailureCount(), postCreateFailuresBefore + 1);
    assert.equal(addon.processHandleCount(), postCreateHandlesBefore);
    assert.equal(fs.statSync(path.join(parentHeld, ambiguousLeaf)).size, 0,
      "post-create close-status failure unexpectedly removed the already-created file");
    outcomes.push("injected post-create close-status failure is surfaced and counted with no HANDLE leak; created zero-byte file remains (explicit outcome ambiguity)");
    const heldRead = addon.readChildFromParent(parentRef, "marker.txt", 64);
    assert.equal(heldRead.content.toString("utf8"), "PARENT-IDENTITY",
      "child read escaped the held parent after rename/junction replacement");
    assert.equal(heldRead.size, 15);
    assert.equal(fs.readFileSync(path.join(outside, "marker.txt"), "utf8"), "OUTSIDE-MARKER",
      "outside canary unexpectedly changed");
    assert.throws(() => addon.readChildFromParent(parentRef, "..\\\\marker.txt", 64));
    assert.throws(() => addon.readChildFromParent(parentRef, "sub\\\\marker.txt", 64));
    for (const invalidLeaf of [
      "trailing-dot.", "trailing-space ", "CON", "con.txt", "CON .txt", "PRN", "AUX.log",
      "NUL", "CONIN$", "CONOUT$", "COM1", "LPT9", "COM¹", "LPT²", "bad:name", "bad?name"
    ]) {
      assert.throws(() => addon.readChildFromParent(parentRef, invalidLeaf, 64),
        undefined, "unsafe Windows leaf name was accepted: " + JSON.stringify(invalidLeaf));
    }
    outcomes.push("Windows leaf-name policy rejects trailing dot/space, reserved DOS device basenames including superscript COM/LPT forms, controls/forbidden punctuation");
    assert.throws(() => addon.readChildFromParent(parentRef, "marker.txt", 4), /cap/i);
    assert.throws(() => addon.readChildFromParent(parentRef, "missing.txt", 64));
    fs.mkdirSync(path.join(parentHeld, "child-directory"));
    assert.throws(() => addon.readChildFromParent(parentRef, "child-directory", 64));
    let childJunctionCreated = false;
    try {
      fs.symlinkSync(outside, path.join(parentHeld, "child-junction"), "junction");
      childJunctionCreated = true;
      assert.throws(() => addon.readChildFromParent(parentRef, "child-junction", 64));
    } catch (error) {
      if (!["EPERM", "EACCES", "UNKNOWN"].includes(error.code)) throw error;
    }
    const childHandlesBefore = addon.processHandleCount();
    for (let i = 0; i < 25; i++) {
      assert.equal(addon.readChildFromParent(parentRef, "marker.txt", 64).content.toString("utf8"), "PARENT-IDENTITY");
      assert.throws(() => addon.readChildFromParent(parentRef, "missing.txt", 64));
      assert.throws(() => addon.readChildFromParent(parentRef, "marker.txt", 4));
    }
    assert.equal(addon.processHandleCount(), childHandlesBefore,
      "child handle success/failure paths leaked process handles");
    outcomes.push("readChildFromParent stayed on held in-root parent after rename/junction replacement; invalid names, missing child, directory child, cap rejection, and repeated child-handle cleanup passed");
    outcomes.push(childJunctionCreated
      ? "child reparse-point junction rejection passed"
      : "child reparse-point junction test skipped: fixture creation was denied");
  } finally {
    if (parentRef) {
      assert.equal(addon.closeParentHandle(parentRef), true);
      assert.equal(addon.closeParentHandle(parentRef), true, "explicit close must be idempotent");
      assert.throws(() => addon.inspectParentHandle(parentRef), /closed|invalid/i);
    }
  }
  const parentHandlesAfter = addon.processHandleCount();
  assert.equal(parentHandlesAfter, parentHandlesBefore,
    `parent-handle success/close leaked a process handle: before=${parentHandlesBefore}, after=${parentHandlesAfter}`);
  const closeFailuresBefore = addon.closeFailureCount();
  const closeFaultHandlesBefore = addon.processHandleCount();
  const closeFaultRef = addon.openParentRelative(root, "parent-held");
  assert.equal(addon.processHandleCount(), closeFaultHandlesBefore + 1);
  addon.failNextCloseForTest();
  assert.throws(() => addon.closeParentHandle(closeFaultRef), /CloseHandle failed/i);
  assert.equal(addon.closeFailureCount(), closeFailuresBefore + 1,
    "injected close failure was not observable in the native failure counter");
  assert.equal(addon.processHandleCount(), closeFaultHandlesBefore,
    "injected post-close failure leaked the OS handle");
  assert.equal(addon.closeParentHandle(closeFaultRef), true,
    "consumed ownership must make a subsequent close idempotent");
  assert.equal(addon.closeFailureCount(), closeFailuresBefore + 1,
    "idempotent close retried a consumed handle");
  outcomes.push("injected explicit CloseHandle status failure is surfaced, counted, consumes ownership exactly once, and does not leak or double-close");

  const transitionFailuresBefore = addon.closeFailureCount();
  const transitionHandlesBefore = addon.processHandleCount();
  addon.failNextCloseForTest();
  assert.throws(() => addon.openRelative(root, "safe\\nested\\ok.txt"), /failed to release prior root-chain handle/i);
  assert.equal(addon.closeFailureCount(), transitionFailuresBefore + 1,
    "failed intermediate-handle release was not counted");
  assert.equal(addon.processHandleCount(), transitionHandlesBefore,
    "failed intermediate-handle release leaked a handle or retained the next handle");
  outcomes.push("injected intermediate directory-handle close failure aborts the walk, is counted, and leaves no leaked HANDLE");

  const failedParentHandlesBefore = addon.processHandleCount();
  for (let i = 0; i < 100; i++) {
    assert.throws(() => addon.openParentRelative(root, "parent-safe\\missing"));
    assert.throws(() => addon.openParentRelative(root, "..\\outside"));
    assert.throws(() => addon.openParentRelative(root, "parent-safe\\"));
  }
  assert.equal(addon.processHandleCount(), failedParentHandlesBefore,
    "repeated parent-handle failure paths leaked process handles");
  outcomes.push("parent-handle explicit/double close and 300 invalid/missing/trailing-path failures showed stable process handle count");

  const read = addon.openRelative(root, "safe\\nested\\ok.txt", 0, 64);
  assert.equal(Buffer.isBuffer(read.content), true);
  assert.equal(read.content.toString("utf8"), "within-root");
  outcomes.push("bounded file content read from the verified final handle");
  assert.throws(() => addon.openRelative(root, "safe\\nested\\ok.txt", 0, 5), /read-size cap/i);
  outcomes.push("oversized file read rejected before allocation/read");

  assert.throws(() => addon.openRelative(root, "..\\outside\\secret.txt"), /dot-dot|relative/i);
  outcomes.push("dot-dot traversal rejected");
  assert.throws(() => addon.openRelative(root, "safe\\missing.txt"));
  outcomes.push("missing target fails closed");
  assert.throws(() => addon.openRelative(root, "safe\\nested"));
  outcomes.push("directory cannot be opened as final file");

  const junction = path.join(root, "jump");
  try {
    fs.symlinkSync(outside, junction, "junction");
    assert.throws(() => addon.openRelative(root, "jump\\secret.txt"));
    outcomes.push("static junction traversal rejected");
  } catch (e) {
    if (e.code === "EPERM" || e.code === "EACCES" || e.code === "UNKNOWN") {
      outcomes.push("static junction test skipped: fixture creation blocked by policy");
    } else {
      throw e;
    }
  }

  const rootLink = path.join(base, "root-link");
  try {
    fs.symlinkSync(outside, rootLink, "junction");
    assert.throws(() => addon.openRelative(rootLink, "secret.txt"));
    outcomes.push("configured root that is itself a junction rejected");
  } catch (e) {
    if (e.code === "EPERM" || e.code === "EACCES" || e.code === "UNKNOWN") {
      outcomes.push("root-junction test skipped: fixture creation blocked by policy");
    } else {
      throw e;
    }
  }

  const rootAncestorLink = path.join(base, "root-ancestor");
  try {
    fs.mkdirSync(path.join(outside, "root-child"));
    fs.writeFileSync(path.join(outside, "root-child", "secret.txt"), "outside-ancestor-target");
    fs.symlinkSync(outside, rootAncestorLink, "junction");
    assert.throws(() => addon.openRelative(path.join(rootAncestorLink, "root-child"), "secret.txt"));
    outcomes.push("configured root path rejects junction in an ancestor component");
  } catch (e) {
    if (e.code === "EPERM" || e.code === "EACCES" || e.code === "UNKNOWN") {
      outcomes.push("root-ancestor junction test skipped: fixture creation blocked by policy");
    } else {
      throw e;
    }
  }

  // Synchronized adversarial swap: the child blocks after opening root\\safe.
  // Rename that directory and replace its old name with a junction to outside until the parent releases it.
  const safePath = path.join(root, "safe");
  const heldPath = path.join(root, "safe-held");
  fs.mkdirSync(path.join(safePath, "inner"), { recursive: true });
  fs.writeFileSync(path.join(safePath, "inner", "secret.txt"), "INSIDE-OK");
  fs.writeFileSync(path.join(outside, "secret.txt"), "OUTSIDE-SECRET-HAS-A-DIFFERENT-LENGTH");
  child = spawn(process.execPath, [path.join(__dirname, "handle-relative-race-worker.cjs"), "descendant", root], { cwd: __dirname, stdio: ["pipe", "pipe", "pipe"] });
  child.once("close", () => closedWorkers.add(child));
  let stdout = "", stderr = "";
  child.stdout.on("data", chunk => { stdout += chunk.toString(); });
  child.stderr.on("data", chunk => { stderr += chunk.toString(); });
  await new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error("race worker did not announce start")), 5000);
    child.stdout.on("data", chunk => { if (stdout.includes("STARTING")) { clearTimeout(timer); resolve(); } });
    child.once("error", reject);
  });
  await new Promise((resolve, reject) => {
    const configuredTimeout = Number(process.env.RAYA_NATIVE_PROBE_SIGNAL_TIMEOUT_MS || 5000);
    const timeoutMs = Number.isInteger(configuredTimeout) && configuredTimeout >= 100 && configuredTimeout <= 5000 ? configuredTimeout : 5000;
    const timer = setTimeout(() => reject(new Error("race worker did not signal parent-handle open")), timeoutMs);
    const check = () => {
      if (stdout.includes("PARENT_HANDLE_OPENED")) {
        clearTimeout(timer);
        resolve();
      }
    };
    child.stdout.on("data", check);
    check();
  });
  if (process.env.RAYA_NATIVE_PROBE_FAULT === "assertion-descendant") {
    throw new Error("injected assertion failure after descendant worker synchronized");
  }
  fs.renameSync(safePath, heldPath);
  try {
    if (process.env.RAYA_NATIVE_PROBE_FAULT === "junction-denial-descendant") {
      const denied = new Error("injected junction creation denial");
      denied.code = "EPERM";
      throw denied;
    }
    fs.symlinkSync(outside, safePath, "junction");
  } catch (e) {
    fs.renameSync(heldPath, safePath);
    child.kill();
    if (e.code === "EPERM" || e.code === "EACCES" || e.code === "UNKNOWN") {
      outcomes.push("junction swap test skipped: fixture creation blocked by policy");
    } else throw e;
  }
  if (fs.existsSync(safePath) && fs.lstatSync(safePath).isSymbolicLink()) {
    child.stdin.write("SWAP_DONE\n");
    const exitCode = await new Promise(resolve => child.once("close", resolve));
    assert.equal(exitCode, 0, "race worker failed: " + stderr + stdout);
    const line = stdout.split(/\r?\n/).find(s => s.startsWith("{"));
    assert.ok(line, "race worker returned no JSON result: " + stdout + stderr);
    const report = JSON.parse(line);
    assert.equal(report.ok, true, "handle-relative open failed: " + JSON.stringify(report));
    assert.equal(report.result.size, 9, "opened the wrong file after ancestor swap");
    assert.equal(report.result.content, "INSIDE-OK", "read content from the wrong target after ancestor swap");
    outcomes.push("synchronized ancestor swap: junction installed only after native parent-handle-open signal; read stayed on held in-root directory");
  }
  // Root-chain race: hold the root-race directory handle, then replace its next
  // component (branch) with a junction to the outside directory before the native
  // root walk opens that component. The only acceptable outcome is fail-closed.
  const rootRaceParent = path.join(base, "root-race");
  const branchPath = path.join(rootRaceParent, "branch");
  const branchHeldPath = path.join(rootRaceParent, "branch-held");
  const configuredRoot = path.join(branchPath, "child");
  fs.mkdirSync(path.join(configuredRoot), { recursive: true });
  fs.writeFileSync(path.join(configuredRoot, "secret.txt"), "ROOT-INSIDE-OK");
  fs.writeFileSync(path.join(outside, "secret.txt"), "ROOT-OUTSIDE-SECRET");
  rootRaceChild = spawn(process.execPath,
    [path.join(__dirname, "handle-relative-race-worker.cjs"), "root-chain", configuredRoot],
    { cwd: __dirname, stdio: ["pipe", "pipe", "pipe"] });
  rootRaceChild.once("close", () => closedWorkers.add(rootRaceChild));
  let rootRaceStdout = "", rootRaceStderr = "";
  rootRaceChild.stdout.on("data", chunk => { rootRaceStdout += chunk.toString(); });
  rootRaceChild.stderr.on("data", chunk => { rootRaceStderr += chunk.toString(); });
  await new Promise((resolve, reject) => {
    const timer = setTimeout(() => { rootRaceChild.kill(); reject(new Error("root-chain worker did not announce start; stdout=" + rootRaceStdout + "; stderr=" + rootRaceStderr)); }, 5000);
    const check = () => {
      if (rootRaceStdout.includes("STARTING")) { clearTimeout(timer); resolve(); }
    };
    rootRaceChild.stdout.on("data", check);
    rootRaceChild.once("error", reject);
    check();
  });
  await new Promise((resolve, reject) => {
    const timer = setTimeout(() => { rootRaceChild.kill(); reject(new Error("root-chain worker did not signal opened root-race component; stdout=" + rootRaceStdout + "; stderr=" + rootRaceStderr)); }, 5000);
    const check = () => {
      if (rootRaceStdout.includes("ROOT_COMPONENT_OPENED:root-race")) { clearTimeout(timer); resolve(); }
    };
    rootRaceChild.stdout.on("data", check);
    check();
  });
  let rootSwapInstalled = false;
  try {
    fs.renameSync(branchPath, branchHeldPath);
    fs.symlinkSync(outside, branchPath, "junction");
    rootSwapInstalled = true;
  } catch (e) {
    if (fs.existsSync(branchHeldPath) && !fs.existsSync(branchPath)) fs.renameSync(branchHeldPath, branchPath);
    rootRaceChild.kill();
    if (e.code === "EPERM" || e.code === "EACCES" || e.code === "UNKNOWN") {
      outcomes.push("root-chain ancestor-swap race test skipped: junction creation blocked by policy");
    } else throw e;
  }
  if (rootSwapInstalled) {
    rootRaceChild.stdin.write("ROOT_SWAP_DONE\n");
    const rootRaceExit = await new Promise(resolve => rootRaceChild.once("close", resolve));
    assert.equal(rootRaceExit, 0, "root-chain race worker failed: " + rootRaceStderr + rootRaceStdout);
    const rootRaceLine = rootRaceStdout.split(/\r?\n/).find(s => s.startsWith("{"));
    assert.ok(rootRaceLine, "root-chain race worker returned no JSON: " + rootRaceStdout + rootRaceStderr);
    const rootRaceReport = JSON.parse(rootRaceLine);
    assert.equal(rootRaceReport.ok, true, "root-chain race did not fail closed: " + JSON.stringify(rootRaceReport));
    assert.equal(rootRaceReport.failedClosed, true, "root-chain race unexpectedly opened a configured root");
    outcomes.push("synchronized configured-root-chain race: replaced next root component with an outside junction after parent-handle signal; native root walk failed closed");
  }
  console.log(JSON.stringify({ ok: true, outcomes }, null, 2));
} catch (error) {
  primaryError = error;
  throw error;
} finally {
  const cleanupErrors = [];
  async function stopWorker(proc, label) {
    if (!proc) return;
    try {
      if (!closedWorkers.has(proc) && proc.exitCode === null && proc.signalCode === null) proc.kill();
      if (!closedWorkers.has(proc)) {
        await new Promise((resolve, reject) => {
          const timer = setTimeout(() => reject(new Error(label + " did not close after termination")), 3000);
          proc.once("close", () => {
            clearTimeout(timer);
            closedWorkers.add(proc);
            resolve();
          });
        });
      }
    } catch (error) {
      cleanupErrors.push(label + " cleanup: " + error.message);
    }
  }
  function removeExpectedJunction(linkPath, expectedTarget) {
    try {
      const stat = fs.lstatSync(linkPath);
      if (!stat.isSymbolicLink()) return;
      const actualTarget = path.resolve(path.dirname(linkPath), fs.readlinkSync(linkPath));
      if (actualTarget.toLowerCase() !== path.resolve(expectedTarget).toLowerCase()) {
        throw new Error("refusing to remove junction with unexpected target: " + linkPath);
      }
      fs.rmSync(linkPath);
    } catch (error) {
      if (error.code !== "ENOENT") cleanupErrors.push("junction cleanup " + linkPath + ": " + error.message);
    }
  }
  await stopWorker(child, "descendant race worker");
  await stopWorker(rootRaceChild, "root-chain race worker");
  if ([child, rootRaceChild].some(proc => proc && !closedWorkers.has(proc))) {
    cleanupErrors.push("fixture retained because a worker may still be running");
    const message = "Native race harness cleanup failed:\\n- " + cleanupErrors.join("\\n- ");
    if (primaryError) throw new AggregateError([primaryError, new Error(message)], message);
    throw new Error(message);
  }
  const safePath = path.join(root, "safe");
  const heldPath = path.join(root, "safe-held");
  const jump = path.join(root, "jump");
  const rootLink = path.join(base, "root-link");
  const rootAncestorLink = path.join(base, "root-ancestor");
  const parentSafe = path.join(root, "parent-safe");
  const parentHeld = path.join(root, "parent-held");
  const branchPath = path.join(base, "root-race", "branch");
  const branchHeldPath = path.join(base, "root-race", "branch-held");
  for (const [linkPath, target] of [
    [jump, outside],
    [parentSafe, outside],
    [safePath, outside],
    [rootLink, outside],
    [rootAncestorLink, outside],
    [branchPath, outside]
  ]) removeExpectedJunction(linkPath, target);
  try {
    if (fs.existsSync(heldPath) && !fs.existsSync(safePath)) fs.renameSync(heldPath, safePath);
  } catch (error) { cleanupErrors.push("restore safe directory: " + error.message); }
  try {
    if (fs.existsSync(parentHeld) && !fs.existsSync(parentSafe)) fs.renameSync(parentHeld, parentSafe);
  } catch (error) { cleanupErrors.push("restore parent-safe directory: " + error.message); }
  try {
    if (fs.existsSync(branchHeldPath) && !fs.existsSync(branchPath)) fs.renameSync(branchHeldPath, branchPath);
  } catch (error) { cleanupErrors.push("restore root-race branch: " + error.message); }
  if (cleanupErrors.length === 0) {
    try { fs.rmSync(base, { recursive: true, force: true }); }
    catch (error) { cleanupErrors.push("remove fixture root: " + error.message); }
  }
  if (cleanupErrors.length) {
    const message = "Native race harness cleanup failed:\\n- " + cleanupErrors.join("\\n- ");
    if (primaryError) throw new AggregateError([primaryError, new Error(message)], message);
    throw new Error(message);
  }
}
}
main().catch(error => { console.error(error); process.exitCode = 1; });
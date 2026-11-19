"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const roots = require("./build/Release/napi_probe_raya_phase1_rootapi.node");
const rename = require("./build/Release/napi_probe_raya_phase1_rename.node");

function renameWithCancellationBoundary(signal, sourceParent, sourceLeaf, destinationParent, destinationLeaf) {
  if (signal?.aborted) throw Object.assign(new Error("cancelled before mutation acquisition"), { name: "AbortError" });
  // Native NtCreateFile/NtSetInformationFile executes synchronously on this Node thread.
  // Do not convert a completed commit into AbortError after returning from the native call.
  return rename.renameChildRelative(sourceParent, sourceLeaf, destinationParent, destinationLeaf);
}
function queuedCancellation(controller) {
  let ran = false;
  const promise = new Promise(resolve => setImmediate(() => { controller.abort(); ran = true; resolve(); }));
  return { promise, didRun: () => ran };
}

async function main() {
  const fixture = fs.mkdtempSync(path.join(os.tmpdir(), "raya-rename-cancel-"));
  const rootPath = path.join(fixture, "root");
  const sourcePath = path.join(rootPath, "source");
  const destinationPath = path.join(rootPath, "destination");
  const baseline = roots.processHandleCount();
  let sourceParent = null, destinationParent = null;
  try {
    fs.mkdirSync(sourcePath, { recursive: true });
    fs.mkdirSync(destinationPath, { recursive: true });
    sourceParent = roots.openParentRelative(rootPath, "source");
    destinationParent = roots.openParentRelative(rootPath, "destination");

    fs.writeFileSync(path.join(sourcePath, "pre-cancel.txt"), "PRE-CANCEL");
    const pre = new AbortController();
    pre.abort();
    assert.throws(
      () => renameWithCancellationBoundary(pre.signal, sourceParent, "pre-cancel.txt", destinationParent, "pre-cancel-moved.txt"),
      error => error.name === "AbortError"
    );
    assert.equal(fs.readFileSync(path.join(sourcePath, "pre-cancel.txt"), "utf8"), "PRE-CANCEL");
    assert.equal(fs.existsSync(path.join(destinationPath, "pre-cancel-moved.txt")), false);

    fs.writeFileSync(path.join(sourcePath, "queued-cancel.txt"), "COMMIT-WINS");
    const during = new AbortController();
    const queued = queuedCancellation(during);
    const committed = renameWithCancellationBoundary(during.signal, sourceParent, "queued-cancel.txt", destinationParent, "queued-cancel-moved.txt");
    assert.equal(committed.committed, true);
    await queued.promise;
    assert.equal(queued.didRun(), true);
    assert.equal(during.signal.aborted, true);
    assert.equal(fs.readFileSync(path.join(destinationPath, "queued-cancel-moved.txt"), "utf8"), "COMMIT-WINS");
    assert.equal(fs.existsSync(path.join(sourcePath, "queued-cancel.txt")), false);

    fs.writeFileSync(path.join(sourcePath, "post-commit-fault.txt"), "COMMITTED-FAULT");
    const after = new AbortController();
    const queuedAfter = queuedCancellation(after);
    rename.failNextCloseAfterCommitForTest();
    assert.throws(
      () => renameWithCancellationBoundary(after.signal, sourceParent, "post-commit-fault.txt", destinationParent, "post-commit-fault-moved.txt"),
      /committed_but_cleanup_failed/
    );
    await queuedAfter.promise;
    assert.equal(after.signal.aborted, true);
    assert.equal(fs.readFileSync(path.join(destinationPath, "post-commit-fault-moved.txt"), "utf8"), "COMMITTED-FAULT");
    assert.equal(fs.existsSync(path.join(sourcePath, "post-commit-fault.txt")), false);

    roots.closeParentHandle(sourceParent); sourceParent = null;
    roots.closeParentHandle(destinationParent); destinationParent = null;
    assert.equal(roots.processHandleCount(), baseline);
    console.log(JSON.stringify({
      ok: true,
      checks: [
        "pre-aborted request does not mutate source or destination",
        "cancellation queued with setImmediate cannot interrupt synchronous native rename; commit result is preserved",
        "post-commit cleanup failure remains distinguishable even when cancellation is queued",
        "all parent HANDLEs return to baseline"
      ],
      boundary: "cooperative cancellation before native call; synchronous native syscall is non-interruptible; committed result must not be rewritten as cancelled"
    }, null, 2));
  } finally {
    if (sourceParent) { try { roots.closeParentHandle(sourceParent); } catch {} }
    if (destinationParent) { try { roots.closeParentHandle(destinationParent); } catch {} }
    fs.rmSync(fixture, { recursive: true, force: true });
  }
}
main().catch(error => { console.error(error); process.exitCode = 1; });

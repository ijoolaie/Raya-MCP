"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const { spawn } = require("node:child_process");
const addon = require("./build/Release/napi_probe_raya_phase1_dircreate.node");
const workerPath = path.join(__dirname, "handle-relative-dircreate-race-worker.cjs");

async function runWorker(root, command, replaceParent, beforeRelease) {
  const child = spawn(process.execPath, [workerPath, root], {
    cwd: __dirname, stdio: ["pipe", "pipe", "pipe"], windowsHide: true
  });
  let stdout = "", stderr = "", signaled = false, closed = false;
  child.stdout.on("data", chunk => { stdout += chunk.toString(); });
  child.stderr.on("data", chunk => { stderr += chunk.toString(); });
  const closePromise = new Promise(resolve => child.once("close", code => { closed = true; resolve(code); }));
  async function reap() {
    if (closed) return;
    child.kill();
    const ok = await Promise.race([closePromise.then(() => true), new Promise(resolve => setTimeout(() => resolve(false), 5000))]);
    if (!ok) throw new Error("directory worker remained alive after termination");
  }
  try {
    await new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        child.kill();
        closePromise.then(() => reject(new Error("directory worker did not signal parent HANDLE")));
      }, 8000);
      const check = () => {
        if (stdout.includes("PARENT_HANDLE_READY")) { signaled = true; clearTimeout(timer); resolve(); }
      };
      child.stdout.on("data", check);
      child.once("error", error => { clearTimeout(timer); reject(error); });
      closePromise.then(code => {
        if (!signaled) { clearTimeout(timer); reject(new Error("directory worker exited before signal: " + code + stdout + stderr)); }
      });
      check();
    });
    if (replaceParent === "outside") {
      fs.renameSync(path.join(root, "parent"), path.join(root, "..", "outside-held-parent"));
      fs.symlinkSync(path.join(root, "..", "outside"), path.join(root, "parent"), "junction");
    } else if (replaceParent) {
      fs.renameSync(path.join(root, "parent"), path.join(root, "parent-held"));
      fs.symlinkSync(path.join(root, "..", "outside"), path.join(root, "parent"), "junction");
    }
    if (beforeRelease) beforeRelease();
    child.stdin.write(command + "\n");
    const exitCode = await new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error("directory worker timed out: " + stdout + stderr)), 10000);
      closePromise.then(code => { clearTimeout(timer); resolve(code); });
    });
    return { exitCode, stdout, stderr };
  } catch (error) {
    await reap();
    throw error;
  }
}
async function reportOf(run, label) {
  assert.equal(run.exitCode, 0, label + " worker failed: " + run.stderr + run.stdout);
  const line = run.stdout.split(/\r?\n/).find(value => value.startsWith("{"));
  assert.ok(line, label + " worker returned no JSON: " + run.stdout + run.stderr);
  const report = JSON.parse(line);
  assert.equal(report.ok, true, label + " worker reported failure: " + JSON.stringify(report));
  assert.equal(report.opened, report.baseline + 1, label + " did not own exactly one parent HANDLE");
  assert.equal(report.afterClose, report.baseline, label + " leaked its parent HANDLE");
  return report;
}
async function main() {
  const base = fs.mkdtempSync(path.join(__dirname, "fixture-dircreate-race-"));
  const root = path.join(base, "root");
  const outside = path.join(base, "outside");
  const parentPath = path.join(root, "parent");
  const heldPath = path.join(root, "parent-held");
  const leaf = path.join(parentPath, "created-dir");
  const outcomes = [];
  fs.mkdirSync(parentPath, { recursive: true });
  fs.mkdirSync(outside);
  fs.writeFileSync(path.join(parentPath, "sentinel.txt"), "IN-DIR-PARENT");
  fs.writeFileSync(path.join(outside, "sentinel.txt"), "OUTSIDE-DIR-CANARY");
  try {
    const create = await runWorker(root, "GO", true);
    const createReport = await reportOf(create, "parent-path replacement create");
    assert.equal(createReport.result.created, true);
    assert.equal(createReport.result.isDirectory, true);
    assert.equal(fs.statSync(path.join(heldPath, "created-dir")).isDirectory(), true);
    assert.equal(fs.existsSync(path.join(outside, "created-dir")), false);
    assert.equal(fs.readFileSync(path.join(heldPath, "sentinel.txt"), "utf8"), "IN-DIR-PARENT");
    assert.equal(fs.readFileSync(path.join(outside, "sentinel.txt"), "utf8"), "OUTSIDE-DIR-CANARY");
    outcomes.push("synchronized parent rename/outside-junction replacement: exclusive directory create stayed under the held in-root parent");

    fs.rmSync(parentPath, { recursive: true, force: true });
    fs.renameSync(heldPath, parentPath);
    const collision = await runWorker(root, "COLLIDE", false, () => {
      fs.writeFileSync(path.join(leaf, "marker.txt"), "DO-NOT-ALTER");
    });
    const collisionReport = await reportOf(collision, "existing-directory collision");
    assert.equal(collisionReport.result.collision, true);
    assert.equal(fs.readFileSync(path.join(leaf, "marker.txt"), "utf8"), "DO-NOT-ALTER");
    outcomes.push("synchronized existing-directory leaf collision: FILE_CREATE rejected the collision and preserved directory contents");

    fs.rmSync(leaf, { recursive: true, force: true });
    const junction = await runWorker(root, "COLLIDE", false, () => fs.symlinkSync(outside, leaf, "junction"));
    const junctionReport = await reportOf(junction, "leaf-junction collision");
    assert.equal(junctionReport.result.collision, true);
    assert.equal(fs.lstatSync(leaf).isSymbolicLink(), true);
    assert.equal(fs.existsSync(path.join(outside, "created-dir")), false);
    assert.equal(fs.readFileSync(path.join(outside, "sentinel.txt"), "utf8"), "OUTSIDE-DIR-CANARY");
    outcomes.push("synchronized leaf-junction insertion: exclusive directory create failed on collision without following or changing the outside target");

    fs.rmSync(leaf);
    const closeFault = await runWorker(root, "FAIL-CLOSE", false);
    const closeFaultReport = await reportOf(closeFault, "post-create close fault");
    assert.equal(closeFaultReport.result.committedButReportedFailed, true);
    assert.equal(closeFaultReport.closeFailuresAfter, closeFaultReport.closeFailuresBefore + 1);
    assert.equal(fs.statSync(leaf).isDirectory(), true,
      "post-create close fault should report the committed directory outcome, not roll it back");
    outcomes.push("injected post-create close-status failure: committed directory remains, failure is counted, and parent HANDLE is released");

    const recursive = await runWorker(root, "PATH", true);
    const recursiveReport = await reportOf(recursive, "recursive path after parent replacement");
    assert.deepEqual(recursiveReport.result, { createdCount: 3, existingCount: 0, committed: true });
    assert.equal(fs.statSync(path.join(heldPath, "recursive-a", "recursive-b", "recursive-c")).isDirectory(), true);
    assert.equal(fs.existsSync(path.join(outside, "recursive-a")), false);
    assert.equal(fs.readFileSync(path.join(heldPath, "sentinel.txt"), "utf8"), "IN-DIR-PARENT");
    assert.equal(fs.readFileSync(path.join(outside, "sentinel.txt"), "utf8"), "OUTSIDE-DIR-CANARY");
    outcomes.push("recursive three-component create stayed inside the original held parent after parent rename and outside-junction replacement");

    fs.rmSync(path.join(heldPath, "recursive-a"), { recursive: true, force: true });
    fs.rmSync(path.join(root, "parent"), { recursive: true, force: true });
    fs.renameSync(heldPath, parentPath);
    const outsideMove = await runWorker(root, "PATH", "outside");
    const outsideMoveReport = await reportOf(outsideMove, "parent moved outside workspace root");
    assert.deepEqual(outsideMoveReport.result, { createdCount: 3, existingCount: 0, committed: true });
    const movedParent = path.join(base, "outside-held-parent");
    assert.equal(fs.statSync(path.join(movedParent, "recursive-a", "recursive-b", "recursive-c")).isDirectory(), true);
    assert.equal(fs.existsSync(path.join(outside, "recursive-a")), false);
    outcomes.push("SECURITY FAILURE: recursive create followed the held parent HANDLE after that directory was renamed outside the workspace root");

    console.log(JSON.stringify({ ok: true, outcomes }, null, 2));
  } finally {
    try {
      const stat = fs.lstatSync(parentPath);
      if (stat.isSymbolicLink()) {
        const target = path.resolve(path.dirname(parentPath), fs.readlinkSync(parentPath));
        if (target.toLowerCase() === outside.toLowerCase()) fs.rmSync(parentPath);
      }
    } catch (error) { if (error.code !== "ENOENT") throw error; }
    if (fs.existsSync(heldPath) && !fs.existsSync(parentPath)) fs.renameSync(heldPath, parentPath);
    try {
      const stat = fs.lstatSync(path.join(parentPath, "created-dir"));
      if (stat.isSymbolicLink()) {
        const target = path.resolve(path.dirname(path.join(parentPath, "created-dir")),
          fs.readlinkSync(path.join(parentPath, "created-dir")));
        if (target.toLowerCase() === outside.toLowerCase()) fs.rmSync(path.join(parentPath, "created-dir"));
      }
    } catch (error) { if (error.code !== "ENOENT") throw error; }
    fs.rmSync(base, { recursive: true, force: true });
  }
}
main().catch(error => { console.error(error); process.exitCode = 1; });
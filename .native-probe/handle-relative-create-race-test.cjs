"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const { spawn } = require("node:child_process");
const addon = require("./build/Release/napi_probe_rootchain_acceptance.node");
const workerPath = path.join(__dirname, "handle-relative-create-race-worker.cjs");

async function settleHandles() {
  if (typeof global.gc !== "function") throw new Error("run this harness with --expose-gc");
  for (let i = 0; i < 8; i++) {
    global.gc();
    await new Promise(resolve => setImmediate(resolve));
  }
}
async function runWorker(root, command, replaceParent, beforeRelease) {
  const child = spawn(process.execPath, [workerPath, root], {
    cwd: __dirname, stdio: ["pipe", "pipe", "pipe"], windowsHide: true
  });
  let stdout = "", stderr = "", signaled = false, closed = false;
  child.stdout.on("data", chunk => { stdout += chunk.toString(); });
  child.stderr.on("data", chunk => { stderr += chunk.toString(); });
  const closePromise = new Promise(resolve => child.once("close", code => {
    closed = true;
    resolve(code);
  }));
  async function reapAfterFailure() {
    if (closed) return;
    child.kill();
    const reaped = await Promise.race([
      closePromise.then(() => true),
      new Promise(resolve => setTimeout(() => resolve(false), 5000))
    ]);
    if (!reaped) throw new Error("worker remained alive after termination; stdout=" + stdout + "; stderr=" + stderr);
  }
  try {
    await new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        child.kill();
        closePromise.then(() => reject(new Error("worker parent-handle signal timed out; output=" + stdout + stderr)));
      }, 8000);
      const check = () => {
        if (stdout.includes("PARENT_HANDLE_READY")) {
          signaled = true; clearTimeout(timer); resolve();
        }
      };
      child.stdout.on("data", check);
      child.once("error", error => { clearTimeout(timer); reject(error); });
      closePromise.then(code => {
        if (!signaled) { clearTimeout(timer); reject(new Error("worker exited before parent-handle signal: " + code + " " + stdout + stderr)); }
      });
      check();
    });
    if (replaceParent) {
      fs.renameSync(path.join(root, "parent"), path.join(root, "parent-held"));
      fs.symlinkSync(path.join(root, "..", "outside"), path.join(root, "parent"), "junction");
    }
    if (beforeRelease) beforeRelease();
    child.stdin.write(command + "\n");
    const exitCode = await new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error("worker exit timed out; stdout=" + stdout + "; stderr=" + stderr)), 10000);
      closePromise.then(code => { clearTimeout(timer); resolve(code); });
    });
    return { exitCode, stdout, stderr };
  } catch (error) {
    await reapAfterFailure();
    throw error;
  }
}
async function main() {
  const base = fs.mkdtempSync(path.join(__dirname, "fixture-create-parent-race-"));
  const root = path.join(base, "root");
  const outside = path.join(base, "outside");
  const parentPath = path.join(root, "parent");
  const heldPath = path.join(root, "parent-held");
  fs.mkdirSync(parentPath, { recursive: true });
  fs.mkdirSync(outside);
  fs.writeFileSync(path.join(parentPath, "sentinel.txt"), "IN-ROOT-SENTINEL");
  fs.writeFileSync(path.join(outside, "sentinel.txt"), "OUTSIDE-CANARY");
  const outcomes = [];
  try {
    await settleHandles();
    const race = await runWorker(root, "GO", true);
    assert.equal(race.exitCode, 0, "race worker failed: " + race.stderr + race.stdout);
    const raceLine = race.stdout.split(/\r?\n/).find(line => line.startsWith("{"));
    assert.ok(raceLine, "race worker returned no JSON: " + race.stdout + race.stderr);
    const report = JSON.parse(raceLine);
    assert.equal(report.ok, true);
    assert.equal(report.opened, report.baseline + 1, "worker did not acquire exactly one parent HANDLE");
    assert.equal(report.afterClose, report.baseline, "worker did not release its parent HANDLE after create");
    assert.equal(report.result.created.created, true);
    assert.equal(report.result.identity.isRegularFile, true);
    assert.equal(report.result.identity.isReparsePoint, false);
    assert.equal(report.result.identity.size, 0);
    assert.equal(fs.statSync(path.join(heldPath, "race-created.txt")).size, 0,
      "create did not target the directory identified by the held parent HANDLE");
    assert.equal(fs.existsSync(path.join(outside, "race-created.txt")), false,
      "create escaped through the replaced parent-path junction");
    assert.equal(fs.readFileSync(path.join(outside, "sentinel.txt"), "utf8"), "OUTSIDE-CANARY");
    assert.equal(fs.readFileSync(path.join(heldPath, "sentinel.txt"), "utf8"), "IN-ROOT-SENTINEL");
    outcomes.push("synchronized parent rename + outside-junction replacement after child process acquired parent HANDLE: FILE_CREATE remained under held in-root directory; outside canaries unchanged");

    fs.rmSync(parentPath, { recursive: true, force: true });
    fs.renameSync(heldPath, parentPath);
    const failure = await runWorker(root, "FAIL", true);
    assert.equal(failure.exitCode, 1, "injected failure did not propagate: " + failure.stderr + failure.stdout);
    const failureLine = failure.stdout.split(/\r?\n/).find(line => line.startsWith("{"));
    assert.ok(failureLine, "failure worker returned no cleanup report: " + failure.stderr + failure.stdout);
    const failureReport = JSON.parse(failureLine);
    assert.equal(failureReport.ok, false);
    assert.match(failureReport.message, /injected post-open worker failure/);
    assert.equal(failureReport.opened, failureReport.baseline + 1);
    assert.equal(failureReport.afterClose, failureReport.baseline,
      "worker failure path leaked the held parent HANDLE");
    assert.equal(fs.existsSync(path.join(heldPath, "race-created.txt")), true,
      "previous successful create unexpectedly disappeared");
    assert.equal(fs.existsSync(path.join(outside, "race-created.txt")), false,
      "failure path created an outside file");
    await settleHandles();
    outcomes.push("injected post-parent-open failure after synchronized path replacement: worker exited nonzero, finally closed its held HANDLE, and no outside file was created");

    fs.rmSync(parentPath, { recursive: true, force: true });
    fs.renameSync(heldPath, parentPath);
    const leafPath = path.join(parentPath, "race-created.txt");
    const tempLeaf = path.join(parentPath, "race-created.tmp");
    const leafCollision = await runWorker(root, "COLLIDE", false, () => {
      fs.writeFileSync(tempLeaf, "LEAF-CANARY-MUST-SURVIVE");
      fs.renameSync(tempLeaf, leafPath);
    });
    assert.equal(leafCollision.exitCode, 0, "leaf collision worker failed: " + leafCollision.stderr + leafCollision.stdout);
    const leafCollisionLine = leafCollision.stdout.split(/\r?\n/).find(line => line.startsWith("{"));
    assert.ok(leafCollisionLine, "leaf collision worker returned no JSON: " + leafCollision.stderr + leafCollision.stdout);
    const leafCollisionReport = JSON.parse(leafCollisionLine);
    assert.equal(leafCollisionReport.ok, true);
    assert.equal(leafCollisionReport.result.collision, true);
    assert.equal(leafCollisionReport.opened, leafCollisionReport.baseline + 1);
    assert.equal(leafCollisionReport.afterClose, leafCollisionReport.baseline,
      "regular-leaf collision worker leaked its parent HANDLE");
    assert.equal(fs.readFileSync(leafPath, "utf8"), "LEAF-CANARY-MUST-SURVIVE",
      "FILE_CREATE collision modified the leaf installed after parent HANDLE acquisition");
    assert.equal(fs.existsSync(path.join(outside, "race-created.txt")), false);
    outcomes.push("synchronized leaf appearance/atomic replacement after parent HANDLE acquisition: FILE_CREATE returned collision and preserved the installed leaf bytes");

    fs.rmSync(leafPath);
    const junctionCollision = await runWorker(root, "COLLIDE", false, () => {
      fs.symlinkSync(outside, leafPath, "junction");
    });
    assert.equal(junctionCollision.exitCode, 0,
      "leaf-junction collision worker failed: " + junctionCollision.stderr + junctionCollision.stdout);
    const junctionLine = junctionCollision.stdout.split(/\r?\n/).find(line => line.startsWith("{"));
    assert.ok(junctionLine, "leaf-junction worker returned no JSON: " + junctionCollision.stderr + junctionCollision.stdout);
    const junctionReport = JSON.parse(junctionLine);
    assert.equal(junctionReport.ok, true);
    assert.equal(junctionReport.result.collision, true);
    assert.equal(junctionReport.opened, junctionReport.baseline + 1);
    assert.equal(junctionReport.afterClose, junctionReport.baseline,
      "leaf-junction collision worker leaked its parent HANDLE");
    assert.equal(fs.lstatSync(leafPath).isSymbolicLink(), true);
    assert.equal(fs.existsSync(path.join(outside, "race-created.txt")), false,
      "FILE_CREATE followed a leaf junction and created a file outside the held parent");
    assert.equal(fs.readFileSync(path.join(outside, "sentinel.txt"), "utf8"), "OUTSIDE-CANARY");
    outcomes.push("synchronized leaf junction insertion after parent HANDLE acquisition: FILE_CREATE failed closed on collision without following the junction or changing outside canaries");

    fs.rmSync(leafPath);
    const readInsert = await runWorker(root, "READ", false, () => {
      fs.writeFileSync(tempLeaf, "READ-IN-ROOT-CANARY");
      fs.renameSync(tempLeaf, leafPath);
    });
    assert.equal(readInsert.exitCode, 0, "handle-relative read worker failed: " + readInsert.stderr + readInsert.stdout);
    const readInsertLine = readInsert.stdout.split(/\r?\n/).find(line => line.startsWith("{"));
    assert.ok(readInsertLine, "read worker returned no JSON: " + readInsert.stderr + readInsert.stdout);
    const readInsertReport = JSON.parse(readInsertLine);
    assert.equal(readInsertReport.ok, true);
    assert.equal(readInsertReport.result.content, "READ-IN-ROOT-CANARY");
    assert.equal(readInsertReport.result.size, 19);
    assert.equal(readInsertReport.opened, readInsertReport.baseline + 1);
    assert.equal(readInsertReport.afterClose, readInsertReport.baseline,
      "handle-relative read worker leaked its parent HANDLE");
    assert.equal(fs.existsSync(path.join(outside, "race-created.txt")), false);
    outcomes.push("synchronized in-root leaf insertion after parent HANDLE acquisition: handle-relative child read returned the exact in-root bytes and released all worker HANDLEs");

    fs.rmSync(leafPath);
    fs.writeFileSync(path.join(outside, "race-created.txt"), "READ-OUTSIDE-SECRET");
    const readJunction = await runWorker(root, "READ-BLOCK", false, () => {
      fs.symlinkSync(outside, leafPath, "junction");
    });
    assert.equal(readJunction.exitCode, 0, "read-junction worker failed: " + readJunction.stderr + readJunction.stdout);
    const readJunctionLine = readJunction.stdout.split(/\r?\n/).find(line => line.startsWith("{"));
    assert.ok(readJunctionLine, "read-junction worker returned no JSON: " + readJunction.stderr + readJunction.stdout);
    const readJunctionReport = JSON.parse(readJunctionLine);
    assert.equal(readJunctionReport.ok, true);
    assert.equal(readJunctionReport.result.blocked, true);
    assert.equal(readJunctionReport.opened, readJunctionReport.baseline + 1);
    assert.equal(readJunctionReport.afterClose, readJunctionReport.baseline,
      "blocked read worker leaked its parent HANDLE");
    assert.equal(fs.readFileSync(path.join(outside, "race-created.txt"), "utf8"), "READ-OUTSIDE-SECRET",
      "outside read canary changed during leaf-junction rejection");
    outcomes.push("synchronized outside leaf-junction insertion after parent HANDLE acquisition: handle-relative child read rejected the reparse point and did not return outside bytes");



    console.log(JSON.stringify({ ok: true, outcomes, workerHandleAccounting: "each worker returned to its own pre-open HANDLE baseline" }, null, 2));
  } finally {
    try {
      const parentStat = fs.lstatSync(parentPath);
      if (parentStat.isSymbolicLink()) {
        const target = path.resolve(path.dirname(parentPath), fs.readlinkSync(parentPath));
        if (target.toLowerCase() === outside.toLowerCase()) fs.rmSync(parentPath);
      }
    } catch (error) { if (error.code !== "ENOENT") throw error; }
    if (fs.existsSync(heldPath) && !fs.existsSync(parentPath)) fs.renameSync(heldPath, parentPath);
    try {
      const leafStat = fs.lstatSync(path.join(parentPath, "race-created.txt"));
      if (leafStat.isSymbolicLink()) {
        const target = path.resolve(path.dirname(path.join(parentPath, "race-created.txt")),
          fs.readlinkSync(path.join(parentPath, "race-created.txt")));
        if (target.toLowerCase() === outside.toLowerCase()) fs.rmSync(path.join(parentPath, "race-created.txt"));
      }
    } catch (error) { if (error.code !== "ENOENT") throw error; }
    fs.rmSync(base, { recursive: true, force: true });
  }
}
main().catch(error => { console.error(error); process.exitCode = 1; });
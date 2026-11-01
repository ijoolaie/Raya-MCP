"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { spawn } = require("node:child_process");
const worker = path.join(__dirname, "handle-relative-dirpath-worker.cjs");
async function main() {
  const fixture = fs.mkdtempSync(path.join(__dirname, "fixture-dirpath-race-"));
  const root = path.join(fixture, "root"), outside = path.join(fixture, "outside");
  const parent = path.join(root, "parent"), held = path.join(root, "parent-held");
  fs.mkdirSync(parent, { recursive: true }); fs.mkdirSync(outside);
  fs.writeFileSync(path.join(parent, "inside.txt"), "ORIGINAL-HELD-PARENT");
  fs.writeFileSync(path.join(outside, "inside.txt"), "OUTSIDE-CANARY");
  const child = spawn(process.execPath, [worker, root, "new-a\\new-b\\new-c"], { stdio: ["pipe", "pipe", "pipe"], windowsHide: true });
  let stdout = "", stderr = "";
  child.stdout.on("data", chunk => { stdout += chunk.toString(); });
  child.stderr.on("data", chunk => { stderr += chunk.toString(); });
  try {
    await new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error("worker did not acquire parent HANDLE")), 10000);
      const check = () => { if (stdout.includes("PARENT_HANDLE_READY")) { clearTimeout(timer); resolve(); } };
      child.stdout.on("data", check); child.once("error", error => { clearTimeout(timer); reject(error); });
      child.once("exit", code => { if (!stdout.includes("PARENT_HANDLE_READY")) { clearTimeout(timer); reject(new Error("worker exited early "+code+" "+stdout+stderr)); } });
      check();
    });
    fs.renameSync(parent, held); fs.symlinkSync(outside, parent, "junction"); child.stdin.write("GO\n");
    const exitCode = await new Promise((resolve, reject) => {
      const timer = setTimeout(() => { child.kill(); reject(new Error("worker timed out "+stdout+stderr)); }, 15000);
      child.once("close", code => { clearTimeout(timer); resolve(code); });
    });
    assert.equal(exitCode, 0, stderr + stdout);
    const line = stdout.split(/\r?\n/).find(x => x.startsWith("{")); assert.ok(line, stdout);
    const report = JSON.parse(line); assert.equal(report.ok, true, JSON.stringify(report));
    assert.deepEqual(report.result, { createdCount: 3, existingCount: 0, committed: true });
    assert.equal(report.afterClose, report.baseline);
    assert.ok(fs.statSync(path.join(held, "new-a", "new-b", "new-c")).isDirectory());
    assert.equal(fs.existsSync(path.join(outside, "new-a")), false);
    assert.equal(fs.readFileSync(path.join(held, "inside.txt"), "utf8"), "ORIGINAL-HELD-PARENT");
    assert.equal(fs.readFileSync(path.join(outside, "inside.txt"), "utf8"), "OUTSIDE-CANARY");
    console.log(JSON.stringify({ ok: true, checks: [
      "worker pinned parent HANDLE before namespace replacement",
      "recursive create completed inside renamed original parent",
      "outside junction target received no created components",
      "both sentinels remained unchanged",
      "worker parent HANDLE returned to process baseline"
    ] }, null, 2));
  } finally {
    if (child.exitCode === null) child.kill();
    try { const st = fs.lstatSync(parent); if (st.isSymbolicLink()) fs.rmSync(parent); } catch {}
    if (fs.existsSync(held) && !fs.existsSync(parent)) fs.renameSync(held, parent);
    fs.rmSync(fixture, { recursive: true, force: true });
  }
}
main().catch(error => { console.error(error); process.exitCode = 1; });

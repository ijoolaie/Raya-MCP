const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const addon = require(path.join(__dirname, "build", "Release", "napi_probe_raya_phase1_rootapi.node"));

async function stableHandleCount() {
  const samples = [];
  for (let i = 0; i < 8; i++) {
    await new Promise(resolve => setImmediate(resolve));
    samples.push(addon.processHandleCount());
  }
  return Math.min(...samples);
}

async function withRequestScope(rootPath, relativeParts, callback, options = {}) {
  const signal = options.signal;
  if (signal?.aborted) throw Object.assign(new Error("Request cancelled before HANDLE acquisition"), { name: "AbortError" });
  const owned = [];
  try {
    const root = addon.openWorkspaceRoot(rootPath);
    owned.push(root);
    let parent = root;
    for (const part of relativeParts.slice(0, -1)) {
      if (signal?.aborted) throw Object.assign(new Error("Request cancelled during parent acquisition"), { name: "AbortError" });
      parent = addon.openDirectoryChild(parent, part);
      owned.push(parent);
    }
    const scope = Object.freeze({
      rootHandle: root,
      parentHandle: parent,
      leaf: relativeParts.at(-1) ?? null,
      read: (name = relativeParts.at(-1)) => addon.readChildFromParent(parent, name, 256 * 1024).content
    });
    const result = await callback(scope);
    if (signal?.aborted) throw Object.assign(new Error("Request cancelled before scope completion"), { name: "AbortError" });
    return result;
  } finally {
    let closeError;
    for (let i = owned.length - 1; i >= 0; i--) {
      try { addon.closeParentHandle(owned[i]); }
      catch (error) { if (!/closed|invalid/i.test(String(error?.message || error))) closeError ??= error; }
    }
    if (closeError) throw closeError;
  }
}

async function main() {
  const fixture = fs.mkdtempSync(path.join(os.tmpdir(), "raya-request-scope-"));
  const root = path.join(fixture, "root");
  const outside = path.join(fixture, "outside");
  const baseline = addon.processHandleCount();
  try {
    fs.mkdirSync(root);
    fs.mkdirSync(path.join(root, "nested"));
    fs.mkdirSync(outside);
    fs.writeFileSync(path.join(root, "nested", "data.txt"), "ROOT-IDENTITY-CONTENT");
    fs.writeFileSync(path.join(root, "data.txt"), "ROOT-IDENTITY-CONTENT");
    fs.writeFileSync(path.join(outside, "data.txt"), "OUTSIDE-REPLACEMENT");
    
    // Independent overlapping requests own distinct root and parent wrappers.
    let releaseA, releaseB;
    const gateA = new Promise(resolve => { releaseA = resolve; });
    const gateB = new Promise(resolve => { releaseB = resolve; });
    let handlesA, handlesB;
    const a = withRequestScope(root, ["nested", "data.txt"], async scope => {
      handlesA = [scope.rootHandle, scope.parentHandle];
      await gateA;
      return Buffer.from(scope.read()).toString("utf8");
    });
    const b = withRequestScope(root, ["nested", "data.txt"], async scope => {
      handlesB = [scope.rootHandle, scope.parentHandle];
      await gateB;
      return Buffer.from(scope.read()).toString("utf8");
    });
    assert.equal(addon.processHandleCount(), baseline + 4, "two requests each hold independent root and parent HANDLEs");
    assert.notStrictEqual(handlesA[0], handlesB[0], "root owners must not be shared");
    assert.notStrictEqual(handlesA[1], handlesB[1], "parent owners must not be shared");
    releaseB();
    assert.equal(await b, "ROOT-IDENTITY-CONTENT");
    assert.equal(addon.processHandleCount(), baseline + 2, "B cleanup must not close A handles");
    assert.throws(() => addon.inspectParentHandle(handlesB[0]), /closed|invalid/i);
    assert.throws(() => addon.inspectParentHandle(handlesB[1]), /closed|invalid/i);
    assert.doesNotThrow(() => addon.inspectParentHandle(handlesA[0]));
    assert.doesNotThrow(() => addon.inspectParentHandle(handlesA[1]));
    releaseA();
    assert.equal(await a, "ROOT-IDENTITY-CONTENT");
    assert.throws(() => addon.inspectParentHandle(handlesA[0]), /closed|invalid/i);
    assert.throws(() => addon.inspectParentHandle(handlesA[1]), /closed|invalid/i);

    // Callback rejection still closes every acquired HANDLE.
    let failureOwners;
    await assert.rejects(withRequestScope(root, ["nested", "data.txt"], async scope => {
      failureOwners = [scope.rootHandle, scope.parentHandle];
      throw new Error("injected async request failure");
    }), /injected async request failure/);
    assert.throws(() => addon.inspectParentHandle(failureOwners[0]), /closed|invalid/i);
    assert.throws(() => addon.inspectParentHandle(failureOwners[1]), /closed|invalid/i);


    // Cooperative cancellation is checked at scope completion and closes handles.
    const controller = new AbortController();
    let unblock;
    const blocked = new Promise(resolve => { unblock = resolve; });
    let cancelOwners;
    const cancelled = withRequestScope(root, ["nested", "data.txt"], async scope => {
      cancelOwners = [scope.rootHandle, scope.parentHandle];
      assert.equal(Buffer.from(scope.read()).toString("utf8"), "ROOT-IDENTITY-CONTENT");
      await blocked;
      return "should-not-escape";
    }, { signal: controller.signal });
    assert.equal(addon.processHandleCount(), baseline + 2);
    controller.abort();
    unblock();
    await assert.rejects(cancelled, error => error.name === "AbortError");
    assert.throws(() => addon.inspectParentHandle(cancelOwners[0]), /closed|invalid/i);
    assert.throws(() => addon.inspectParentHandle(cancelOwners[1]), /closed|invalid/i);

    // A root renamed/replaced while the request is suspended remains bound to
    // the already-open root HANDLE, not the replacement junction target.
    const moved = path.join(fixture, "root-held");
    let swapped = false;
    let heldRoot;
    const held = withRequestScope(root, ["data.txt"], async scope => {
      heldRoot = scope.rootHandle;
      fs.renameSync(root, moved);
      fs.symlinkSync(outside, root, "junction");
      swapped = true;
      await Promise.resolve();
      return Buffer.from(scope.read()).toString("utf8");
    });
    assert.equal(await held, "ROOT-IDENTITY-CONTENT");
    assert.equal(fs.readFileSync(path.join(outside, "data.txt"), "utf8"), "OUTSIDE-REPLACEMENT");
    assert.throws(() => addon.inspectParentHandle(heldRoot), /closed|invalid/i);
    if (swapped) {
      fs.rmdirSync(root);
      fs.renameSync(moved, root);
      swapped = false;
    }

    // Cancellation before acquisition does not open a HANDLE.
    const pre = new AbortController(); pre.abort();
    await assert.rejects(withRequestScope(root, ["data.txt"], async () => "no", { signal: pre.signal }), e => e.name === "AbortError");


    console.log(JSON.stringify({ok:true,checks:12,concurrency:"isolated HANDLE owners",cleanup:"success/error/cancel",rootReplacement:"held HANDLE remains bound"}));
  } finally {
    fs.rmSync(fixture, {recursive:true,force:true});
    
  }
}
main().catch(error => { console.error(error); process.exitCode = 1; });
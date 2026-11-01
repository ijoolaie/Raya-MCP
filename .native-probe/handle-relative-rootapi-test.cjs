const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const addon = require(path.join(__dirname, "build", "Release", "napi_probe_raya_phase1_rootapi.node"));

function main() {
  const fixture = fs.mkdtempSync(path.join(os.tmpdir(), "raya-root-handle-"));
  let root = null;
  let nested = null;
  let linkInstalled = false;
  const rootPath = path.join(fixture, "workspace");
  const movedPath = path.join(fixture, "workspace-held");
  const outsidePath = path.join(fixture, "outside");
  const before = addon.processHandleCount();
  try {
    fs.mkdirSync(rootPath);
    fs.mkdirSync(outsidePath);
    fs.writeFileSync(path.join(rootPath, "root.txt"), "ROOT-LEVEL-CONTENT", "utf8");
    fs.mkdirSync(path.join(rootPath, "nested"));
    fs.writeFileSync(path.join(rootPath, "nested", "inside.txt"), "NESTED-CONTENT", "utf8");
    fs.writeFileSync(path.join(outsidePath, "root.txt"), "OUTSIDE-SECRET", "utf8");

    root = addon.openWorkspaceRoot(rootPath);
    assert.equal(root.opened, true);
    const rootInfo = addon.inspectParentHandle(root);
    assert.equal(rootInfo.isDirectory, true);
    assert.equal(rootInfo.isReparsePoint, false);
    assert.match(rootInfo.fileId, /^[0-9A-F]{32}$/);

    const rootRead = addon.readChildFromParent(root, "root.txt", 262144);
    assert.equal(Buffer.from(rootRead.content).toString("utf8"), "ROOT-LEVEL-CONTENT");
    assert.equal(rootRead.size, 18);
    const rootMeta = addon.inspectEntryMetadata(root, "root.txt");
    assert.equal(rootMeta.isDirectory, false);
    assert.equal(rootMeta.isReparsePoint, false);
    assert.equal(rootMeta.size, "18");

    nested = addon.openDirectoryChild(root, "nested");
    const nestedRead = addon.readChildFromParent(nested, "inside.txt", 262144);
    assert.equal(Buffer.from(nestedRead.content).toString("utf8"), "NESTED-CONTENT");
    assert.equal(nestedRead.size, 14);

    for (const unsafe of ["bad.", "bad ", "CON", "PRN.txt", "AUX", "NUL.log", "COM1", "LPT9", "CONIN$", "CONOUT$"]) {
      assert.throws(() => addon.openParentRelative(rootPath, unsafe), /safe Windows filename component|invalid|blocked/i, unsafe);
    }
    assert.throws(() => addon.openParentRelative(rootPath, "nested\\bad."), /safe Windows filename component|invalid|blocked/i);
    assert.throws(() => addon.openParentRelative(rootPath, ".."), /safe Windows filename component|invalid|blocked/i);

    for (const unsafeRoot of [
      "C:\\RayaInvalidRoot.\\child",
      "C:\\RayaInvalidRoot \\child",
      "C:\\CON\\child",
      "C:\\PRN.txt\\child",
      "C:\\COM1\\child",
      "C:\\LPT9\\child"
    ]) {
      assert.throws(() => addon.openWorkspaceRoot(unsafeRoot), /unsafe root path component|invalid root path component/i, unsafeRoot);
    }

    addon.closeParentHandle(nested);
    nested = null;

    fs.renameSync(rootPath, movedPath);
    fs.symlinkSync(outsidePath, rootPath, "junction");
    linkInstalled = true;
    const afterRootSwap = addon.readChildFromParent(root, "root.txt", 262144);
    assert.equal(Buffer.from(afterRootSwap.content).toString("utf8"), "ROOT-LEVEL-CONTENT");
    assert.equal(fs.readFileSync(path.join(outsidePath, "root.txt"), "utf8"), "OUTSIDE-SECRET");

    addon.closeParentHandle(root);
    root = null;
    assert.equal(addon.processHandleCount(), before);

    process.stdout.write(JSON.stringify({
      ok: true,
      checks: [
        "root HANDLE opens as an opaque owner",
        "root HANDLE identity reports a plain directory",
        "root-level file read uses the held root HANDLE",
        "root-level metadata uses the held root HANDLE",
        "nested directory open and read remain handle-relative",
        "relative parent rejects trailing dot/space and reserved device names",
        "relative parent rejects traversal",
        "root-chain rejects trailing dot/space and reserved device names",
        "root HANDLE remains bound to original directory after rename and outside-junction replacement",
        "explicit close returns process HANDLE count to baseline"
      ]
    }) + "\n");
  } finally {
    if (nested) { try { addon.closeParentHandle(nested); } catch {} }
    if (root) { try { addon.closeParentHandle(root); } catch {} }
    if (linkInstalled) { try { fs.rmdirSync(rootPath); } catch {} }
    fs.rmSync(fixture, { recursive: true, force: true });
  }
}
try { main(); } catch (error) { console.error(error); process.exitCode = 1; }
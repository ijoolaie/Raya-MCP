const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const roots = require(path.join(__dirname, "build", "Release", "napi_probe_raya_phase1_rootapi.node"));
const rename = require(path.join(__dirname, "build", "Release", "napi_probe_raya_phase1_rename.node"));

function main() {
  const fixture = fs.mkdtempSync(path.join(os.tmpdir(), "raya-rename-relative-"));
  const rootPath = path.join(fixture, "root");
  const srcDir = path.join(rootPath, "src");
  const dstDir = path.join(rootPath, "dst");
  const outside = path.join(fixture, "outside");
  const dstMoved = path.join(rootPath, "dst-held");
  const srcMoved = path.join(rootPath, "src-held");
  let root = null, src = null, dst = null, crossParent = null, crossRoot = null, linkInstalled = false;
  let crossVolumeTested = false;
  const baseline = roots.processHandleCount();
  const checks = [];
  let reparseLeafTested = false;
  const close = (h) => { if (h) roots.closeParentHandle(h); };
  try {
    fs.mkdirSync(rootPath);
    fs.mkdirSync(srcDir);
    fs.mkdirSync(dstDir);
    fs.mkdirSync(outside);
    root = roots.openWorkspaceRoot(rootPath);
    src = roots.openParentRelative(rootPath, "src");
    dst = roots.openParentRelative(rootPath, "dst");

    fs.writeFileSync(path.join(srcDir, "same-a.txt"), "SAME-A");
    let r = rename.renameChildRelative(src, "same-a.txt", src, "same-b.txt");
    assert.equal(r.committed, true);
    assert.equal(fs.readFileSync(path.join(srcDir, "same-b.txt"), "utf8"), "SAME-A");
    assert.equal(fs.existsSync(path.join(srcDir, "same-a.txt")), false);
    checks.push("same-parent relative rename commits");

    fs.writeFileSync(path.join(srcDir, "move.txt"), "MOVE-CONTENT");
    r = rename.renameChildRelative(src, "move.txt", dst, "moved.txt");
    assert.equal(r.committed, true);
    assert.equal(fs.readFileSync(path.join(dstDir, "moved.txt"), "utf8"), "MOVE-CONTENT");
    assert.equal(fs.existsSync(path.join(srcDir, "move.txt")), false);
    checks.push("distinct-parent same-volume rename commits");

    fs.writeFileSync(path.join(srcDir, "collision.txt"), "SOURCE");
    fs.writeFileSync(path.join(dstDir, "collision.txt"), "DESTINATION");
    assert.throws(() => rename.renameChildRelative(src, "collision.txt", dst, "collision.txt"));
    assert.equal(fs.readFileSync(path.join(srcDir, "collision.txt"), "utf8"), "SOURCE");
    assert.equal(fs.readFileSync(path.join(dstDir, "collision.txt"), "utf8"), "DESTINATION");
    checks.push("existing destination file is never overwritten");

    fs.writeFileSync(path.join(srcDir, "dir-collision.txt"), "SOURCE-DIR-COLLISION");
    fs.mkdirSync(path.join(dstDir, "dir-collision.txt"));
    assert.throws(() => rename.renameChildRelative(src, "dir-collision.txt", dst, "dir-collision.txt"));
    assert.equal(fs.readFileSync(path.join(srcDir, "dir-collision.txt"), "utf8"), "SOURCE-DIR-COLLISION");
    checks.push("existing destination directory blocks rename");

    fs.writeFileSync(path.join(srcDir, "junction-collision.txt"), "SOURCE-JUNCTION-COLLISION");
    fs.writeFileSync(path.join(outside, "destination-link-target.txt"), "OUTSIDE-DESTINATION");
    fs.symlinkSync(path.join(outside, "destination-link-target.txt"), path.join(dstDir, "junction-collision.txt"), "file");
    assert.throws(() => rename.renameChildRelative(src, "junction-collision.txt", dst, "junction-collision.txt"));
    assert.equal(fs.readFileSync(path.join(srcDir, "junction-collision.txt"), "utf8"), "SOURCE-JUNCTION-COLLISION");
    assert.equal(fs.readFileSync(path.join(outside, "destination-link-target.txt"), "utf8"), "OUTSIDE-DESTINATION");
    assert.equal(fs.lstatSync(path.join(dstDir, "junction-collision.txt")).isSymbolicLink(), true);
    checks.push("existing destination reparse leaf blocks rename without following or replacing it");

    fs.mkdirSync(path.join(srcDir, "directory-source"));
    assert.throws(() => rename.renameChildRelative(src, "directory-source", dst, "directory-source"));
    assert.equal(fs.statSync(path.join(srcDir, "directory-source")).isDirectory(), true);
    checks.push("directory source is rejected by the regular-file-only prototype");

    assert.throws(() => rename.renameChildRelative(src, "missing.txt", dst, "missing.txt"));
    checks.push("missing source fails closed");

    for (const bad of ["", ".", "..", "nested\\leaf", "C:leaf", "trailing.", "trailing ", "CON", "NUL.txt"]) {
      assert.throws(() => rename.renameChildRelative(src, bad, dst, "valid.txt"));
      assert.throws(() => rename.renameChildRelative(src, "valid-source.txt", dst, bad));
    }
    checks.push("unsafe source and destination leaf names rejected");

    crossRoot = fs.mkdtempSync(path.join("D:\\", "raya-rename-cross-volume-"));
    fs.mkdirSync(path.join(crossRoot, "destination"));
    crossParent = roots.openWorkspaceRoot(crossRoot);
    fs.writeFileSync(path.join(srcDir, "cross-volume.txt"), "CROSS-VOLUME-SOURCE");
    assert.throws(() => rename.renameChildRelative(src, "cross-volume.txt", crossParent, "cross-volume.txt"), /cross_volume/);
    assert.equal(fs.readFileSync(path.join(srcDir, "cross-volume.txt"), "utf8"), "CROSS-VOLUME-SOURCE");
    assert.equal(fs.existsSync(path.join(crossRoot, "cross-volume.txt")), false);
    roots.closeParentHandle(crossParent); crossParent = null;
    fs.rmSync(crossRoot, {recursive:true,force:true}); crossRoot = null;
    crossVolumeTested = true;
    checks.push("cross-volume rename is rejected before mutation");

    fs.writeFileSync(path.join(outside, "reparse-secret.txt"), "OUTSIDE-SECRET");
    try {
      fs.symlinkSync(path.join(outside, "reparse-secret.txt"), path.join(srcDir, "leaf-link.txt"), "file");
      reparseLeafTested = true;
      assert.throws(() => rename.renameChildRelative(src, "leaf-link.txt", dst, "leaf-link-moved.txt"));
      assert.equal(fs.readFileSync(path.join(outside, "reparse-secret.txt"), "utf8"), "OUTSIDE-SECRET");
      assert.equal(fs.lstatSync(path.join(srcDir, "leaf-link.txt")).isSymbolicLink(), true);
      checks.push("source reparse leaf is rejected without following or moving its target");
    } catch (error) {
      if (reparseLeafTested) throw error;
      checks.push("source reparse leaf test skipped: host did not permit creating a file symlink");
    }

    fs.writeFileSync(path.join(srcDir, "pre-fault.txt"), "PRE-COMMIT");
    rename.failNextBeforeRenameForTest();
    assert.throws(() => rename.renameChildRelative(src, "pre-fault.txt", dst, "pre-fault.txt"), /injected_pre_commit_failure/);
    assert.equal(fs.readFileSync(path.join(srcDir, "pre-fault.txt"), "utf8"), "PRE-COMMIT");
    assert.equal(fs.existsSync(path.join(dstDir, "pre-fault.txt")), false);
    checks.push("pre-commit fault leaves source unchanged");

    fs.writeFileSync(path.join(srcDir, "post-fault.txt"), "POST-COMMIT");
    rename.failNextCloseAfterCommitForTest();
    assert.throws(() => rename.renameChildRelative(src, "post-fault.txt", dst, "post-fault.txt"), /committed_but_cleanup_failed/);
    assert.equal(fs.existsSync(path.join(srcDir, "post-fault.txt")), false);
    assert.equal(fs.readFileSync(path.join(dstDir, "post-fault.txt"), "utf8"), "POST-COMMIT");
    checks.push("post-commit cleanup failure is reported distinctly");

    close(src); src = null;
    close(dst); dst = null;
    fs.renameSync(dstDir, dstMoved);
    fs.symlinkSync(outside, dstDir, "junction");
    linkInstalled = true;
    dst = roots.openParentRelative(rootPath, "dst-held");
    src = roots.openParentRelative(rootPath, "src");
    fs.writeFileSync(path.join(srcDir, "dst-swap.txt"), "DEST-PARENT-HELD");
    rename.renameChildRelative(src, "dst-swap.txt", dst, "dst-swap.txt");
    assert.equal(fs.readFileSync(path.join(dstMoved, "dst-swap.txt"), "utf8"), "DEST-PARENT-HELD");
    assert.equal(fs.existsSync(path.join(outside, "dst-swap.txt")), false);
    checks.push("destination parent rename and junction replacement cannot redirect held destination HANDLE");

    close(src); src = null;
    close(dst); dst = null;
    fs.rmdirSync(dstDir);
    linkInstalled = false;
    fs.renameSync(srcDir, srcMoved);
    fs.symlinkSync(outside, srcDir, "junction");
    linkInstalled = true;
    src = roots.openParentRelative(rootPath, "src-held");
    dst = roots.openWorkspaceRoot(rootPath);
    fs.writeFileSync(path.join(srcMoved, "src-swap.txt"), "SOURCE-PARENT-HELD");
    rename.renameChildRelative(src, "src-swap.txt", dst, "src-swap.txt");
    assert.equal(fs.readFileSync(path.join(rootPath, "src-swap.txt"), "utf8"), "SOURCE-PARENT-HELD");
    assert.equal(fs.existsSync(path.join(outside, "src-swap.txt")), false);
    checks.push("source parent rename and junction replacement cannot redirect held source HANDLE");

    close(src); src = null;
    close(dst); dst = null;
    close(root); root = null;
    assert.equal(roots.processHandleCount(), baseline);
    checks.push("all explicit parent HANDLE owners return process HANDLE count to baseline");

    process.stdout.write(JSON.stringify({ok:true,checks,checkCount:checks.length,crossVolume:crossVolumeTested?"tested across C: and D:":"not tested",reparseLeaf:reparseLeafTested?"tested":"host did not permit file symlink creation"})+"\n");
  } finally {
    close(src); close(dst); close(root); close(crossParent);
    if (crossRoot) fs.rmSync(crossRoot,{recursive:true,force:true});
    if (linkInstalled) { try { fs.rmdirSync(srcDir); } catch {} try { fs.rmdirSync(dstDir); } catch {} }
    fs.rmSync(fixture,{recursive:true,force:true});
  }
}
try { main(); } catch (e) { console.error(e); process.exitCode=1; }

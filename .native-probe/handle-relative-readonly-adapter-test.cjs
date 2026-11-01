const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const addon = require(path.join(__dirname, "build", "Release", "napi_probe_raya_phase1_rootapi.node"));

const MAX_READ_BYTES = 256 * 1024;
const FILETIME_EPOCH_TICKS = 116444736000000000n;
const TAG_MOUNT_POINT = 0xA0000003;
const TAG_SYMLINK = 0xA000000C;

function within(root, target) {
  const rel = path.relative(root, target);
  return rel === "" || (!rel.startsWith("..") && !path.isAbsolute(rel));
}
function selectRoot(input, roots) {
  const absolute = path.resolve(input);
  const root = roots.find(candidate => within(path.resolve(candidate), absolute));
  if (!root) throw new Error("Path is outside the configured workspace allowlist.");
  const rel = path.relative(path.resolve(root), absolute);
  const parts = rel === "" ? [] : rel.split(path.sep);
  if (parts.some(part => !part || part === "." || part === "..")) {
    throw new Error("Invalid relative workspace path.");
  }
  return { rootPath: path.resolve(root), absolute, parts };
}

// Test-only model of the production-facing contract: callers supply an opaque
// rootId and a relative path; only this server-owned registry contains root paths.
function createRootRegistry(configuredRoots) {
  const entries = configuredRoots.map((rootPath, index) => ({
    rootId: "root-" + (index + 1),
    rootPath: path.resolve(rootPath)
  }));
  for (let i = 0; i < entries.length; i++) {
    for (let j = i + 1; j < entries.length; j++) {
      if (within(entries[i].rootPath, entries[j].rootPath) ||
          within(entries[j].rootPath, entries[i].rootPath)) {
        throw new Error("Ambiguous configured roots overlap.");
      }
    }
  }
  return new Map(entries.map(entry => [entry.rootId, Object.freeze(entry)]));
}
function resolveRootRequest(registry, rootId, relativePath = "") {
  if (typeof rootId !== "string" || !registry.has(rootId)) {
    throw new Error("Unknown workspace rootId.");
  }
  if (typeof relativePath !== "string" || relativePath.includes("\\0") ||
      path.isAbsolute(relativePath) || path.win32.isAbsolute(relativePath) ||
      /^[A-Za-z]:/.test(relativePath)) {
    throw new Error("Workspace path must be relative to rootId.");
  }
  const parts = relativePath.split(/[\\\\/]+/).filter(Boolean);
  if (parts.some(part => part === "." || part === "..")) {
    throw new Error("Workspace relative path traversal is blocked.");
  }
  const entry = registry.get(rootId);
  const absolute = path.resolve(entry.rootPath, ...parts);
  if (!within(entry.rootPath, absolute)) throw new Error("Workspace path escaped rootId.");
  return { rootId, rootPath: entry.rootPath, absolute, parts };
}
function assertNativeCategory(callback, expected, label) {
  let caught;
  try { callback(); } catch (error) { caught = error; }
  assert.ok(caught, label + ": expected native operation to fail");
  assert.ok(String(caught.message || caught).startsWith(expected + ":"), label + ": " + String(caught.message || caught));
}
function filetimeIso(value) {
  const ms = Number((BigInt(value) - FILETIME_EPOCH_TICKS) / 10000n);
  const date = new Date(ms);
  if (!Number.isFinite(date.getTime())) throw new Error("Native FILETIME is outside supported Date range.");
  return date.toISOString();
}
function withParent(input, roots, callback, hooks = {}) {
  const selected = selectRoot(input, roots);
  const rootHandle = addon.openWorkspaceRoot(selected.rootPath);
  const owned = [rootHandle];
  try {
    hooks.afterRootOpened?.({ selected, rootHandle });
    let parent = rootHandle;
    for (const part of selected.parts.slice(0, -1)) {
      parent = addon.openDirectoryChild(parent, part);
      owned.push(parent);
      hooks.afterParentOpened?.({ selected, parent, part });
    }
    return callback({ ...selected, parent, leaf: selected.parts.at(-1) ?? null, rootHandle });
  } finally {
    for (let i = owned.length - 1; i >= 0; i--) {
      try { addon.closeParentHandle(owned[i]); } catch (error) {
        if (!/closed|invalid/i.test(String(error?.message || error))) throw error;
      }
    }
  }
}
function adapterFileRead(input, roots, hooks = {}) {
  return withParent(input, roots, ({ parent, leaf }) => {
    if (!leaf) throw new Error("Path is not a file.");
    const native = addon.readChildFromParent(parent, leaf, MAX_READ_BYTES);
    return Buffer.from(native.content).toString("utf8");
  }, hooks);
}
function adapterFileInfo(input, roots) {
  return withParent(input, roots, ({ absolute, parent, leaf, rootHandle }) => {
    const native = leaf ? addon.inspectEntryMetadata(parent, leaf) : addon.inspectParentHandle(rootHandle);
    if (native.isReparsePoint) {
      if (![TAG_MOUNT_POINT, TAG_SYMLINK].includes(native.reparseTag) || native.reparseLogicalSize == null) {
        throw new Error("Unsupported reparse tag; metadata mapping fails closed.");
      }
      return {
        path: absolute,
        type: "symlink",
        sizeBytes: Number(native.reparseLogicalSize),
        createdAt: filetimeIso(native.creationTime100ns),
        modifiedAt: filetimeIso(native.lastWriteTime100ns),
        accessedAt: filetimeIso(native.lastAccessTime100ns),
        readonly: Boolean(native.attributes & 1),
        symlink: true
      };
    }
    const type = native.isDirectory ? "directory" : "file";
    return {
      path: absolute,
      type,
      sizeBytes: Number(native.size),
      createdAt: filetimeIso(native.creationTime100ns),
      modifiedAt: filetimeIso(native.lastWriteTime100ns),
      accessedAt: filetimeIso(native.lastAccessTime100ns),
      readonly: Boolean(native.attributes & 1),
      symlink: false
    };
  });
}

function main() {
  const fixture = fs.mkdtempSync(path.join(os.tmpdir(), "raya-readonly-adapter-"));
  const root = path.join(fixture, "workspace");
  const outside = path.join(fixture, "outside");
  const roots = [root];
  const installedLinks = [];
  const beforeHandles = addon.processHandleCount();
  try {
    fs.mkdirSync(root);
    fs.mkdirSync(outside);

    // rootId is the only root selector exposed to a caller; absolute roots
    // remain exclusively in the server-owned registry.
    const secondRoot = path.join(fixture, "workspace-two");
    fs.mkdirSync(secondRoot);
    const registry = createRootRegistry([root, secondRoot]);
    assert.deepEqual(resolveRootRequest(registry, "root-1", "nested\\inside.txt").parts, ["nested", "inside.txt"]);
    assert.equal(resolveRootRequest(registry, "root-2", "").absolute, secondRoot);
    assert.throws(() => resolveRootRequest(registry, "unknown-root", "x"), /Unknown workspace rootId/);
    for (const bad of ["..\\outside\\secret.txt", "../outside/secret.txt", "C:\\\\Windows\\\\win.ini", "\\\\\\\\server\\\\share\\\\x", root]) {
      assert.throws(() => resolveRootRequest(registry, "root-1", bad), /relative|traversal/i, "reject caller-controlled absolute/traversal path: " + bad);
    }
    assert.throws(() => createRootRegistry([root, path.join(root, "nested")]), /overlap/i);
    assert.throws(() => createRootRegistry([root, root]), /overlap/i);
    fs.mkdirSync(path.join(root, "nested"));
    fs.writeFileSync(path.join(root, "root.txt"), "ROOT-LEVEL-UTF8-✓", "utf8");
    fs.writeFileSync(path.join(root, "nested", "inside.txt"), "NESTED-UTF8-سلام", "utf8");
    fs.writeFileSync(path.join(root, "nested", "invalid-utf8.bin"), Buffer.from([0x61, 0xff, 0x62]));
    fs.writeFileSync(path.join(root, "large.bin"), Buffer.alloc(MAX_READ_BYTES + 1, 0x61));
    fs.writeFileSync(path.join(outside, "secret.txt"), "OUTSIDE-SECRET", "utf8");
    const unicodeOutside = path.join(fixture, "outside-سلام");
    fs.mkdirSync(unicodeOutside);

    const rootRead = adapterFileRead(path.join(root, "root.txt"), roots);
    assert.equal(rootRead, fs.readFileSync(path.join(root, "root.txt"), "utf8"));
    const nestedRead = adapterFileRead(path.join(root, "nested", "inside.txt"), roots);
    assert.equal(nestedRead, fs.readFileSync(path.join(root, "nested", "inside.txt"), "utf8"));
    const invalidUtf8 = adapterFileRead(path.join(root, "nested", "invalid-utf8.bin"), roots);
    assert.equal(invalidUtf8, fs.readFileSync(path.join(root, "nested", "invalid-utf8.bin"), "utf8"));
    assert.throws(() => adapterFileRead(path.join(root, "large.bin"), roots), /size|cap|read/i);
    assert.throws(() => adapterFileRead(path.join(outside, "secret.txt"), roots), /allowlist/i);
    assert.throws(() => adapterFileRead(path.join(root, "..", "outside", "secret.txt"), roots), /allowlist/i);
    assert.throws(() => adapterFileRead(root, roots), /not a file/i);
    assertNativeCategory(() => adapterFileRead(path.join(root, "nested"), roots), "is_directory", "directory passed to file reader");
    assertNativeCategory(() => adapterFileRead(path.join(root, "missing.txt"), roots), "not_found", "missing leaf");
    assertNativeCategory(() => adapterFileRead(path.join(root, "bad:name"), roots), "invalid_name", "invalid child name");
    assertNativeCategory(() => adapterFileRead(path.join(root, "missing-parent", "missing.txt"), roots), "not_found", "missing parent");
    assertNativeCategory(() => adapterFileRead(path.join(fixture, "missing-root", "missing.txt"), [path.join(fixture, "missing-root")]), "not_found", "missing configured root");
    addon.failNextChildOpenStatusForTest(0xC0000022);
    assertNativeCategory(() => adapterFileRead(path.join(root, "root.txt"), roots), "access_denied", "injected native access denied");
    assertNativeCategory(() => adapterFileRead(path.join(root, "root.txt"), roots, { afterRootOpened: () => addon.failNextCloseForTest() }), "close_failure", "injected native close failure");
    assert.equal(addon.processHandleCount(), beforeHandles);
    assert.equal(addon.processHandleCount(), beforeHandles);
    assert.throws(() => adapterFileRead(path.join(root, "root.txt"), roots, {
      afterRootOpened: () => { throw new Error("injected root adapter failure"); }
    }), /injected root adapter failure/);
    assert.throws(() => adapterFileRead(path.join(root, "nested", "inside.txt"), roots, {
      afterParentOpened: () => { throw new Error("injected parent adapter failure"); }
    }), /injected parent adapter failure/);
    assert.equal(addon.processHandleCount(), beforeHandles);

    const rootInfo = adapterFileInfo(root, roots);
    assert.equal(rootInfo.type, "directory");
    assert.equal(rootInfo.path, root);
    assert.equal(rootInfo.symlink, false);
    assert.equal(typeof rootInfo.createdAt, "string");
    const fileInfo = adapterFileInfo(path.join(root, "root.txt"), roots);
    const nodeFile = fs.statSync(path.join(root, "root.txt"));
    assert.equal(fileInfo.type, "file");
    assert.equal(fileInfo.sizeBytes, nodeFile.size);
    assert.equal(fileInfo.readonly, (nodeFile.mode & 0o200) === 0);
    assert.ok(Math.abs(Date.parse(fileInfo.createdAt) - nodeFile.birthtimeMs) <= 1);
    assert.ok(Math.abs(Date.parse(fileInfo.modifiedAt) - nodeFile.mtimeMs) <= 1);
    const dirInfo = adapterFileInfo(path.join(root, "nested"), roots);
    assert.equal(dirInfo.type, "directory");
    assert.equal(dirInfo.sizeBytes, fs.statSync(path.join(root, "nested")).size);

    const linkFixtures = [
      ["outside-link", outside, "junction"],
      ["outside-dir-symlink", outside, "dir"],
      ["outside-file-symlink", path.join(outside, "secret.txt"), "file"],
      ["outside-relative-file-symlink", path.relative(root, path.join(outside, "secret.txt")), "file"],
      ["outside-unicode-dir-symlink", unicodeOutside, "dir"]
    ];
    for (const [name, target, type] of linkFixtures) {
      try { fs.symlinkSync(target, path.join(root, name), type); installedLinks.push(name); }
      catch (error) { if (name === "outside-link") throw error; }
    }
    assert.throws(() => adapterFileRead(path.join(root, "outside-link", "secret.txt"), roots), /reparse|disallowed|blocked/i);
    for (const name of installedLinks) {
      const publicInfo = adapterFileInfo(path.join(root, name), roots);
      const nodeInfo = fs.lstatSync(path.join(root, name));
      assert.equal(publicInfo.type, "symlink", name);
      assert.equal(publicInfo.symlink, true, name);
      assert.equal(publicInfo.sizeBytes, nodeInfo.size, name);
      assert.equal(publicInfo.readonly, (nodeInfo.mode & 0o200) === 0, name);
      assert.ok(Math.abs(Date.parse(publicInfo.createdAt) - nodeInfo.birthtimeMs) <= 1, name + " creation");
      assert.ok(Math.abs(Date.parse(publicInfo.modifiedAt) - nodeInfo.mtimeMs) <= 1, name + " modified");
      assert.ok(Math.abs(Date.parse(publicInfo.accessedAt) - nodeInfo.atimeMs) <= 1, name + " accessed");
    }
    assert.equal(fs.readFileSync(path.join(outside, "secret.txt"), "utf8"), "OUTSIDE-SECRET");

    const outsideParent = path.join(fixture, "outside-parent");
    fs.mkdirSync(outsideParent);
    fs.writeFileSync(path.join(outsideParent, "inside.txt"), "OUTSIDE-PARENT-CANARY");
    let parentSwapInstalled = false;
    const heldParentRead = adapterFileRead(path.join(root, "nested", "inside.txt"), roots, {
      afterParentOpened: () => {
        fs.renameSync(path.join(root, "nested"), path.join(root, "nested-held"));
        fs.symlinkSync(outsideParent, path.join(root, "nested"), "junction");
        parentSwapInstalled = true;
      }
    });
    assert.equal(heldParentRead, "NESTED-UTF8-سلام");
    assert.equal(fs.readFileSync(path.join(outsideParent, "inside.txt"), "utf8"), "OUTSIDE-PARENT-CANARY");
    if (parentSwapInstalled) {
      fs.rmdirSync(path.join(root, "nested"));
      fs.renameSync(path.join(root, "nested-held"), path.join(root, "nested"));
      parentSwapInstalled = false;
    }

    const outsideRoot = path.join(fixture, "outside-root");
    fs.mkdirSync(outsideRoot);
    fs.writeFileSync(path.join(outsideRoot, "root.txt"), "OUTSIDE-ROOT-CANARY");
    const movedRoot = path.join(fixture, "workspace-held");
    let rootSwapInstalled = false;
    const heldRootRead = adapterFileRead(path.join(root, "root.txt"), roots, {
      afterRootOpened: () => {
        fs.renameSync(root, movedRoot);
        fs.symlinkSync(outsideRoot, root, "junction");
        rootSwapInstalled = true;
      }
    });
    assert.equal(heldRootRead, "ROOT-LEVEL-UTF8-✓");
    assert.equal(fs.readFileSync(path.join(outsideRoot, "root.txt"), "utf8"), "OUTSIDE-ROOT-CANARY");
    if (rootSwapInstalled) {
      fs.rmdirSync(root);
      fs.renameSync(movedRoot, root);
      rootSwapInstalled = false;
    }

    assert.equal(addon.processHandleCount(), beforeHandles);
    process.stdout.write(JSON.stringify({
      ok: true,
      checks: [
        "configured-root selection rejects paths outside allowlist",
        "root-level and nested file reads match Node UTF-8 output",
        "invalid UTF-8 replacement behavior matches Node Buffer decoding",
        "256-KiB read cap fails closed",
        "directory reads are rejected",
        "get_file_info root/file/directory metadata shape and type mapping",
        "ordinary-file size, creation/modification timestamps, and readonly mapping match Node on fixture",
        "missing paths, invalid names, injected access-denied and close-failure categories are distinct; all acquired HANDLEs close",
        "reparse reads fail closed; junction/symlink get_file_info maps type, size, timestamps, and readonly to Node lstat without following targets",
        "root and parent rename/junction swaps remain bound to held HANDLEs; all handles return to baseline"
      ]
    }) + "\n");
  } finally {
    for (const name of installedLinks) { try { fs.rmdirSync(path.join(root, name)); } catch { try { fs.unlinkSync(path.join(root, name)); } catch {} } }
    fs.rmSync(fixture, { recursive: true, force: true });
  }
}
try { main(); } catch (error) { console.error(error); process.exitCode = 1; }
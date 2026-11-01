const assert = require("node:assert/strict");
const path = require("node:path");

function within(root, target) {
  const rel = path.relative(root, target);
  return rel === "" || (!rel.startsWith("..") && !path.isAbsolute(rel));
}
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
  if (typeof rootId !== "string" || !registry.has(rootId)) throw new Error("Unknown workspace rootId.");
  if (typeof relativePath !== "string" || relativePath.includes("\\0") ||
      path.isAbsolute(relativePath) || path.win32.isAbsolute(relativePath) ||
      /^[A-Za-z]:/.test(relativePath)) throw new Error("Workspace path must be relative to rootId.");
  const parts = relativePath.split(/[\\\\/]+/).filter(Boolean);
  if (parts.some(part => part === "." || part === "..")) throw new Error("Workspace relative path traversal is blocked.");
  const entry = registry.get(rootId);
  const absolute = path.resolve(entry.rootPath, ...parts);
  if (!within(entry.rootPath, absolute)) throw new Error("Workspace path escaped rootId.");
  return { rootId, rootPath: entry.rootPath, absolute, parts };
}

const temp = path.join(process.env.TEMP || process.env.TMP || ".", "raya-rootid-contract-fixture");
const roots = [path.join(temp, "one"), path.join(temp, "two")];
const registry = createRootRegistry(roots);
assert.equal(registry.size, 2);
assert.deepEqual(resolveRootRequest(registry, "root-1", "a\\b.txt").parts, ["a", "b.txt"]);
assert.equal(resolveRootRequest(registry, "root-2", "").rootPath, path.resolve(roots[1]));
assert.throws(() => resolveRootRequest(registry, "root-3", "x"), /Unknown workspace rootId/);
for (const p of ["..\\escape", "../escape", "C:\\\\Windows", "\\\\\\\\server\\\\share", roots[0], "\\0"]) {
  assert.throws(() => resolveRootRequest(registry, "root-1", p), /relative|traversal/i);
}
assert.throws(() => createRootRegistry([roots[0], path.join(roots[0], "nested")]), /overlap/i);
assert.throws(() => createRootRegistry([roots[0], roots[0]]), /overlap/i);
console.log(JSON.stringify({ok:true,checks:12,scope:"rootId-to-configured-roots contract model only"}));
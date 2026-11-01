const assert = require("node:assert/strict");
const path = require("node:path");
const fs = require("node:fs");
const os = require("node:os");
const addon = require(path.join(__dirname, "build", "Release", "napi_probe_raya_phase1_rootapi.node"));
const cases = [
  [0xC0000034, "not_found"],
  [0xC000003A, "not_found"],
  [0xC000000F, "not_found"],
  [0xC0000022, "access_denied"],
  [0xC0000035, "already_exists"],
  [0xC000000D, "invalid_name"],
  [0xC0000033, "invalid_name"],
  [0xC000003B, "invalid_name"],
  [0xC00000BA, "is_directory"],
  [0xC0000103, "not_directory"],
  [0xC000050B, "reparse_blocked"],
  [0x8000002D, "reparse_blocked"],
  [0xC0000043, "sharing_violation"],
  [0xC000007F, "storage_full"],
  [0xC000009A, "resource_exhausted"],
  [0xC0000185, "io_error"],
  [0, "io_error"]
];
const fixture = fs.mkdtempSync(path.join(os.tmpdir(), "raya-native-error-map-"));
const beforeHandles = addon.processHandleCount();
let rootHandle = null;
try {
  fs.writeFileSync(path.join(fixture, "already.txt"), "PRESERVE-COLLISION");
  rootHandle = addon.openWorkspaceRoot(fixture);
  let collision;
  try { addon.createChildExclusive(rootHandle, "already.txt"); } catch (error) { collision = error; }
  assert.ok(collision, "existing leaf must reject FILE_CREATE");
  assert.ok(String(collision.message || collision).startsWith("already_exists:"), String(collision.message || collision));
  assert.equal(fs.readFileSync(path.join(fixture, "already.txt"), "utf8"), "PRESERVE-COLLISION");
} finally {
  if (rootHandle) addon.closeParentHandle(rootHandle);
  assert.equal(addon.processHandleCount(), beforeHandles);
  fs.rmSync(fixture, {recursive:true,force:true});
}
for (const [code, expected] of cases) {
  const result = addon.classifyNtStatusForTest(code);
  assert.equal(result.code, code >>> 0);
  assert.equal(result.category, expected, "0x" + (code >>> 0).toString(16));
}
assert.throws(() => addon.classifyNtStatusForTest("0xC0000022"), /uint32/);
process.stdout.write(JSON.stringify({ok:true,checked:cases.length+1,checks:[
  "not-found status variants map to not_found",
  "access denied, collision, invalid name, directory mismatch and reparse block map distinctly",
  "sharing, storage and resource failures have distinct categories",
  "unknown/success status values fail to generic io_error",
  "invalid argument type is rejected",
  "real FILE_CREATE collision maps to already_exists and preserves existing bytes"
]})+"\n");
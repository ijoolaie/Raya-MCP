const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const assert = require("node:assert/strict");
const addon = require("./build/Release/napi_probe_raya_phase1_hardlink_guard_rootpin.node");

const fixture = fs.mkdtempSync(path.join(os.tmpdir(), "raya-root-pin-regression-"));
const root = path.join(fixture, "root");
const movedRoot = path.join(fixture, "moved-root");
const outside = path.join(fixture, "outside-root");
let rootCallbackSeen = false;
let renameBlocked = false;
let renameError = null;
let result;

try {
  fs.mkdirSync(path.join(root, "parent"), { recursive: true });
  fs.mkdirSync(outside, { recursive: true });
  fs.writeFileSync(path.join(root, "parent", "marker.txt"), "ORIGINAL-ROOT-CONTENT");
  fs.writeFileSync(path.join(outside, "marker.txt"), "JUNCTION-TARGET-CONTENT");

  result = addon.openRelative(root, "parent/marker.txt", 0, 1024, () => {}, (component) => {
    if (String(component).toLowerCase() !== "root" || rootCallbackSeen) return;
    rootCallbackSeen = true;
    try {
      fs.renameSync(root, movedRoot);
      fs.symlinkSync(outside, root, "junction");
    } catch (error) {
      renameBlocked = true;
      renameError = error.code || error.name;
    }
  });

  assert.equal(rootCallbackSeen, true, "root-chain synchronization point was not reached");
  assert.equal(renameBlocked, true, "configured root rename was not blocked while root-chain HANDLEs were held");
  assert.ok(["EPERM", "EBUSY", "EACCES"].includes(renameError), `unexpected rename error: ${renameError}`);
  assert.equal(result.opened, true, "openRelative did not complete after blocked namespace swap");
  assert.equal(result.content.toString(), "ORIGINAL-ROOT-CONTENT", "openRelative returned unexpected content");

  let rootIsJunction = false;
  try { rootIsJunction = fs.lstatSync(root).isSymbolicLink(); } catch {}
  assert.equal(rootIsJunction, false, "attacker-controlled junction replaced configured root");

  console.log(JSON.stringify({
    ok: true,
    rootCallbackSeen,
    rootRenameBlocked: renameBlocked,
    renameError,
    originalRootContentReturned: result.content.toString() === "ORIGINAL-ROOT-CONTENT",
    configuredRootReplacedByJunction: rootIsJunction
  }, null, 2));
} finally {
  for (const candidate of [root, movedRoot, outside, fixture]) {
    try { fs.rmSync(candidate, { recursive: true, force: true }); } catch {}
  }
}

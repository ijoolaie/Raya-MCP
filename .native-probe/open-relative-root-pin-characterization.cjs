const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const addon = require("./build/Release/napi_probe_raya_phase1_hardlink_guard.node");

const fixture = fs.mkdtempSync(path.join(os.tmpdir(), "raya-root-pin-"));
const root = path.join(fixture, "root");
const movedRoot = path.join(fixture, "moved-root");
const outside = path.join(fixture, "outside-root");
const events = [];
let swapped = false;
let swapError = null;
let result = null;
try {
  fs.mkdirSync(path.join(root, "parent"), { recursive: true });
  fs.mkdirSync(outside, { recursive: true });
  fs.writeFileSync(path.join(root, "parent", "marker.txt"), "ORIGINAL-ROOT-CONTENT");
  fs.writeFileSync(path.join(outside, "marker.txt"), "JUNCTION-TARGET-CONTENT");

  try {
    result = addon.openRelative(root, "parent/marker.txt", 0, 1024, () => {}, (component) => {
      events.push(String(component));
      if (String(component).toLowerCase() !== "root" || swapped || swapError) return;
      try {
        fs.renameSync(root, movedRoot);
        events.push("root-rename-succeeded");
        fs.symlinkSync(outside, root, "junction");
        events.push("junction-installed");
        swapped = true;
      } catch (e) {
        swapError = { code: e.code || e.name, message: e.message };
        throw e;
      }
    });
  } catch (e) {
    result = { error: e.message };
  }

  const controlRoot = path.join(fixture, "control-root");
  const movedControlRoot = path.join(fixture, "moved-control-root");
  fs.mkdirSync(path.join(controlRoot, "parent"), { recursive: true });
  const controlParent = addon.openParentRelative(controlRoot, "parent");
  let controlRenameBlocked = false;
  let controlRenameError = null;
  try { fs.renameSync(controlRoot, movedControlRoot); }
  catch (e) { controlRenameBlocked = true; controlRenameError = e.code || e.name; }
  addon.closeParentHandle(controlParent);

  let movedContent = null;
  try { movedContent = fs.readFileSync(path.join(movedRoot, "parent", "marker.txt"), "utf8"); }
  catch (e) { movedContent = "unavailable:" + (e.code || e.name); }
  let rootIsSymlink = false;
  try { rootIsSymlink = fs.lstatSync(root).isSymbolicLink(); } catch {}
  console.log(JSON.stringify({
    callbackComponents: events,
    swapSucceeded: swapped,
    swapError,
    openRelativeReturnedOriginalMovedRootContent: !!result && !result.error && result.content.toString() === "ORIGINAL-ROOT-CONTENT",
    openRelativeReturnedJunctionTargetContent: !!result && !result.error && result.content.toString() === "JUNCTION-TARGET-CONTENT",
    resultError: result && result.error ? result.error : null,
    configuredRootIsJunction: rootIsSymlink,
    openParentRelativeBlocksRootRename: controlRenameBlocked,
    openParentRelativeRenameError: controlRenameError,
    movedRootContent: movedContent
  }, null, 2));
} finally {
  try { fs.rmSync(root, { recursive: true, force: true }); } catch {}
  try { fs.rmSync(movedRoot, { recursive: true, force: true }); } catch {}
  try { fs.rmSync(outside, { recursive: true, force: true }); } catch {}
  try { fs.rmSync(controlRoot, { recursive: true, force: true }); } catch {}
  try { fs.rmSync(movedControlRoot, { recursive: true, force: true }); } catch {}
  try { fs.rmSync(fixture, { recursive: true, force: true }); } catch {}
}
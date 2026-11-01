"use strict";
const fs = require("node:fs");
const addon = require("./build/Release/napi_probe_raya_phase1_dircreate.node");
const root = process.argv[2];
let baseline = null, parent = null, opened = null, failure = null, result = null;
let closeFailuresBefore = null, closeFailuresAfter = null;
function waitForCommand() {
  const input = Buffer.alloc(128);
  let received = "";
  while (!received.includes("\n")) {
    const count = fs.readSync(0, input, 0, input.length, null);
    if (count === 0) throw new Error("parent closed synchronization pipe");
    received += input.subarray(0, count).toString("utf8");
  }
  return received.split(/\r?\n/, 1)[0];
}
try {
  process.stdout.write("WORKER_STARTED\n");
  baseline = addon.processHandleCount();
  parent = addon.openParentRelative(root, "parent");
  opened = addon.processHandleCount();
  process.stdout.write("PARENT_HANDLE_READY\n");
  const command = waitForCommand();
  if (command === "GO") {
    result = addon.createDirectoryChildExclusive(parent, "created-dir");
  } else if (command === "COLLIDE") {
    try {
      addon.createDirectoryChildExclusive(parent, "created-dir");
      throw new Error("exclusive directory creation unexpectedly succeeded on an existing leaf");
    } catch (error) {
      if (!/exclusive child directory creation blocked/i.test(error.message)) throw error;
      result = { collision: true, message: error.message };
    }
  } else if (command === "PATH") {
    result = addon.createDirectoryPathRelative(parent, "recursive-a\\recursive-b\\recursive-c");
  } else if (command === "FAIL-CLOSE") {
    closeFailuresBefore = addon.closeFailureCount();
    addon.failNextCloseForTest();
    try {
      addon.createDirectoryChildExclusive(parent, "created-dir");
      throw new Error("injected close failure was not surfaced");
    } catch (error) {
      if (!/CloseHandle failed after exclusive child directory creation/i.test(error.message)) throw error;
      result = { committedButReportedFailed: true, message: error.message };
    }
    closeFailuresAfter = addon.closeFailureCount();
    if (closeFailuresAfter !== closeFailuresBefore + 1) throw new Error("close-failure counter did not increment exactly once");
  } else {
    throw new Error("unexpected command: " + command);
  }
} catch (error) {
  failure = error.message;
} finally {
  if (parent) {
    try { addon.closeParentHandle(parent); }
    catch (error) { failure = (failure ? failure + "; " : "") + "parent close: " + error.message; }
  }
}
const afterClose = addon.processHandleCount();
console.log(JSON.stringify({ ok: !failure, message: failure, result, baseline, opened, afterClose, closeFailuresBefore, closeFailuresAfter }));
if (failure) process.exitCode = 1;

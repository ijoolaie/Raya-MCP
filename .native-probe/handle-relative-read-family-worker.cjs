"use strict";
const fs = require("node:fs");
const addon = require("./build/Release/napi_probe_raya_phase1_tail.node");
const root = process.argv[2];
let baseline = null, parent = null, opened = null, preOperation = null, afterOperation = null, failure = null, result = null;
function waitForCommand() {
  const input = Buffer.alloc(256);
  let received = "";
  while (!received.includes("\n")) {
    const count = fs.readSync(0, input, 0, input.length, null);
    if (count === 0) throw new Error("parent closed synchronization pipe");
    received += input.subarray(0, count).toString("utf8");
  }
  return received.split(/\r?\n/, 1)[0];
}
function readManyChildrenFromParent(handle, names, maxBytes) {
  return names.map(name => {
    try {
      const item = addon.readChildFromParent(handle, name, maxBytes);
      return { name, ok: true, content: item.content.toString("utf8"), size: item.size };
    } catch (error) {
      return { name, ok: false, error: error.message };
    }
  });
}
try {
  baseline = addon.processHandleCount();
  parent = addon.openParentRelative(root, "parent");
  opened = addon.processHandleCount();
  process.stdout.write("PARENT_HANDLE_READY\n");
  const command = waitForCommand();
  preOperation = addon.processHandleCount();
  if (command === "TAIL") {
    const value = addon.readTailChildFromParent(parent, "tail.txt", 5);
    result = { content: value.content.toString("utf8"), size: value.size, offset: value.offset, bytesRead: value.bytesRead };
  } else if (command === "TAIL-BLOCK") {
    try {
      addon.readTailChildFromParent(parent, "tail.txt", 5);
      throw new Error("tail read unexpectedly accepted leaf junction");
    } catch (error) {
      if (!/child open relative|reparse|disallowed/i.test(error.message)) throw error;
      result = { blocked: true, message: error.message };
    }
  } else if (command === "MULTI") {
    result = readManyChildrenFromParent(parent, ["one.txt", "missing.txt", "two.txt"], 128);
  } else if (command === "MULTI-JUNCTION") {
    result = readManyChildrenFromParent(parent, ["one.txt", "escape.txt", "two.txt"], 128);
  } else {
    throw new Error("unexpected command: " + command);
  }
} catch (error) { failure = error.message; }
finally {
  afterOperation = addon.processHandleCount();
  if (parent) {
    try { addon.closeParentHandle(parent); }
    catch (error) { failure = (failure ? failure + "; " : "") + error.message; }
  }
}
console.log(JSON.stringify({ ok: !failure, message: failure, result, baseline, opened, preOperation, afterOperation, afterClose: addon.processHandleCount() }));
if (failure) process.exitCode = 1;
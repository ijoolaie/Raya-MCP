"use strict";
const fs = require("node:fs");
const addon = require("./build/Release/napi_probe_rootchain_acceptance.node");
const root = process.argv[2];
let baseline = null;
let parent = null;
let opened = null;
let failure = null;
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
  if (command === "FAIL") throw new Error("injected post-open worker failure");
  if (command === "COLLIDE") {
    try {
      addon.createChildExclusive(parent, "race-created.txt");
      throw new Error("FILE_CREATE unexpectedly succeeded after the leaf appeared");
    } catch (error) {
      if (!/exclusive child creation blocked/i.test(error.message)) throw error;
      var result = { collision: true, message: error.message };
    }
  } else if (command === "READ") {
    const read = addon.readChildFromParent(parent, "race-created.txt", 64);
    result = { content: read.content.toString("utf8"), size: read.size };
  } else if (command === "READ-BLOCK") {
    try {
      addon.readChildFromParent(parent, "race-created.txt", 64);
      throw new Error("handle-relative read unexpectedly accepted a leaf junction");
    } catch (error) {
      if (!/reparse|relative to held parent|disallowed/i.test(error.message)) throw error;
      result = { blocked: true, message: error.message };
    }
  } else if (command === "GO") {
    const created = addon.createChildExclusive(parent, "race-created.txt");
    const identity = addon.inspectChildFromParent(parent, "race-created.txt");
    result = { created, identity };
  } else throw new Error("unexpected command: " + command);
} catch (error) {
  failure = error.message;
} finally {
  if (parent) {
    try { addon.closeParentHandle(parent); }
    catch (error) { failure = (failure ? failure + "; " : "") + "close: " + error.message; }
  }
}
const afterClose = addon.processHandleCount();
console.log(JSON.stringify({ ok: !failure, message: failure, result, baseline, opened, afterClose }));
if (failure) process.exitCode = 1;
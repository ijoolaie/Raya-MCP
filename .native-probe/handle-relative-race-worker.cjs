"use strict";
const fs = require("node:fs");
const addon = require("./build/Release/napi_probe_rootchain_acceptance.node");
const mode = process.argv[2];
const root = process.argv[3];
console.log("STARTING");
let synchronized = false;
function waitForSwap(signal) {
  if (process.env.RAYA_NATIVE_PROBE_SUPPRESS_SIGNAL !== signal) {
    fs.writeSync(1, signal + "\n");
  }
  const input = Buffer.alloc(64);
  let received = "";
  while (!received.includes("\n")) {
    const count = fs.readSync(0, input, 0, input.length, null);
    if (count === 0) throw new Error("parent closed synchronization pipe before swap completed");
    received += input.subarray(0, count).toString("utf8");
  }
  const expected = signal.startsWith("ROOT_COMPONENT_OPENED:") ? "ROOT_SWAP_DONE" : "SWAP_DONE";
  if (!received.startsWith(expected)) throw new Error("unexpected synchronization message");
}
try {
  if (mode === "root-chain") {
    let synchronized = false;
    try {
      const result = addon.openRelative(root, "secret.txt", 0, 64, () => {}, component => {
        fs.writeSync(1, "ROOT_COMPONENT:" + component + "\n");
        if (!synchronized && component.toLowerCase() === "root-race") {
          synchronized = true;
          waitForSwap("ROOT_COMPONENT_OPENED:root-race");
        }
      });
      console.log(JSON.stringify({ ok: false, unexpectedlyOpened: true, content: result.content.toString("utf8") }));
      process.exitCode = 2;
    } catch (error) {
      if (!synchronized) throw error;
      console.log(JSON.stringify({ ok: true, failedClosed: true, message: error.message }));
    }
  } else {
    const result = addon.openRelative(root, "safe\\inner\\secret.txt", 0, 64, () => {
      if (!synchronized) {
        synchronized = true;
        waitForSwap("PARENT_HANDLE_OPENED");
      }
    });
    console.log(JSON.stringify({ ok: true, result: { opened: result.opened, isDirectory: result.isDirectory, size: result.size, content: result.content.toString("utf8") } }));
  }
} catch (error) {
  console.error(JSON.stringify({ ok: false, message: error.message }));
  process.exitCode = 2;
}

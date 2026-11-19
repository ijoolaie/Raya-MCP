"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { execFileSync } = require("node:child_process");
const addon = require("./build/Release/napi_probe_raya_phase1_hardlink_guard.node");

function icacls(target, ...args) {
  return execFileSync("icacls.exe", [target, ...args], {
    encoding: "utf8", windowsHide: true, stdio: ["ignore", "pipe", "pipe"]
  });
}
function currentSid() {
  const out = execFileSync("whoami.exe", ["/user", "/fo", "csv", "/nh"], {
    encoding: "utf8", windowsHide: true
  });
  const match = out.match(/"(S-1-[0-9-]+)"/);
  if (!match) throw new Error("Could not parse current user SID: " + out);
  return match[1];
}
function capture(fn) {
  try {
    const value = fn();
    return { succeeded: true, error: null, value };
  } catch (error) {
    return {
      succeeded: false,
      error: String(error && error.message || error),
      accessDenied: /0xC0000022|STATUS_ACCESS_DENIED|access is denied/i.test(
        String(error && error.message || error)
      )
    };
  }
}

const fixture = fs.mkdtempSync(path.join(os.tmpdir(), "raya-ra-rd-matrix-"));
const sid = currentSid();
const baseline = addon.processHandleCount();
const scenarios = [
  { name: "deny-ra-only", rights: "RA" },
  { name: "deny-rd-only", rights: "RD" },
  { name: "deny-ra-and-rd", rights: "RA,RD" }
];
const results = [];

try {
  for (const scenario of scenarios) {
    const root = path.join(fixture, scenario.name, "root");
    const parentPath = path.join(root, "parent");
    const filePath = path.join(parentPath, "canary.txt");
    fs.mkdirSync(parentPath, { recursive: true });
    fs.writeFileSync(filePath, "RA-RD-CONTENT-CANARY", { flag: "wx" });

    let parent = null;
    let denyApplied = false;
    try {
      // Open and retain the parent before changing the child DACL.
      parent = addon.openParentRelative(root, "parent");
      const before = {
        inspect: capture(() => addon.inspectChildFromParent(parent, "canary.txt")),
        read: capture(() => addon.readChildFromParent(parent, "canary.txt", 1024))
      };

      icacls(filePath, "/deny", "*" + sid + ":(" + scenario.rights + ")");
      denyApplied = true;
      const acl = icacls(filePath);
      const during = {
        inspect: capture(() => addon.inspectChildFromParent(parent, "canary.txt")),
        read: capture(() => addon.readChildFromParent(parent, "canary.txt", 1024))
      };

      addon.closeParentHandle(parent);
      parent = null;
      icacls(filePath, "/remove:d", "*" + sid);
      denyApplied = false;

      parent = addon.openParentRelative(root, "parent");
      const after = {
        inspect: capture(() => addon.inspectChildFromParent(parent, "canary.txt")),
        read: capture(() => addon.readChildFromParent(parent, "canary.txt", 1024))
      };
      assert.equal(after.inspect.succeeded, true, scenario.name + ": metadata after ACL restore");
      assert.equal(after.read.succeeded, true, scenario.name + ": read after ACL restore");
      assert.equal(after.read.value.content.toString(), "RA-RD-CONTENT-CANARY");

      const denyAceLine = acl.split(/\r?\n/).find(line => /\(DENY\)/i.test(line)) || null;
      results.push({
        scenario: scenario.name,
        deniedRights: scenario.rights,
        denyAceLine,
        aclShowsDeny: !!denyAceLine && scenario.rights.split(",").every(
          right => new RegExp("\\b" + right + "\\b", "i").test(denyAceLine)
        ),
        before: {
          metadataSucceeded: before.inspect.succeeded,
          contentReadSucceeded: before.read.succeeded
        },
        whileDenied: {
          metadataSucceeded: during.inspect.succeeded,
          metadataError: during.inspect.error,
          contentReadSucceeded: during.read.succeeded,
          contentReadError: during.read.error,
          contentReadAccessDenied: during.read.accessDenied === true,
          returnedContent: during.read.succeeded ? during.read.value.content.toString() : null
        },
        afterRestore: {
          metadataSucceeded: after.inspect.succeeded,
          contentReadSucceeded: after.read.succeeded,
          contentMatchesCanary: after.read.value.content.toString() === "RA-RD-CONTENT-CANARY"
        }
      });
      addon.closeParentHandle(parent);
      parent = null;
      assert.equal(addon.processHandleCount(), baseline,
        scenario.name + ": HANDLE count must return to baseline");
    } finally {
      if (parent) { try { addon.closeParentHandle(parent); } catch {} }
      if (denyApplied) {
        try { icacls(filePath, "/remove:d", "*" + sid); } catch {}
      }
    }
  }

  assert.equal(addon.processHandleCount(), baseline, "all native HANDLEs must return to baseline");
  console.log(JSON.stringify({
    ok: true,
    userSid: sid,
    integrityNote: "Run this script from the same non-elevated Medium-integrity PowerShell session used for prior ACL tests.",
    results,
    handleCountBaseline: baseline,
    handleCountFinal: addon.processHandleCount(),
    handleCountReturnedToBaseline: true,
    scope: "three disposable temporary NTFS files; only explicit deny ACEs on those files; ACEs removed before cleanup; no workspace, user-profile, or global ACL changed"
  }, null, 2));
} finally {
  fs.rmSync(fixture, { recursive: true, force: true });
}
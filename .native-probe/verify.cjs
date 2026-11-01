"use strict";
const assert = require("node:assert/strict");
const addon = require("./build/Release/napi_probe.node");
const version = addon.napiVersion();
assert.equal(typeof version, "number");
assert.ok(version > 0, "N-API version must be positive");
console.log(JSON.stringify({ node: process.version, arch: process.arch, modulesAbi: process.versions.modules, napiVersion: version, addonLoaded: true }, null, 2));

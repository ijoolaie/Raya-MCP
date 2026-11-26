import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import fs from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const npmExecPath = process.env.npm_execpath;

function run(command, args, cwd, options = {}) {
  const executable = command === "npm" && npmExecPath ? process.execPath : command === "npm" && process.platform === "win32" ? "npm.cmd" : command;
  const executableArgs = command === "npm" && npmExecPath ? [npmExecPath, ...args] : args;
  const result = spawnSync(executable, executableArgs, {
    cwd,
    env: process.env,
    encoding: "utf8",
    windowsHide: true,
    shell: command === "npm" && !npmExecPath && process.platform === "win32",
    ...options
  });
  if (result.error) throw result.error;
  if (result.status !== 0) {
    if (result.stdout) process.stderr.write(result.stdout);
    if (result.stderr) process.stderr.write(result.stderr);
    throw new Error(`Command failed (${result.status}): ${command} ${args.join(" ")}`);
  }
  return result.stdout || "";
}

async function exists(target) {
  try { await fs.access(target); return true; } catch { return false; }
}

async function readJson(file) {
  return JSON.parse(await fs.readFile(path.join(root, file), "utf8"));
}

async function sourceFiles(dir) {
  const out = [];
  for (const entry of await fs.readdir(dir, { withFileTypes: true })) {
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) out.push(...await sourceFiles(full));
    else if (/\.(?:mjs|js|cjs)$/.test(entry.name)) out.push(full);
  }
  return out;
}

function assertDependency(manifest, name, expected) {
  const actual = manifest.dependencies?.[name];
  assert.equal(actual, expected, `${manifest.name} dependency ${name}`);
}

const manifests = {
  core: await readJson("packages/raya-mcp-core/package.json"),
  local: await readJson("packages/raya-local-mcp/package.json"),
  bridge: await readJson("packages/raya-bridge/package.json"),
  stack: await readJson("packages/raya-stack/package.json"),
  root: await readJson("package.json")
};

assert.equal(manifests.core.name, "@ijoolaie/mcp-core");
assert.equal(manifests.local.name, "@ijoolaie/local-mcp");
assert.equal(manifests.bridge.name, "@ijoolaie/bridge");
assert.equal(manifests.stack.name, "@ijoolaie/stack");
for (const [key, manifest] of Object.entries(manifests).filter(([key]) => key !== "root")) {
  assert.equal(manifest.version, "0.1.0", `${key} package version`);
}
assertDependency(manifests.local, "@ijoolaie/mcp-core", "0.1.0");
assertDependency(manifests.bridge, "@ijoolaie/mcp-core", "0.1.0");
assertDependency(manifests.stack, "@ijoolaie/mcp-core", "0.1.0");
assertDependency(manifests.stack, "@ijoolaie/local-mcp", "0.1.0");
assertDependency(manifests.stack, "@ijoolaie/bridge", "0.1.0");
for (const [key, manifest] of [["core", manifests.core], ["local", manifests.local], ["bridge", manifests.bridge]]) {
  for (const forbidden of ["@ijoolaie/local-mcp", "@ijoolaie/bridge", "@ijoolaie/stack"]) {
    if (key === "local" && forbidden === "@ijoolaie/local-mcp") continue;
    if (key === "bridge" && forbidden === "@ijoolaie/bridge") continue;
    assert.equal(manifest.dependencies?.[forbidden], undefined, `${key} must not depend on ${forbidden}`);
  }
}
for (const workspace of ["packages/raya-mcp-core", "packages/raya-local-mcp", "packages/raya-bridge", "packages/raya-stack"]) {
  assert.ok(manifests.root.workspaces?.includes(workspace), `missing workspace ${workspace}`);
}

const forbiddenImports = {
  "packages/raya-mcp-core/src": ["@ijoolaie/local-mcp", "@ijoolaie/bridge", "@ijoolaie/stack"],
  "packages/raya-local-mcp/src": ["@ijoolaie/bridge", "@ijoolaie/stack"],
  "packages/raya-bridge/src": ["@ijoolaie/local-mcp", "@ijoolaie/stack"]
};
for (const [relativeDir, forbidden] of Object.entries(forbiddenImports)) {
  for (const file of await sourceFiles(path.join(root, relativeDir))) {
    const content = await fs.readFile(file, "utf8");
    for (const specifier of forbidden) {
      assert.ok(!content.includes(`from "${specifier}"`) && !content.includes(`from '${specifier}'`) &&
        !content.includes(`import("${specifier}"`) && !content.includes(`import('${specifier}'`),
      `forbidden package import ${specifier} in ${path.relative(root, file)}`);
    }
  }
}

const tempRoot = await fs.mkdtemp(path.join(os.tmpdir(), "raya-modular-acceptance-"));
try {
  const packs = path.join(tempRoot, "packs");
  await fs.mkdir(packs);
  const packageDirs = ["raya-mcp-core", "raya-local-mcp", "raya-bridge", "raya-stack"];
  for (const name of packageDirs) {
    run("npm", ["pack", `./packages/${name}`, "--pack-destination", packs, "--ignore-scripts", "--json"], root, { stdio: "ignore" });
  }
  const tarballNames = { "raya-mcp-core": "ijoolaie-mcp-core", "raya-local-mcp": "ijoolaie-local-mcp", "raya-bridge": "ijoolaie-bridge", "raya-stack": "ijoolaie-stack" };
  const tarball = name => path.join(packs, `${tarballNames[name]}-0.1.0.tgz`);
  for (const name of packageDirs) assert.ok(await exists(tarball(name)), `missing packed tarball ${name}`);

  const profiles = [
    { name: "core-only", packages: ["raya-mcp-core"], expected: ["mcp-core"] },
    { name: "local-mcp-only", packages: ["raya-mcp-core", "raya-local-mcp"], expected: ["mcp-core", "local-mcp"] },
    { name: "bridge-only", packages: ["raya-mcp-core", "raya-bridge"], expected: ["mcp-core", "bridge"] },
    { name: "full-stack", packages: packageDirs, expected: ["mcp-core", "local-mcp", "bridge", "stack"] }
  ];

  for (const profile of profiles) {
    const consumer = path.join(tempRoot, profile.name);
    await fs.mkdir(consumer);
    run("npm", ["install", "--prefix", consumer, ...profile.packages.map(tarball), "--ignore-scripts", "--no-audit", "--no-fund"], root, { stdio: "ignore" });
    for (const packageName of ["mcp-core", "local-mcp", "bridge", "stack"]) {
      const present = await exists(path.join(consumer, "node_modules", "@ijoolaie", packageName));
      assert.equal(present, profile.expected.includes(packageName), `${profile.name} package presence: ${packageName}`);
    }
    run("npm", ["ls", "--prefix", consumer, "--depth=0"], root, { stdio: "ignore" });
    process.stdout.write(`PROFILE_OK ${profile.name}\n`);

    if (profile.name === "local-mcp-only") {
      run(process.execPath, [path.join(root, "local-mcp-stdio-consumer-smoke.mjs")], consumer);
    }
    if (profile.name === "bridge-only") {
      const code = `import assert from "node:assert/strict"; import path from "node:path"; import {createBridgeConnector} from "@ijoolaie/bridge/connector"; const b=createBridgeConnector(); const fixture=path.resolve("node_modules/@ijoolaie/mcp-core/examples/minimal-stdio-server.mjs"); try { await b.connectStdio({id:"verify",command:process.execPath,args:[fixture],cwd:path.dirname(fixture)}); const {tools}=await b.listTools("verify"); assert.equal(tools[0].name,"ping"); const result=await b.callTool("verify","ping"); assert.equal(result.content?.[0]?.text,"pong"); } finally { await b.closeAll(); }`;
      run(process.execPath, ["--input-type=module", "-e", code], consumer);
      run(process.execPath, [path.join(root, "bridge-consumer-smoke.mjs")], consumer);
    }
    if (profile.name === "full-stack") {
      const code = `import assert from "node:assert/strict"; import {getStackProfile} from "@ijoolaie/stack"; assert.deepEqual(getStackProfile("full-stack").install,["@ijoolaie/stack"]);`;
      run(process.execPath, ["--input-type=module", "-e", code], consumer);
    }
  }
  run(process.execPath, [path.join(root, "bridge-http-integration-smoke.mjs")], root);
} finally {
  await fs.rm(tempRoot, { recursive: true, force: true });
}

process.stdout.write("RAYA_MODULAR_PACKAGE_ACCEPTANCE_OK\n");

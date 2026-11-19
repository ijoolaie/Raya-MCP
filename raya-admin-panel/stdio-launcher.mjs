import fs from "node:fs/promises";
import path from "node:path";
import { spawn } from "node:child_process";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, "..");
const configPath = path.join(here, "panel-config.json");
const config = JSON.parse(await fs.readFile(configPath, "utf8"));
if (config.version !== 1 || !Array.isArray(config.allowedRoots) || config.allowedRoots.length < 1 || config.allowedRoots.length > 32) {
  console.error("Raya panel config is invalid.");
  process.exit(20);
}
const locked = path.resolve(root).toLowerCase();
const systemRoots = [process.env.WINDIR || "C:\\Windows", process.env.ProgramFiles || "C:\\Program Files", process.env["ProgramFiles(x86)"] || "C:\\Program Files (x86)", process.env.ProgramData || "C:\\ProgramData"].map(p => path.resolve(p).toLowerCase());
const roots = [];
for (const input of config.allowedRoots) {
  if (typeof input !== "string" || !path.win32.isAbsolute(input) || input.startsWith("\\\\") || input.includes(";")) {
    console.error("Invalid local workspace root in panel-config.json.");
    process.exit(21);
  }
  const absolute = path.resolve(input);
  if (absolute.toLowerCase() === path.parse(absolute).root.toLowerCase()) {
    console.error("Drive-root workspace access is blocked.");
    process.exit(21);
  }
  const st = await fs.lstat(absolute);
  if (!st.isDirectory() || st.isSymbolicLink()) {
    console.error("Workspace root must be a real directory (junctions/symlinks are blocked): " + absolute);
    process.exit(22);
  }
  const real = await fs.realpath(absolute);
  if (real.toLowerCase() !== absolute.toLowerCase()) {
    console.error("Workspace root does not match its canonical path: " + absolute);
    process.exit(23);
  }
  for (const systemRoot of systemRoots) {
    const rel = path.relative(systemRoot, real).toLowerCase();
    if (rel === "" || (!rel.startsWith("..") && !path.isAbsolute(rel))) {
      console.error("Windows system and Program Files roots are blocked.");
      process.exit(24);
    }
  }
  roots.push(real);
}
if (!roots.some(p => p.toLowerCase() === locked)) {
  console.error("The local-mcp-bridge root is mandatory.");
  process.exit(24);
}
const keys = roots.map(p => p.replace(/\\/g, "/").replace(/\/$/, "").toLowerCase());
for (let i = 0; i < keys.length; i++) for (let j = i + 1; j < keys.length; j++) {
  if (keys[i] === keys[j] || keys[i].startsWith(keys[j] + "/") || keys[j].startsWith(keys[i] + "/")) {
    console.error("Duplicate or nested workspace roots are not allowed.");
    process.exit(25);
  }
}
const child = spawn(process.execPath, [path.join(root, "server.mjs")], {
  cwd: root,
  env: {
    ...process.env,
    MCP_ALLOWED_ROOTS: roots.join(";"),
    MCP_ENABLE_POWERSHELL: config.powershellEnabled === true ? "true" : "false"
  },
  stdio: "inherit",
  windowsHide: true,
  shell: false
});
child.once("error", e => { console.error("Could not start Raya MCP:", e.message); process.exit(26); });
child.once("close", code => process.exit(code ?? 1));
for (const signal of ["SIGINT", "SIGTERM"]) process.once(signal, () => child.kill(signal));

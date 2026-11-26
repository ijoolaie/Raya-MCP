import fs from "node:fs/promises";
import fsSync from "node:fs";
import path from "node:path";
import os from "node:os";
import { spawn } from "node:child_process";
import { createServer } from "node:http";
import { randomUUID, timingSafeEqual } from "node:crypto";
import { createCoreServer, createStdioTransport, createStreamableHttpTransport } from "@ijoolaie/mcp-core";
import { z } from "zod";
import { registerAdvancedTools } from "./advanced-tools.mjs";
import { registerFormatTools } from "./format-tools.mjs";
import { registerUrlTools } from "./url-tools.mjs";

const HOME = process.env.USERPROFILE || os.homedir();
const DEFAULT_ROOTS = [
  path.join(HOME, "AI-Employee"),
  path.join(HOME, "IranPriceScout"),
  path.join(HOME, "Nova-Audit-pr2-clean-check"),
  path.join(HOME, "imoxy-net-review-worktree"),
  path.join(HOME, "local-mcp-bridge"),
  path.join(HOME, "IranPriceScout-integration")
];
const ROOTS = (process.env.MCP_ALLOWED_ROOTS
  ? process.env.MCP_ALLOWED_ROOTS.split(";").filter(Boolean)
  : DEFAULT_ROOTS).map(p => path.resolve(p));
const MAX_READ_BYTES = 256 * 1024;
const MAX_WRITE_BYTES = 256 * 1024;
const MAX_COMMAND_OUTPUT = 64 * 1024;
const UPLOAD_TTL_MS = 60 * 60 * 1000;
const activeUploads = new Map();

async function expireUploads() {
  const now = Date.now();
  for (const [id, upload] of activeUploads) {
    if (!upload.busy && upload.expiresAt <= now) {
      activeUploads.delete(id);
      await fs.unlink(upload.temp).catch(() => {});
    }
  }
}

function insideRoot(candidate) {
  const absolute = path.resolve(candidate);
  return ROOTS.some(root => {
    const rel = path.relative(root, absolute);
    return rel === "" || (!rel.startsWith("..") && !path.isAbsolute(rel));
  });
}
function checkedPath(input) {
  const absolute = path.resolve(input);
  const root = ROOTS.find(candidate => {
    const rel = path.relative(path.resolve(candidate), absolute);
    return rel === "" || (!rel.startsWith("..") && !path.isAbsolute(rel));
  });
  if (!root) throw new Error("Path is outside the configured workspace allowlist.");
  const realRoot = fsSync.realpathSync(root);
  let probe = absolute;
  const suffix = [];
  while (true) {
    try { fsSync.lstatSync(probe); break; }
    catch (error) {
      if (error.code !== "ENOENT") throw error;
      const parent = path.dirname(probe);
      if (parent === probe) throw error;
      suffix.unshift(path.basename(probe));
      probe = parent;
    }
  }
  const realProbe = fsSync.realpathSync(probe);
  const relative = path.relative(realRoot, realProbe);
  if (!(relative === "" || (!relative.startsWith("..") && !path.isAbsolute(relative)))) {
    throw new Error("Path resolves outside the configured workspace allowlist.");
  }
  let target = realProbe;
  for (const part of suffix) target = path.join(target, part);
  const finalRelative = path.relative(realRoot, target);
  if (!(finalRelative === "" || (!finalRelative.startsWith("..") && !path.isAbsolute(finalRelative)))) {
    throw new Error("Path resolves outside the configured workspace allowlist.");
  }
  return target;
}
function safeChildEnv(overrides = {}) {
  const allowed = ["PATH","SystemRoot","WINDIR","TEMP","TMP","USERPROFILE","APPDATA","LOCALAPPDATA","ProgramFiles","ProgramFiles(x86)","ComSpec","PROCESSOR_ARCHITECTURE","PATHEXT"];
  const out = {};
  for (const key of allowed) if (process.env[key] !== undefined) out[key] = process.env[key];
  for (const [key, value] of Object.entries(overrides)) {
    if (!/^[A-Za-z_][A-Za-z0-9_]*$/.test(key)) throw new Error("Invalid environment variable name.");
    if (/TOKEN|SECRET|PASSWORD|API_KEY|CREDENTIAL|AUTH/i.test(key)) throw new Error("Setting secret-like environment variables is blocked.");
    out[key] = value;
  }
  return out;
}
async function waitForChildClose(child, timeoutMs) {
  if (child.closed || child.exitCode !== null || child.signalCode !== null) return true;
  return new Promise(resolve => {
    let settled = false;
    const finish = value => {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      child.off("close", onClose);
      resolve(value);
    };
    const onClose = () => finish(true);
    const timer = setTimeout(() => finish(false), timeoutMs);
    child.once("close", onClose);
  });
}

function runTaskkill(pid, timeoutMs) {
  return new Promise((resolve, reject) => {
    const killer = spawn("taskkill.exe", ["/pid", String(pid), "/t", "/f"], {
      windowsHide: true, shell: false, stdio: "ignore", env: safeChildEnv()
    });
    let settled = false;
    const finish = value => {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      killer.off("error", onError);
      killer.off("close", onClose);
      resolve(value);
    };
    const onError = error => {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      killer.off("close", onClose);
      reject(error);
    };
    const onClose = code => finish({ exitCode: code, timedOut: false });
    const timer = setTimeout(() => {
      try { killer.kill(); } catch {}
      finish({ exitCode: null, timedOut: true });
    }, timeoutMs);
    killer.once("error", onError);
    killer.once("close", onClose);
  });
}

async function terminateProcessTree(child, { waitMs = 5000 } = {}) {
  if (!child) return { terminationRequested: false, exitConfirmed: true };
  if (child.closed || child.exitCode !== null || child.signalCode !== null) {
    return { terminationRequested: false, exitConfirmed: true, exitCode: child.exitCode, signal: child.signalCode };
  }
  const closePromise = waitForChildClose(child, waitMs);
  let helper = null;
  let terminationError = null;
  try {
    if (process.platform === "win32") helper = await runTaskkill(child.pid, Math.min(3000, waitMs));
    else child.kill("SIGKILL");
  } catch (error) {
    terminationError = String(error?.message || error).slice(0, 240);
  }
  const exitConfirmed = await closePromise;
  return {
    terminationRequested: true,
    exitConfirmed,
    exitCode: child.exitCode,
    signal: child.signalCode,
    helperExitCode: helper?.exitCode ?? null,
    helperTimedOut: helper?.timedOut ?? false,
    terminationError
  };
}

function textResult(value) {
  return { content: [{ type: "text", text: typeof value === "string" ? value : JSON.stringify(value, null, 2) }] };
}

function errorCategory(error) {
  const message = String(error?.message || "");
  if (/outside the configured workspace allowlist|outside the workspace allowlist/i.test(message)) return "path_not_allowed";
  if (/larger than 256 KiB|maximum|too large/i.test(message)) return "size_limit";
  if (/not a file|already exists/i.test(message)) return "file_state";
  if (/ENOENT|not found/i.test(message)) return "not_found";
  if (/EACCES|EPERM|permission/i.test(message)) return "permission";
  if (/timed out|timeout/i.test(message)) return "timeout";
  if (/spawn .* ENOENT/i.test(message)) return "runtime_unavailable";
  if (/disabled/i.test(message)) return "feature_disabled";
  return "other";
}
function registerAuditedTool(server, name, description, schema, handler) {
  server.tool(name, description, schema, async (args) => {
    const started = Date.now();
    console.error(JSON.stringify({ event: "mcp_tool_start", tool: name, at: new Date().toISOString() }));
    try {
      const result = await handler(args);
      console.error(JSON.stringify({
        event: "mcp_tool_end", tool: name, ok: true, durationMs: Date.now() - started
      }));
      return result;
    } catch (error) {
      console.error(JSON.stringify({
        event: "mcp_tool_end", tool: name, ok: false,
        durationMs: Date.now() - started, errorCategory: errorCategory(error)
      }));
      throw error;
    }
  });
}
function createMcpServer() {
  return createCoreServer({
    name: "local-mcp-bridge",
    version: "0.1.0",
    registerCapabilities(server) {
  registerAuditedTool(server, "workspace_roots", "List the workspace directories this server is allowed to access.", {}, async () =>
    textResult(ROOTS.map(root => ({ path: root })))
  );
  registerAuditedTool(server, "workspace_list", "List files and folders inside an allowed workspace path.", {
    path: z.string().optional().describe("Absolute path inside an allowed workspace; omit to list configured roots.")
  }, async ({ path: input }) => {
    if (!input) return textResult(await Promise.all(ROOTS.map(async root => {
      try { return { path: root, entries: (await fs.readdir(root, { withFileTypes: true })).map(e => ({ name: e.name, type: e.isDirectory() ? "directory" : "file" })) }; }
      catch (e) { return { path: root, error: e.message }; }
    })));
    const target = checkedPath(input);
    const entries = await fs.readdir(target, { withFileTypes: true });
    return textResult(entries.map(e => ({ name: e.name, type: e.isDirectory() ? "directory" : "file" })));
  });
  registerAuditedTool(server, "file_read", "Read a UTF-8 text file within an allowed workspace (maximum 256 KiB).", {
    path: z.string()
  }, async ({ path: input }) => {
    const target = checkedPath(input);
    const stat = await fs.stat(target);
    if (!stat.isFile()) throw new Error("Path is not a file.");
    if (stat.size > MAX_READ_BYTES) throw new Error("File is larger than 256 KiB; choose a smaller file.");
    return textResult(await fs.readFile(target, "utf8"));
  });
  registerAuditedTool(server, "file_read_chunk", "Read a UTF-8 file in bounded byte chunks. Use offsetBytes from the previous response until complete.", {
    path: z.string(), offsetBytes: z.number().int().min(0).optional().default(0),
    maxBytes: z.number().int().min(1).max(MAX_READ_BYTES).optional().default(MAX_READ_BYTES)
  }, async ({ path: input, offsetBytes, maxBytes }) => {
    const target = checkedPath(input), stat = await fs.stat(target);
    if (!stat.isFile()) throw new Error("Path is not a file.");
    if (offsetBytes > stat.size) throw new Error("offsetBytes exceeds file size.");
    const length = Math.min(maxBytes, stat.size - offsetBytes);
    const handle = await fs.open(target, "r");
    try {
      const buffer = Buffer.alloc(length);
      const { bytesRead } = await handle.read(buffer, 0, length, offsetBytes);
      return textResult({ path: target, offsetBytes, nextOffsetBytes: offsetBytes + bytesRead,
        totalBytes: stat.size, done: offsetBytes + bytesRead >= stat.size,
        contentBase64: buffer.subarray(0, bytesRead).toString("base64") });
    } finally { await handle.close(); }
  });
  registerAuditedTool(server, "file_write", "Write a UTF-8 text file inside an allowed workspace. Existing files are protected unless overwrite=true is explicitly supplied.", {
    path: z.string(),
    content: z.string().max(MAX_WRITE_BYTES),
    overwrite: z.boolean().optional().default(false)
  }, async ({ path: input, content, overwrite }) => {
    if (Buffer.byteLength(content, "utf8") > MAX_WRITE_BYTES) throw new Error("UTF-8 content is larger than 256 KiB.");
    const target = checkedPath(input);
    if (!insideRoot(path.dirname(target))) throw new Error("Parent directory is outside the workspace allowlist.");
    try {
      await fs.access(target);
      if (!overwrite) throw new Error("File already exists. Set overwrite=true only after reviewing the target.");
    } catch (e) {
      if (e.code !== "ENOENT") throw e;
    }
    await fs.mkdir(path.dirname(target), { recursive: true });
    await fs.writeFile(target, content, { encoding: "utf8", flag: overwrite ? "w" : "wx" });
    return textResult({ written: target, bytes: Buffer.byteLength(content), overwritten: overwrite });
  });
  registerAuditedTool(server, "file_write_chunk", "Upload a new UTF-8 file in ordered chunks; the target appears only after the final chunk is received.", {
    path: z.string(), uploadId: z.string().uuid(), content: z.string().max(200000),
    offsetBytes: z.number().int().min(0), totalBytes: z.number().int().min(0).max(32 * 1024 * 1024)
  }, async ({ path: input, uploadId, content, offsetBytes, totalBytes }) => {
    await expireUploads();
    const target = checkedPath(input);
    if (!insideRoot(path.dirname(target))) throw new Error("Parent directory is outside the workspace allowlist.");
    const chunk = Buffer.from(content, "utf8");
    if (chunk.length > 200 * 1024 || offsetBytes + chunk.length > totalBytes) throw new Error("Chunk size or range is invalid.");
    const temp = target + ".raya-upload-" + uploadId;
    let upload = activeUploads.get(uploadId);
    if (!upload) {
      if (offsetBytes !== 0) throw new Error("Unknown or expired upload; restart from offset zero.");
      upload = { target, temp, totalBytes, nextOffset: 0, expiresAt: Date.now() + UPLOAD_TTL_MS, busy: true };
      activeUploads.set(uploadId, upload);
    } else {
      if (upload.busy) throw new Error("An operation for this upload is already in progress.");
      if (upload.target !== target || upload.temp !== temp || upload.totalBytes !== totalBytes) {
        throw new Error("Upload ID is already bound to a different target or total size.");
      }
      if (offsetBytes !== upload.nextOffset) throw new Error("Upload offset mismatch; expected " + upload.nextOffset + ".");
      upload.busy = true;
      upload.expiresAt = Date.now() + UPLOAD_TTL_MS;
    }
    let handle;
    let published = false;
    let ownsTemp = false;
    try {
      await fs.mkdir(path.dirname(target), { recursive: true });
      if (offsetBytes === 0) handle = await fs.open(temp, "wx");
      else handle = await fs.open(temp, "r+");
      ownsTemp = true;
      const st = await handle.stat();
      if (st.size !== offsetBytes) throw new Error("Temporary upload size mismatch; expected " + offsetBytes + ".");
      let written = 0;
      while (written < chunk.length) {
        const result = await handle.write(chunk, written, chunk.length - written, offsetBytes + written);
        if (!result.bytesWritten) throw new Error("Upload write made no progress.");
        written += result.bytesWritten;
      }
      await handle.sync();
      const finalStat = await handle.stat();
      await handle.close(); handle = null;
      upload.nextOffset = finalStat.size;
      upload.expiresAt = Date.now() + UPLOAD_TTL_MS;
      if (finalStat.size !== totalBytes) {
        upload.busy = false;
        return textResult({ uploadId, receivedBytes: finalStat.size, totalBytes, complete: false, nextOffsetBytes: finalStat.size });
      }
      // link() publishes the fully written same-directory file without replacing an existing target.
      await fs.link(temp, target);
      published = true;
      await fs.unlink(temp).catch(() => {});
      activeUploads.delete(uploadId);
      return textResult({ written: target, bytes: totalBytes, complete: true });
    } catch (e) {
      if (handle) await handle.close().catch(() => {});
      activeUploads.delete(uploadId);
      if (!published && ownsTemp) await fs.unlink(temp).catch(() => {});
      throw e;
    }
  });
  const presets = {
    git_status: { exe: "git", args: ["status", "--short", "--branch"] },
    git_diff_check: { exe: "git", args: ["diff", "--check"] },
    git_recent_log: { exe: "git", args: ["log", "-5", "--oneline", "--decorate"] },
    npm_test: { exe: "npm", args: ["test"] },
    npm_lint: { exe: "npm", args: ["run", "lint"] },
    pytest: { exe: "python", args: ["-m", "pytest", "-q"] },
    docker_ps: { exe: "docker", args: ["ps", "--format", "table {{.Names}}\\t{{.Status}}\\t{{.Ports}}"] },
    docker_compose_ps: { exe: "docker", args: ["compose", "ps"] }
  };
  registerAuditedTool(server, "run_check", "Run one predefined development check in an allowed workspace. This tool does not accept arbitrary shell commands.", {
    check: z.enum(["git_status", "git_diff_check", "git_recent_log", "npm_test", "npm_lint", "pytest", "docker_ps", "docker_compose_ps"]),
    cwd: z.string().optional().describe("Working directory inside an allowed workspace; defaults to the first root.")
  }, async ({ check, cwd }) => {
    const workdir = checkedPath(cwd || ROOTS[0]);
    const preset = presets[check];
    const result = await new Promise((resolve, reject) => {
      // Windows does not spawn .cmd shims directly. Route npm presets through cmd.exe,
      // which resolves npm.cmd from PATH; keep all other predefined commands shell-free.
      const isNpmCheck = check === "npm_test" || check === "npm_lint";
      const command = process.platform === "win32" && isNpmCheck
        ? {
            exe: process.env.ComSpec || path.join(process.env.SystemRoot || "C:\\Windows", "System32", "cmd.exe"),
            args: ["/d", "/s", "/c", `npm.cmd run ${check === "npm_test" ? "test" : "lint"}`]
          }
        : preset;
      const child = spawn(command.exe, command.args, { cwd: workdir, windowsHide: true, shell: false, env: safeChildEnv() });
      let stdout = "", stderr = "", settled = false;
      const finish = value => { if (settled) return; settled = true; clearTimeout(timer); resolve(value); };
      child.stdout.on("data", b => { stdout = (stdout + b.toString()).slice(-MAX_COMMAND_OUTPUT); });
      child.stderr.on("data", b => { stderr = (stderr + b.toString()).slice(-MAX_COMMAND_OUTPUT); });
      child.on("error", error => { if (settled) return; settled = true; clearTimeout(timer); reject(error); });
      child.on("close", code => finish({ check, cwd: workdir, exitCode: code, timedOut: false, terminationRequested: false, exitConfirmed: true, stdout, stderr }));
      const timer = setTimeout(async () => {
        if (settled) return;
        const termination = await terminateProcessTree(child, { waitMs: 5000 });
        finish({ check, cwd: workdir, exitCode: child.exitCode, timedOut: true, ...termination, stdout, stderr });
      }, 120000);
    });
    return textResult(result);
  });
  registerAuditedTool(server, "powershell_execute", "Execute a PowerShell command on this Windows host. Requires MCP_ENABLE_POWERSHELL=true. Commands run with the server user's permissions; this is not a sandbox.", {
    command: z.string().min(1).max(16384).describe("PowerShell command or script, maximum 16 KiB."),
    cwd: z.string().optional().describe("Working directory inside a configured workspace root."),
    timeoutMs: z.number().int().min(1000).max(120000).optional().default(30000)
  }, async ({ command, cwd, timeoutMs }) => {
    if (process.platform !== "win32") throw new Error("powershell_execute is available only on Windows.");
    if (process.env.MCP_ENABLE_POWERSHELL !== "true") {
      throw new Error("PowerShell execution is disabled. Set MCP_ENABLE_POWERSHELL=true in the MCP server environment after reviewing the security implications.");
    }
    const workdir = checkedPath(cwd || ROOTS[0]);
    const result = await new Promise((resolve, reject) => {
      const child = spawn("powershell.exe", [
        "-NoLogo", "-NoProfile", "-NonInteractive", "-Command", command
      ], {
        cwd: workdir,
        windowsHide: true,
        shell: false,
        env: safeChildEnv()
      });
      let stdout = "", stderr = "", settled = false;
      const finish = value => {
        if (settled) return;
        settled = true;
        clearTimeout(timer);
        resolve(value);
      };
      child.stdout.on("data", b => { stdout = (stdout + b.toString()).slice(-MAX_COMMAND_OUTPUT); });
      child.stderr.on("data", b => { stderr = (stderr + b.toString()).slice(-MAX_COMMAND_OUTPUT); });
      child.on("error", error => {
        if (settled) return;
        settled = true;
        clearTimeout(timer);
        reject(error);
      });
      child.on("close", code => finish({
        cwd: workdir, exitCode: code, timedOut: false, terminationRequested: false, exitConfirmed: true, stdout, stderr
      }));
      const timer = setTimeout(async () => {
        if (settled) return;
        const termination = await terminateProcessTree(child, { waitMs: 5000 });
        finish({ cwd: workdir, exitCode: child.exitCode, timedOut: true, ...termination, stdout, stderr });
      }, timeoutMs);
    });
    return textResult(result);
  });
  registerAdvancedTools(server, { ROOTS });
  registerFormatTools(server, { ROOTS });
  registerUrlTools(server);
    }
  });
}

const httpMode = process.argv.includes("--http");
if (!httpMode) {
  const server = createMcpServer();
  await server.connect(createStdioTransport());
} else {
  const host = process.env.MCP_HOST || "127.0.0.1";
  const port = Number(process.env.MCP_PORT || "8765");
  const token = process.env.MCP_BEARER_TOKEN;
  if (!token || token.length < 32) {
    console.error("HTTP mode requires MCP_BEARER_TOKEN with at least 32 characters.");
    process.exit(2);
  }
  const tokenBuf = Buffer.from(token);
  const mcp = createMcpServer();
  const transport = createStreamableHttpTransport({ sessionIdGenerator: () => randomUUID() });
  await mcp.connect(transport);
  const httpServer = createServer(async (req, res) => {
    const auth = req.headers.authorization || "";
    const supplied = Buffer.from(auth.startsWith("Bearer ") ? auth.slice(7) : "");
    const authorized = supplied.length === tokenBuf.length && timingSafeEqual(supplied, tokenBuf);
    if (!authorized) {
      res.writeHead(401, { "content-type": "application/json" });
      res.end(JSON.stringify({ error: "Unauthorized" }));
      return;
    }
    const origin = req.headers.origin;
    if (origin && !(/^https:\/\/([a-z0-9-]+\.)*chatgpt\.com$/i.test(origin) ||
      /^https:\/\/([a-z0-9-]+\.)*openai\.com$/i.test(origin) ||
      /^http:\/\/(localhost|127\.0\.0\.1)(:\d+)?$/i.test(origin))) {
      res.writeHead(403); res.end("Origin not allowed"); return;
    }
    if (req.url !== "/mcp") {
      res.writeHead(404); res.end("Not found"); return;
    }
    try { await transport.handleRequest(req, res); }
    catch (error) {
      console.error("MCP HTTP request failed:", error);
      if (!res.headersSent) res.writeHead(500);
      if (!res.writableEnded) res.end("Internal server error");
    }
  });
  httpServer.listen(port, host, () => console.error("Local MCP HTTP endpoint listening at http://" + host + ":" + port + "/mcp"));
  let shutdownPromise = null;
  const closeHttpServerBounded = () => new Promise((resolve, reject) => {
    let settled = false, forceTimer;
    const finish = error => {
      if (settled) return;
      settled = true;
      clearTimeout(graceTimer);
      clearTimeout(forceTimer);
      error ? reject(error) : resolve();
    };
    const graceTimer = setTimeout(() => {
      httpServer.closeAllConnections?.();
      forceTimer = setTimeout(() => finish(new Error("HTTP listener did not close after forced connection cleanup.")), 1500);
    }, 7000);
    httpServer.close(error => {
      if (error && error.code !== "ERR_SERVER_NOT_RUNNING") finish(error);
      else finish();
    });
  });
  const withDeadline = (promise, timeoutMs, label) => new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error(label + " exceeded its shutdown deadline.")), timeoutMs);
    Promise.resolve(promise).then(
      value => { clearTimeout(timer); resolve(value); },
      error => { clearTimeout(timer); reject(error); }
    );
  });
  const shutdown = () => {
    if (shutdownPromise) return shutdownPromise;
    shutdownPromise = (async () => {
      let failed = false;
      const hardDeadline = setTimeout(() => {
        console.error("MCP HTTP shutdown exceeded its hard deadline; forcing process exit.");
        process.exit(1);
      }, 15000);
      try { await closeHttpServerBounded(); }
      catch (error) { failed = true; console.error("MCP HTTP listener shutdown:", error.message); }
      try { await withDeadline(transport.close(), 2000, "MCP transport close"); }
      catch (error) { failed = true; console.error("MCP transport shutdown:", error.message); }
      try { await withDeadline(mcp.close(), 2000, "MCP server close"); }
      catch (error) { failed = true; console.error("MCP server shutdown:", error.message); }
      if (!failed) clearTimeout(hardDeadline);
      process.exitCode = failed ? 1 : 0;
    })();
    return shutdownPromise;
  };
  process.once("SIGINT", shutdown);
  process.once("SIGTERM", shutdown);
}
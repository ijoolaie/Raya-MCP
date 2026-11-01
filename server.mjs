import fs from "node:fs/promises";
import fsSync from "node:fs";
import path from "node:path";
import os from "node:os";
import { spawn } from "node:child_process";
import { createServer } from "node:http";
import { timingSafeEqual } from "node:crypto";
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { StreamableHTTPServerTransport } from "@modelcontextprotocol/sdk/server/streamableHttp.js";
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
  const server = new McpServer({ name: "local-mcp-bridge", version: "0.1.0" });
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
  registerAuditedTool(server, "file_write", "Write a UTF-8 text file inside an allowed workspace. Existing files are protected unless overwrite=true is explicitly supplied.", {
    path: z.string(),
    content: z.string().max(MAX_WRITE_BYTES),
    overwrite: z.boolean().optional().default(false)
  }, async ({ path: input, content, overwrite }) => {
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
      const child = spawn(command.exe, command.args, { cwd: workdir, windowsHide: true, shell: false, env: process.env });
      let stdout = "", stderr = "";
      child.stdout.on("data", b => { stdout = (stdout + b.toString()).slice(-MAX_COMMAND_OUTPUT); });
      child.stderr.on("data", b => { stderr = (stderr + b.toString()).slice(-MAX_COMMAND_OUTPUT); });
      child.on("error", reject);
      child.on("close", code => resolve({ check, cwd: workdir, exitCode: code, stdout, stderr }));
      const timer = setTimeout(() => { child.kill(); resolve({ check, cwd: workdir, exitCode: null, timedOut: true, stdout, stderr }); }, 120000);
      child.on("close", () => clearTimeout(timer));
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
        env: process.env
      });
      let stdout = "", stderr = "", settled = false;
      const finish = (value) => {
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
        cwd: workdir, exitCode: code, timedOut: false, stdout, stderr
      }));
      const timer = setTimeout(() => {
        if (settled) return;
        child.kill();
        finish({ cwd: workdir, exitCode: null, timedOut: true, stdout, stderr });
      }, timeoutMs);
    });
    return textResult(result);
  });
  registerAdvancedTools(server, { ROOTS });
  registerFormatTools(server, { ROOTS });
  registerUrlTools(server);
  return server;
}

const httpMode = process.argv.includes("--http");
if (!httpMode) {
  const server = createMcpServer();
  await server.connect(new StdioServerTransport());
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
  const transport = new StreamableHTTPServerTransport({ sessionIdGenerator: undefined });
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
  const shutdown = async () => {
    httpServer.close();
    await transport.close();
    await mcp.close();
    process.exit(0);
  };
  process.on("SIGINT", shutdown);
  process.on("SIGTERM", shutdown);
}





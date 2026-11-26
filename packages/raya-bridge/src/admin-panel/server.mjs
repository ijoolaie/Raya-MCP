import http from "node:http";
import fs from "node:fs/promises";
import fsSync from "node:fs";
import path from "node:path";
import os from "node:os";
import { spawn } from "node:child_process";
import { randomBytes, timingSafeEqual } from "node:crypto";
import { fileURLToPath } from "node:url";
import { createBridgeConnector } from "../connector/index.mjs";
import { parseBridgeEndpointConfig, resolveBridgeEndpointConfig, publicBridgeEndpoint } from "../connector/config.mjs";


const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(process.env.RAYA_WORKSPACE_ROOT || process.cwd());
const CONFIG_FILE = path.resolve(process.env.RAYA_PANEL_CONFIG_FILE || path.join(ROOT, "raya-admin-panel", "panel-config.json"));
const RAYA_CONFIG = path.resolve(process.env.RAYA_CONFIG_FILE || path.join(ROOT, "raya-config.json"));
const TUNNEL_EXE = path.resolve(process.env.RAYA_TUNNEL_EXE || path.join(ROOT, "downloads", "tunnel-client-v0.0.17", "tunnel-client.exe"));
const TUNNEL_PROFILE = process.env.RAYA_TUNNEL_PROFILE || "raya-local";
const HEALTH_URL = process.env.RAYA_TUNNEL_HEALTH_URL || "http://127.0.0.1:8080/healthz";
const READYZ_URL = process.env.RAYA_TUNNEL_READYZ_URL || "http://127.0.0.1:8080/readyz";
const MCP_HEALTH_URL = process.env.RAYA_TUNNEL_MCP_HEALTH_URL || "http://127.0.0.1:8080/health/mcp";
const MCP_ENDPOINTS_FILE = path.resolve(process.env.RAYA_MCP_ENDPOINTS_FILE || path.join(ROOT, "raya-admin-panel", "mcp-endpoints.json"));

const PORT = Number(process.env.RAYA_PANEL_PORT || 8766);
const HOST = "127.0.0.1";
const PANEL_TOKEN_FILE = path.join(process.env.LOCALAPPDATA || os.homedir(), "RayaMCP", "panel-token.txt");
const TOKEN = process.env.RAYA_PANEL_TOKEN || (fsSync.existsSync(PANEL_TOKEN_FILE) ? fsSync.readFileSync(PANEL_TOKEN_FILE, "utf8").trim() : randomBytes(32).toString("hex"));
const MAX_BODY = 16 * 1024;
const LOCKED_ROOT = path.resolve(ROOT).replace(/\\/g, "/").replace(/\/$/, "").toLowerCase();
const SYSTEM_ROOTS = [
  process.env.WINDIR || "C:\\Windows",
  process.env.ProgramFiles || "C:\\Program Files",
  process.env["ProgramFiles(x86)"] || "C:\\Program Files (x86)",
  process.env.ProgramData || "C:\\ProgramData"
].map(p => path.resolve(p).toLowerCase());

let tunnelChild = null;
let tunnelState = "stopped";
let tunnelStartedAt = null;
let tunnelExit = null;
let logLines = [];
let shuttingDown = false;
let lifecycleQueue = Promise.resolve();
let endpointRegistryPromise = null;

async function getEndpointRegistry() {
  if (!endpointRegistryPromise) {
    endpointRegistryPromise = (async () => {
      let raw;
      try { raw = JSON.parse(await fs.readFile(MCP_ENDPOINTS_FILE, "utf8")); }
      catch (error) {
        if (error.code === "ENOENT") raw = { version: 1, endpoints: [] };
        else throw error;
      }
      return { endpoints: parseBridgeEndpointConfig(raw), connector: createBridgeConnector() };
    })();
    endpointRegistryPromise.catch(() => { endpointRegistryPromise = null; });
  }
  return endpointRegistryPromise;
}

function findConfiguredEndpoint(registry, id) {
  if (typeof id !== "string" || !id.trim()) throw new TypeError("MCP endpoint id is required.");
  const endpoint = registry.endpoints.find(item => item.id === id);
  if (!endpoint) throw new Error("Unknown configured MCP endpoint id.");
  return endpoint;
}

async function listMcpEndpoints() {
  const registry = await getEndpointRegistry();
  return registry.endpoints.map(endpoint => publicBridgeEndpoint(endpoint, registry.connector.listSessions()));
}

async function connectMcpEndpoint(id) {
  const registry = await getEndpointRegistry();
  const endpoint = findConfiguredEndpoint(registry, id);
  const result = await registry.connector.connectEndpoint(resolveBridgeEndpointConfig(endpoint));
  return { ...publicBridgeEndpoint(endpoint, registry.connector.listSessions()), serverInfo: result.serverInfo };
}

async function disconnectMcpEndpoint(id) {
  const registry = await getEndpointRegistry();
  findConfiguredEndpoint(registry, id);
  return registry.connector.disconnect(id);
}

async function listMcpTools(id) {
  const registry = await getEndpointRegistry();
  findConfiguredEndpoint(registry, id);
  return registry.connector.listTools(id);
}

async function callMcpTool(id, name, args) {
  const registry = await getEndpointRegistry();
  findConfiguredEndpoint(registry, id);
  return registry.connector.callTool(id, name, args);
}


function addLog(message) {
  const line = new Date().toISOString() + " " + String(message).replace(/[\r\n]+/g, " ").slice(0, 500);
  logLines.push(line);
  if (logLines.length > 250) logLines = logLines.slice(-250);
}
function safeEqual(a, b) {
  const x = Buffer.from(String(a || ""));
  const y = Buffer.from(String(b || ""));
  return x.length === y.length && timingSafeEqual(x, y);
}
function isLoopback(address) {
  return address === "127.0.0.1" || address === "::1" || address === "::ffff:127.0.0.1";
}
function isInside(parent, child) {
  const rel = path.relative(parent, child);
  return rel === "" || (!rel.startsWith("..") && !path.isAbsolute(rel));
}
function normalizeComparable(p) { return path.resolve(p).replace(/\\/g, "/").replace(/\/$/, "").toLowerCase(); }

async function readConfig() {
  const raw = JSON.parse(await fs.readFile(CONFIG_FILE, "utf8"));
  if (raw.version !== 1 || !Array.isArray(raw.allowedRoots)) throw new Error("panel-config.json schema is invalid.");
  const settings = JSON.parse(await fs.readFile(RAYA_CONFIG, "utf8"));
  return {
    version: 1,
    allowedRoots: raw.allowedRoots,
    powershellEnabled: raw.powershellEnabled === true,
    settings: {
      maxSessions: settings.maxSessions ?? 32,
      maxSearches: settings.maxSearches ?? 8,
      searchMaxResults: settings.searchMaxResults ?? 500,
      searchMaxDepth: settings.searchMaxDepth ?? 8
    }
  };
}
async function validateRoot(input) {
  if (typeof input !== "string" || !input.trim() || input.length > 320) throw new Error("مسیر معتبر نیست.");
  if (!path.win32.isAbsolute(input) || input.startsWith("\\\\") || input.includes(";") || /[\r\n\0]/.test(input)) {
    throw new Error("فقط مسیر مطلق محلی ویندوز مجاز است؛ مسیر شبکه و جداکننده ; پذیرفته نیست.");
  }
  const absolute = path.resolve(input);
  const parsed = path.parse(absolute);
  if (normalizeComparable(absolute) === normalizeComparable(parsed.root)) throw new Error("اجازه دسترسی به ریشه درایو داده نمی‌شود.");
  const lst = await fs.lstat(absolute);
  if (lst.isSymbolicLink() || !lst.isDirectory()) throw new Error("مسیر باید پوشه واقعی باشد و Junction/Symlink نباشد.");
  const real = await fs.realpath(absolute);
  if (normalizeComparable(real) !== normalizeComparable(absolute)) throw new Error("مسیر به مقصد دیگری resolve می‌شود؛ مسیر مستقیم را انتخاب کن.");
  if (SYSTEM_ROOTS.some(s => isInside(s, normalizeComparable(real)))) throw new Error("افزودن پوشه‌های سیستمی ویندوز یا Program Files مجاز نیست.");
  return real;
}
async function validateRootSet(roots) {
  if (!Array.isArray(roots) || roots.length < 1 || roots.length > 32) throw new Error("تعداد مسیرها باید بین ۱ تا ۳۲ باشد.");
  const checked = [];
  for (const root of roots) checked.push(await validateRoot(root));
  const keys = checked.map(normalizeComparable);
  if (new Set(keys).size !== keys.length) throw new Error("مسیر تکراری وجود دارد.");
  for (let i = 0; i < keys.length; i++) {
    for (let j = i + 1; j < keys.length; j++) {
      if (isInside(keys[i], keys[j]) || isInside(keys[j], keys[i])) throw new Error("مسیرهای تو در تو مجاز نیستند؛ مسیر والد یا فرزند را جداگانه اضافه نکن.");
    }
  }
  if (!keys.includes(LOCKED_ROOT)) throw new Error("Configured workspace root must remain in the allowed-root list.");
  return checked;
}
function validateSettings(s) {
  const ranges = {
    maxSessions: [1, 64],
    maxSearches: [1, 8],
    searchMaxResults: [1, 2000],
    searchMaxDepth: [0, 12]
  };
  const out = {};
  for (const [key, [min, max]] of Object.entries(ranges)) {
    const n = Number(s?.[key]);
    if (!Number.isInteger(n) || n < min || n > max) throw new Error("مقدار نامعتبر برای " + key + "؛ بازه مجاز " + min + " تا " + max + " است.");
    out[key] = n;
  }
  return out;
}
async function atomicJson(file, value) {
  const temp = file + ".tmp-" + randomBytes(6).toString("hex");
  await fs.writeFile(temp, JSON.stringify(value, null, 2) + "\n", { flag: "wx" });
  try { await fs.rename(temp, file); }
  catch (e) { await fs.rm(temp, { force: true }).catch(() => {}); throw e; }
}
async function removeRoot(input) {
  const targetPath = input?.root;
  if (typeof targetPath !== "string" || !targetPath.trim() || targetPath.length > 320) {
    throw new Error("مسیر درخواستی برای حذف معتبر نیست.");
  }
  const target = normalizeComparable(targetPath);
  if (target === LOCKED_ROOT) throw new Error("مسیر اصلی Raya قابل حذف نیست.");
  const raw = JSON.parse(await fs.readFile(CONFIG_FILE, "utf8"));
  if (raw.version !== 1 || !Array.isArray(raw.allowedRoots)) throw new Error("panel-config.json schema is invalid.");
  const index = raw.allowedRoots.findIndex(root => typeof root === "string" && normalizeComparable(root) === target);
  if (index < 0) throw new Error("این مسیر در فهرست مجاز وجود ندارد یا قبلاً حذف شده است.");
  if (raw.allowedRoots.length <= 1) throw new Error("حداقل یک مسیر مجاز باید باقی بماند.");
  const removedRoot = raw.allowedRoots[index];
  const allowedRoots = raw.allowedRoots.filter((_, i) => i !== index);
  await atomicJson(CONFIG_FILE, {
    version: 1,
    allowedRoots,
    powershellEnabled: raw.powershellEnabled === true
  });
  addLog("مسیر از فهرست مجاز حذف شد: " + removedRoot);
  return { allowedRoots, removedRoot, restartRequired: true };
}
async function saveConfig(input) {
  const roots = await validateRootSet(input.allowedRoots);
  const settings = validateSettings(input.settings);
  const powershellEnabled = input.powershellEnabled === true;
  await atomicJson(CONFIG_FILE, { version: 1, allowedRoots: roots, powershellEnabled });
  await atomicJson(RAYA_CONFIG, settings);
  addLog("تنظیمات ذخیره شد؛ برای اعمال روی MCP باید تونل راه‌اندازی مجدد شود.");
  return { allowedRoots: roots, powershellEnabled, settings, restartRequired: true };
}
async function probe(url) {
  try {
    const response = await fetch(url, { signal: AbortSignal.timeout(1200) });
    const body = await response.text();
    return { ok: response.ok, status: response.status, body: body.slice(0, 3000) };
  } catch (e) { return { ok: false, error: String(e.message || e).slice(0, 200) }; }
}
async function status() {
  const cfg = await readConfig();
  const rootIssues = [];
  for (const root of cfg.allowedRoots) {
    try { await validateRoot(root); }
    catch (e) { rootIssues.push({ path: root, error: e.message }); }
  }
  const healthz = await probe(HEALTH_URL);
  const readyz = await probe(READYZ_URL);
  const mcp = await probe(MCP_HEALTH_URL);
  let mcpJson = null;
  try { if (mcp.body) mcpJson = JSON.parse(mcp.body); } catch {}
  return {
    panel: { pid: process.pid, host: HOST, port: PORT },
    tunnel: { state: tunnelState, pid: tunnelChild?.pid || null, startedAt: tunnelStartedAt, exit: tunnelExit },
    apiKeyAvailable: Boolean(process.env.CONTROL_PLANE_API_KEY),
    health: { healthz, readyz, mcp: mcpJson ? { ok: mcp.ok, status: mcp.status, state: mcpJson.state, childState: mcpJson.details?.child_state, generation: mcpJson.details?.child_generation, initializeEpoch: mcpJson.details?.initialize_epoch, toolsList: mcpJson.details?.tools_list } : mcp },
    config: cfg,
    rootIssues,
    logs: logLines.slice(-30)
  };
}
function serializeLifecycle(operation) {
  const current = lifecycleQueue.then(operation, operation);
  lifecycleQueue = current.catch(() => {});
  return current;
}

function waitForClose(child, ms) {
  if (child.closed) return Promise.resolve({ code: child.exitCode, signal: child.signalCode });
  return new Promise(resolve => {
    let done = false;
    let timer;
    const onClose = (code, signal) => finish({ code, signal });
    const finish = value => { if (done) return; done = true; clearTimeout(timer); child.off("close", onClose); resolve(value); };
    child.once("close", onClose);
    timer = setTimeout(() => finish(null), ms);
  });
}
async function startTunnel() {
  if (tunnelChild) {
    const reason = tunnelState === "running" ? "already_running" : "lifecycle_state_" + tunnelState;
    return { started: false, reason, pid: tunnelChild.pid };
  }
  if (!process.env.CONTROL_PLANE_API_KEY) throw new Error("کلید CONTROL_PLANE_API_KEY در محیط این پنل موجود نیست. پنل باید از همان PowerShellای اجرا شود که قبلاً تونل Raya را از آن اجرا می‌کردی.");
  await validateRootSet((await readConfig()).allowedRoots);
  await fs.access(TUNNEL_EXE);
  const existing = await probe("http://127.0.0.1:8080/healthz");
  if (existing.ok) throw new Error("یک تونل از قبل روی پورت 8080 فعال است و تحت مدیریت این پنل نیست. ابتدا آن را از پنجره PowerShell قبلی با Ctrl+C متوقف کن.");
  tunnelExit = null;
  tunnelState = "starting";
  const child = spawn(TUNNEL_EXE, ["run", "--profile", TUNNEL_PROFILE], {
    cwd: ROOT, env: { ...process.env }, windowsHide: true, stdio: ["ignore", "pipe", "pipe"], shell: false
  });
  tunnelChild = child;
  tunnelStartedAt = new Date().toISOString();
  child.stdout.on("data", b => addLog("tunnel stdout: " + b.toString("utf8").slice(0, 350)));
  child.stderr.on("data", b => addLog("tunnel: " + b.toString("utf8").slice(0, 350)));
  child.once("error", e => { tunnelState = "failed"; tunnelExit = { error: e.message }; addLog("خطای اجرای تونل: " + e.message); });
  child.once("close", (code, signal) => {
    tunnelExit = { code, signal, at: new Date().toISOString() };
    tunnelState = code === 0 ? "stopped" : "failed";
    addLog("تونل متوقف شد؛ exit=" + code + " signal=" + signal);
    if (tunnelChild === child) tunnelChild = null;
  });
  await new Promise(r => setTimeout(r, 1800));
  if (tunnelState === "failed" || child.exitCode !== null) throw new Error("تونل اجرا نشد. گزارش پنل را بررسی کن.");
  tunnelState = "running";
  addLog("تونل raya-local با PID " + child.pid + " شروع شد.");
  return { started: true, pid: child.pid };
}
async function stopTunnel() {
  if (!tunnelChild || !tunnelChild.pid) return { stopped: false, reason: "not_managed_by_panel" };
  const child = tunnelChild;
  tunnelState = "stopping";
  try {
  if (process.platform === "win32") {
    await new Promise((resolve, reject) => {
      const killer = spawn("taskkill.exe", ["/PID", String(child.pid), "/T", "/F"], { windowsHide: true, stdio: "ignore", shell: false });
      killer.once("error", reject);
      killer.once("close", code => code === 0 || child.closed || child.exitCode !== null || child.signalCode !== null ? resolve() : reject(new Error("توقف پردازش تونل با taskkill ناموفق بود.")));
    });
    } else child.kill("SIGTERM");
  } catch (error) {
    if (tunnelChild === child && !child.closed) tunnelState = "stop_failed";
    addLog("درخواست توقف تونل ناموفق بود: " + error.message);
    throw error;
  }
  const closed = await waitForClose(child, 8000);
  if (!closed) {
    if (tunnelChild === child) tunnelState = "stop_unconfirmed";
    addLog("توقف تونل در مهلت مقرر تأیید نشد؛ ارجاع پردازش حفظ شد.");
    return { stopped: false, reason: "close_timeout", closeConfirmed: false, pid: child.pid };
  }
  if (tunnelChild === child) tunnelChild = null;
  tunnelState = "stopped";
  addLog("توقف تونل تحت مدیریت پنل با رویداد close تأیید شد.");
  return { stopped: true, closeConfirmed: true, pid: child.pid, exitCode: closed.code, signal: closed.signal };
}
async function readBody(req) {
  const chunks = []; let size = 0;
  for await (const chunk of req) {
    size += chunk.length;
    if (size > MAX_BODY) throw new Error("درخواست بیش از حد بزرگ است.");
    chunks.push(chunk);
  }
  return JSON.parse(Buffer.concat(chunks).toString("utf8") || "{}");
}
function send(res, code, value) {
  const body = JSON.stringify(value);
  res.writeHead(code, { "content-type": "application/json; charset=utf-8", "cache-control": "no-store", "x-content-type-options": "nosniff" });
  res.end(body);
}
function authorized(req) {
  const auth = req.headers.authorization || "";
  return auth.startsWith("Bearer ") && safeEqual(auth.slice(7), TOKEN);
}

const app = http.createServer(async (req, res) => {
  if (!isLoopback(req.socket.remoteAddress)) return send(res, 403, { error: "loopback_only" });
  const host = String(req.headers.host || "");
  if (!new RegExp("^(127\\.0\\.0\\.1|localhost):" + PORT + "$", "i").test(host)) return send(res, 403, { error: "invalid_host" });
  if (req.method === "GET" && req.url?.split("?")[0] === "/") {
    const requested = new URL(req.url, "http://" + host);
    const cookieToken = String(req.headers.cookie || "").split(";").map(v => v.trim()).find(v => v.startsWith("raya_panel="))?.slice("raya_panel=".length) || "";
    const queryToken = requested.searchParams.get("token") || "";
    if (!safeEqual(queryToken, TOKEN) && !safeEqual(cookieToken, TOKEN)) return send(res, 401, { error: "panel_link_required" });
    const headers = { "content-type": "text/html; charset=utf-8", "cache-control": "no-store", "content-security-policy": "default-src 'self'; style-src 'self' 'unsafe-inline'; script-src 'self' 'unsafe-inline'; connect-src 'self'; img-src 'self' data:; base-uri 'none'; frame-ancestors 'none'", "x-content-type-options": "nosniff", "referrer-policy": "no-referrer" };
    if (safeEqual(queryToken, TOKEN)) headers["set-cookie"] = "raya_panel=" + TOKEN + "; Path=/; HttpOnly; SameSite=Strict; Max-Age=86400";
    res.writeHead(200, headers);
    const html = await fs.readFile(path.join(HERE, "index.html"), "utf8");
    res.end(html.replace("__PANEL_TOKEN__", TOKEN));
    return;
  }
  if (!req.url?.startsWith("/api/")) { res.writeHead(404); res.end("Not found"); return; }
  if (!authorized(req)) return send(res, 401, { error: "unauthorized" });
  const origin = req.headers.origin;
  if (origin && !new RegExp("^http://(127\\.0\\.0\\.1|localhost):" + PORT + "$", "i").test(origin)) return send(res, 403, { error: "origin_not_allowed" });
  try {
    if (req.method === "GET" && req.url === "/api/status") return send(res, 200, await status());
    if (req.method === "GET" && req.url === "/api/logs") return send(res, 200, { logs: logLines.slice(-100) });
    if (req.method === "GET" && req.url === "/api/mcp/endpoints") return send(res, 200, { endpoints: await listMcpEndpoints() });
    if (req.method === "POST" && req.url === "/api/mcp/connect") { const input = await readBody(req); return send(res, 200, await connectMcpEndpoint(input.id)); }
    if (req.method === "POST" && req.url === "/api/mcp/disconnect") { const input = await readBody(req); return send(res, 200, await disconnectMcpEndpoint(input.id)); }
    if (req.method === "POST" && req.url === "/api/mcp/tools") { const input = await readBody(req); return send(res, 200, await listMcpTools(input.id)); }
    if (req.method === "POST" && req.url === "/api/mcp/call") { const input = await readBody(req); return send(res, 200, await callMcpTool(input.id, input.name, input.arguments || {})); }
    if (req.method === "POST" && req.url === "/api/logs/clear") { const cleared = logLines.length; logLines = []; addLog("گزارش‌ها توسط کاربر پاک شدند."); return send(res, 200, { cleared }); }
    if (req.method === "POST" && req.url === "/api/config/remove-root") return send(res, 200, await removeRoot(await readBody(req)));    if (req.method === "POST" && req.url === "/api/config/save") return send(res, 200, await saveConfig(await readBody(req)));
    if (req.method === "POST" && req.url === "/api/server/start") return send(res, 200, await serializeLifecycle(startTunnel));
    if (req.method === "POST" && req.url === "/api/server/stop") return send(res, 200, await serializeLifecycle(stopTunnel));
    if (req.method === "POST" && req.url === "/api/server/restart") {
      const outcome = await serializeLifecycle(async () => {
      if (tunnelChild) {
        const stopped = await stopTunnel();
        if (!stopped.stopped) return { restarted: false, reason: "stop_unconfirmed", ...stopped };
      }
      const started = await startTunnel();
      return { restarted: started.started, ...started };
      });
      return send(res, outcome.restarted === false && outcome.reason === "stop_unconfirmed" ? 409 : 200, outcome);
    }
    return send(res, 404, { error: "not_found" });
  } catch (e) { addLog("خطا: " + e.message); return send(res, 400, { error: e.message }); }
});
app.on("error", e => { addLog("خطای پنل: " + e.message); });
app.listen(PORT, HOST, () => {
  const url = "http://" + HOST + ":" + PORT + "/?token=" + TOKEN;
  console.log("\nRaya MCP Admin Panel is ready.");
  console.log("Open this local address in your browser:");
  console.log(url);
  if (process.platform === "win32" && process.env.RAYA_PANEL_NO_BROWSER !== "true") {
    const opener = spawn("cmd.exe", ["/c", "start", "", url], {
      windowsHide: true, detached: true, stdio: "ignore", shell: false
    });
    opener.unref();
  }
  console.log("This one-time panel token stays in memory and is never written to disk.");
  console.log("Keep this PowerShell window open while managing the tunnel. Press Ctrl+C to close the panel.");
  addLog("پنل مدیریتی روی " + HOST + ":" + PORT + " آماده است.");
});
async function shutdown() {
  if (shuttingDown) return;
  shuttingDown = true;
  // Closing the dashboard must not stop the independently running Raya tunnel.
  try { if (endpointRegistryPromise) await (await endpointRegistryPromise).connector.closeAll(); }
  catch (error) { addLog("خطا در بستن اتصال‌های MCP: " + error.message); }
  app.close(() => process.exit(0));
}
process.once("SIGINT", shutdown);
process.once("SIGTERM", shutdown);
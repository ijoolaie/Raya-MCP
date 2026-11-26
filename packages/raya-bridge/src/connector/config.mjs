const ID = /^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$/;
const ENV = /^[A-Za-z_][A-Za-z0-9_]*$/;
const HEADER = /^[!#$%&'*+.^_`|~0-9A-Za-z-]+$/;
const FORBIDDEN_HEADERS = new Set(["connection", "content-length", "host", "transfer-encoding"]);

function object(value, label) {
  if (!value || typeof value !== "object" || Array.isArray(value)) throw new TypeError(label + " must be an object.");
  return value;
}

function envMap(value, label) {
  if (value === undefined) return {};
  object(value, label);
  if (Object.keys(value).length > 32) throw new TypeError(label + " supports at most 32 mappings.");
  for (const [target, source] of Object.entries(value)) {
    if (!ENV.test(target) || typeof source !== "string" || !ENV.test(source)) {
      throw new TypeError(label + " must map valid target names to environment-variable names.");
    }
  }
  return { ...value };
}

function headerEnvMap(value) {
  if (value === undefined) return {};
  object(value, "HTTP header environment mapping");
  if (Object.keys(value).length > 32) throw new TypeError("HTTP header environment mapping supports at most 32 entries.");
  const result = {};
  for (const [header, source] of Object.entries(value)) {
    if (!HEADER.test(header) || FORBIDDEN_HEADERS.has(header.toLowerCase()) || typeof source !== "string" || !ENV.test(source)) {
      throw new TypeError("Invalid or forbidden HTTP header environment mapping.");
    }
    result[header] = source;
  }
  return result;
}

function validateEndpoint(endpoint) {
  object(endpoint, "MCP endpoint");
  if (!ID.test(endpoint.id || "")) throw new TypeError("Endpoint id must be 1-64 safe characters.");
  if (typeof endpoint.name !== "string" || !endpoint.name.trim() || endpoint.name.length > 120) throw new TypeError("Endpoint name is required (max 120 characters).");
  if (endpoint.transport === "stdio") {
    if (typeof endpoint.command !== "string" || !endpoint.command.trim() || endpoint.command.length > 260) throw new TypeError("STDIO command is required.");
    if (endpoint.args !== undefined && (!Array.isArray(endpoint.args) || endpoint.args.length > 64 || endpoint.args.some(value => typeof value !== "string" || value.length > 4096))) throw new TypeError("STDIO args must be at most 64 strings.");
    if (endpoint.cwd !== undefined && (typeof endpoint.cwd !== "string" || !endpoint.cwd.trim() || endpoint.cwd.length > 1024)) throw new TypeError("STDIO cwd is invalid.");
    return {
      id: endpoint.id, name: endpoint.name, transport: "stdio",
      command: endpoint.command, args: [...(endpoint.args || [])], cwd: endpoint.cwd,
      env: envMap(endpoint.env, "STDIO environment")
    };
  }
  if (endpoint.transport === "streamable-http") {
    let url;
    try { url = new URL(endpoint.url); } catch { throw new TypeError("Streamable HTTP URL is invalid."); }
    if (url.username || url.password || url.hash) throw new TypeError("MCP URL credentials and fragments are not allowed.");
    const host = url.hostname.toLowerCase().replace(/^\[|\]$/g, "");
    const loopback = host === "localhost" || host === "127.0.0.1" || host === "::1";
    if (url.protocol !== "https:" && !(url.protocol === "http:" && loopback)) throw new TypeError("Remote MCP endpoints must use HTTPS; HTTP is allowed only for loopback.");
    const headersEnv = headerEnvMap(endpoint.headersEnv);
    return { id: endpoint.id, name: endpoint.name, transport: "streamable-http", url: url.href, headersEnv };
  }
  throw new TypeError("MCP transport must be stdio or streamable-http.");
}

export function parseBridgeEndpointConfig(raw) {
  object(raw, "MCP endpoint configuration");
  if (raw.version !== 1 || !Array.isArray(raw.endpoints) || raw.endpoints.length > 32) throw new TypeError("MCP endpoint config must use version 1 and contain at most 32 endpoints.");
  const ids = new Set();
  const endpoints = raw.endpoints.map(validateEndpoint);
  for (const endpoint of endpoints) {
    if (ids.has(endpoint.id)) throw new TypeError("Duplicate MCP endpoint id: " + endpoint.id);
    ids.add(endpoint.id);
  }
  return Object.freeze(endpoints.map(endpoint => Object.freeze(endpoint)));
}

export function resolveBridgeEndpointConfig(endpoint, environment = process.env) {
  validateEndpoint(endpoint);
  const resolved = { ...endpoint };
  if (endpoint.transport === "stdio") {
    const env = {};
    for (const [target, source] of Object.entries(endpoint.env || {})) {
      if (environment[source] === undefined) throw new Error("Required environment variable is not set: " + source);
      env[target] = environment[source];
    }
    resolved.env = env;
  } else {
    const headers = {};
    for (const [header, source] of Object.entries(endpoint.headersEnv || {})) {
      if (environment[source] === undefined) throw new Error("Required environment variable is not set: " + source);
      headers[header] = environment[source];
    }
    resolved.headers = headers;
    delete resolved.headersEnv;
  }
  return resolved;
}

export function publicBridgeEndpoint(endpoint, sessions = []) {
  const active = sessions.find(session => session.id === endpoint.id);
  return {
    id: endpoint.id,
    name: endpoint.name,
    transport: endpoint.transport,
    state: active ? "connected" : "disconnected",
    serverInfo: active?.serverInfo || null
  };
}

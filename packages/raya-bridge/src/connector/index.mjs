import {
  createCoreClient,
  createStdioClientTransport,
  createStreamableHttpClientTransport
} from "@ijoolaie/mcp-core";

const ID_PATTERN = /^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$/;
const FORBIDDEN_HEADERS = new Set(["connection", "content-length", "host", "transfer-encoding"]);

function validateId(id) {
  if (typeof id !== "string" || !ID_PATTERN.test(id)) {
    throw new TypeError("Session id must be 1-64 characters: letters, digits, dot, underscore or hyphen.");
  }
  return id;
}

function normalizeHttpUrl(value) {
  const url = value instanceof URL ? new URL(value.href) : new URL(value);
  if (url.username || url.password) throw new TypeError("Credentials in MCP endpoint URLs are not allowed.");
  if (url.hash) throw new TypeError("MCP endpoint URLs must not contain a fragment.");
  const host = url.hostname.toLowerCase().replace(/^\[|\]$/g, "");
  const loopback = host === "localhost" || host === "127.0.0.1" || host === "::1";
  if (url.protocol !== "https:" && !(url.protocol === "http:" && loopback)) {
    throw new TypeError("MCP HTTP endpoints must use HTTPS; plain HTTP is allowed only for loopback.");
  }
  return url;
}

function normalizeHeaders(headers) {
  if (headers === undefined) return {};
  if (!headers || typeof headers !== "object" || Array.isArray(headers)) {
    throw new TypeError("HTTP headers must be an object of string values.");
  }
  const normalized = {};
  for (const [name, value] of Object.entries(headers)) {
    if (!/^[!#$%&'*+.^_`|~0-9A-Za-z-]+$/.test(name)) {
      throw new TypeError("Invalid HTTP header name.");
    }
    if (FORBIDDEN_HEADERS.has(name.toLowerCase())) {
      throw new TypeError("Hop-by-hop and framing headers cannot be overridden.");
    }
    if (typeof value !== "string" || /[\r\n]/.test(value)) {
      throw new TypeError("HTTP header values must be single-line strings.");
    }
    normalized[name] = value;
  }
  return normalized;
}

function normalizeEnv(env) {
  if (env === undefined) return {};
  if (!env || typeof env !== "object" || Array.isArray(env)) {
    throw new TypeError("STDIO environment must be an object of string values.");
  }
  const normalized = {};
  for (const [key, value] of Object.entries(env)) {
    if (!/^[A-Za-z_][A-Za-z0-9_]*$/.test(key) || typeof value !== "string" || value.startsWith("()")) {
      throw new TypeError("Invalid STDIO environment entry.");
    }
    normalized[key] = value;
  }
  return normalized;
}

export function createBridgeConnector({
  clientName = "raya-bridge",
  clientVersion = "0.1.0",
  defaultHttpHeaders = {}
} = {}) {
  const sessions = new Map();
  const baseHeaders = normalizeHeaders(defaultHttpHeaders);

  function ensureAvailableId(id) {
    validateId(id);
    if (sessions.has(id)) throw new Error("An MCP session with this id already exists.");
    return id;
  }

  async function attach(id, kind, endpoint, transport, client) {
    try {
      await client.connect(transport);
    } catch (error) {
      await client.close().catch(() => {});
      throw error;
    }
    const session = {
      id,
      kind,
      endpoint,
      client,
      serverInfo: client.getServerVersion?.() ?? null
    };
    sessions.set(id, session);
    return { id, transport: kind, serverInfo: session.serverInfo };
  }

  function getSession(id) {
    const session = sessions.get(validateId(id));
    if (!session) throw new Error("Unknown MCP session.");
    return session;
  }

  const api = {
    async connectStdio({ id, command, args = [], cwd, env = {}, stderr = "inherit" } = {}) {
      ensureAvailableId(id);
      if (typeof command !== "string" || !command.trim()) throw new TypeError("STDIO command is required.");
      if (!Array.isArray(args) || args.some(arg => typeof arg !== "string")) throw new TypeError("STDIO args must be an array of strings.");
      if (cwd !== undefined && (typeof cwd !== "string" || !cwd.trim())) throw new TypeError("STDIO cwd must be a non-empty string.");
      const transport = createStdioClientTransport({
        command,
        args,
        cwd,
        env: normalizeEnv(env),
        stderr
      });
      const client = createCoreClient({ name: clientName, version: clientVersion });
      return attach(id, "stdio", null, transport, client);
    },

    async connectHttp({ id, url, headers = {}, fetch } = {}) {
      ensureAvailableId(id);
      const endpoint = normalizeHttpUrl(url);
      const requestHeaders = { ...baseHeaders, ...normalizeHeaders(headers) };
      const transport = createStreamableHttpClientTransport(endpoint, {
        requestInit: { headers: requestHeaders },
        ...(fetch ? { fetch } : {})
      });
      const client = createCoreClient({ name: clientName, version: clientVersion });
      return attach(id, "streamable-http", endpoint, transport, client);
    },

    async connectEndpoint(config = {}) {
      if (config.transport === "stdio") return api.connectStdio(config);
      if (config.transport === "streamable-http") return api.connectHttp(config);
      throw new TypeError("Unsupported MCP transport; use stdio or streamable-http.");
    },

    async listTools(id) {
      return getSession(id).client.listTools();
    },

    async callTool(id, name, args = {}) {
      if (typeof name !== "string" || !name.trim()) throw new TypeError("Tool name is required.");
      if (!args || typeof args !== "object" || Array.isArray(args)) throw new TypeError("Tool arguments must be an object.");
      return getSession(id).client.callTool({ name, arguments: args });
    },

    async listResources(id) {
      return getSession(id).client.listResources();
    },

    async readResource(id, uri) {
      if (typeof uri !== "string" || !uri.trim()) throw new TypeError("Resource URI is required.");
      return getSession(id).client.readResource({ uri });
    },

    async listPrompts(id) {
      return getSession(id).client.listPrompts();
    },

    async getPrompt(id, name, args = {}) {
      if (typeof name !== "string" || !name.trim()) throw new TypeError("Prompt name is required.");
      if (!args || typeof args !== "object" || Array.isArray(args)) throw new TypeError("Prompt arguments must be an object.");
      return getSession(id).client.getPrompt({ name, arguments: args });
    },

    listSessions() {
      return [...sessions.values()].map(session => ({
        id: session.id,
        transport: session.kind,
        endpoint: session.endpoint ? {
          origin: session.endpoint.origin,
          pathname: session.endpoint.pathname
        } : null,
        serverInfo: session.serverInfo
      }));
    },

    async disconnect(id) {
      const session = getSession(id);
      sessions.delete(session.id);
      await session.client.close();
      return { id: session.id, disconnected: true };
    },

    async closeAll() {
      const current = [...sessions.values()];
      sessions.clear();
      const results = await Promise.allSettled(current.map(session => session.client.close()));
      return {
        closed: results.filter(result => result.status === "fulfilled").length,
        failed: results.filter(result => result.status === "rejected").length
      };
    }
  };
  return Object.freeze(api);;
}
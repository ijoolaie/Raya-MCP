import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { StreamableHTTPServerTransport } from "@modelcontextprotocol/sdk/server/streamableHttp.js";
import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StdioClientTransport, getDefaultEnvironment } from "@modelcontextprotocol/sdk/client/stdio.js";
import { StreamableHTTPClientTransport } from "@modelcontextprotocol/sdk/client/streamableHttp.js";

export const RAYA_MCP_CORE_API_VERSION = "0.1.0";

export function createCoreServer({
  name = "raya-mcp-core",
  version = "0.1.0",
  registerCapabilities
} = {}) {
  if (typeof name !== "string" || !name.trim()) {
    throw new TypeError("Core server name must be a non-empty string.");
  }
  if (typeof version !== "string" || !version.trim()) {
    throw new TypeError("Core server version must be a non-empty string.");
  }
  if (registerCapabilities !== undefined && typeof registerCapabilities !== "function") {
    throw new TypeError("registerCapabilities must be a function when provided.");
  }

  const server = new McpServer({ name, version });
  if (registerCapabilities) registerCapabilities(server);
  return server;
}

export function createCoreClient({
  name = "raya-mcp-client",
  version = "0.1.0",
  capabilities = {}
} = {}) {
  if (typeof name !== "string" || !name.trim()) {
    throw new TypeError("Core client name must be a non-empty string.");
  }
  if (typeof version !== "string" || !version.trim()) {
    throw new TypeError("Core client version must be a non-empty string.");
  }
  if (!capabilities || typeof capabilities !== "object" || Array.isArray(capabilities)) {
    throw new TypeError("Core client capabilities must be an object.");
  }
  return new Client({ name, version }, { capabilities });
}

export function createStdioClientTransport(options) {
  if (!options || typeof options !== "object" || Array.isArray(options)) {
    throw new TypeError("STDIO client transport options must be an object.");
  }
  if (typeof options.command !== "string" || !options.command.trim()) {
    throw new TypeError("STDIO client transport requires a command.");
  }
  return new StdioClientTransport({
    ...options,
    env: { ...getDefaultEnvironment(), ...(options.env || {}) }
  });
}

export function createStreamableHttpClientTransport(url, options = {}) {
  const target = url instanceof URL ? url : new URL(url);
  if (!options || typeof options !== "object" || Array.isArray(options)) {
    throw new TypeError("Streamable HTTP client transport options must be an object.");
  }
  return new StreamableHTTPClientTransport(target, options);
}

export function createStdioTransport() {
  return new StdioServerTransport();
}

export function createStreamableHttpTransport(options = {}) {
  return new StreamableHTTPServerTransport(options);
}
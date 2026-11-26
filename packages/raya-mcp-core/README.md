# @ijoolaie/mcp-core

Raya MCP Core is the protocol foundation for the Raya MCP product family.

## Public API

- `RAYA_MCP_CORE_API_VERSION`: public JavaScript API contract version.
- `createCoreServer(options)`: creates an MCP SDK server and optionally invokes a capability-registration callback.
- `createCoreClient(options)`: creates an MCP SDK client.
- `createStdioClientTransport(options)` and `createStreamableHttpClientTransport(url, options)`: client transports. STDIO client creation merges the SDK's safe default environment with explicit overrides, so standard commands on `PATH` remain available without inheriting the entire parent environment.
- `createStdioTransport()`: creates an MCP STDIO transport.
- `createStreamableHttpTransport(options)`: creates an MCP Streamable HTTP transport.

Core does not import or depend on Raya Local MCP or Raya Bridge. Capability implementations are supplied by consumers through the registration callback.

## Quick start

Install `@ijoolaie/mcp-core` in a Node.js project. The package uses the MCP SDK and declares Zod as a peer dependency.

```js
import { createCoreServer, createStdioTransport } from "@ijoolaie/mcp-core";

const server = createCoreServer({
  name: "my-mcp-server",
  version: "0.1.0",
  registerCapabilities(mcp) {
    mcp.tool("ping", "A simple health check", {}, async () => ({
      content: [{ type: "text", text: "pong" }]
    }));
  }
});

await server.connect(createStdioTransport());
```

A runnable version of this example is included at `examples/minimal-stdio-server.mjs`.

## Current maturity

This is the first extracted Core boundary (0.1.0). It centralizes MCP server construction and transport creation. It does not yet provide a standalone CLI, full transport lifecycle management, or a stable cross-language contract. Those remain explicit follow-up work before a 1.0 release.

## Development

Run `npm test` from this package directory.

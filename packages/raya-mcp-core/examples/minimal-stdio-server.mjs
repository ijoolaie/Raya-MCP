import { createCoreServer, createStdioTransport } from "../src/index.mjs";

const server = createCoreServer({
  name: "raya-mcp-core-standalone-fixture",
  version: "0.1.0",
  registerCapabilities(mcp) {
    mcp.tool("ping", "Return a pong response.", {}, async () => ({
      content: [{ type: "text", text: "pong" }]
    }));
  }
});

await server.connect(createStdioTransport());

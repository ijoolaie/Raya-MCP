# @ijoolaie/bridge

Raya Bridge is the connectivity and administration component in Raya MCP Stack. It depends on **Raya MCP Core** for MCP client and transport primitives, but it does not depend on **Raya Local MCP**.

## MCP endpoint connector

The connector supports MCP servers over STDIO and Streamable HTTP. HTTP endpoints must use HTTPS; plain HTTP is permitted only for loopback addresses. Credentials are supplied as headers, never embedded in endpoint URLs. Session listings omit environment variables, headers and URL query strings.

```js
import { createBridgeConnector } from "@ijoolaie/bridge/connector";

const bridge = createBridgeConnector();

await bridge.connectStdio({
  id: "local-tools",
  command: "node",
  args: ["./my-mcp-server.mjs"],
  cwd: process.cwd()
});

const { tools } = await bridge.listTools("local-tools");
const result = await bridge.callTool("local-tools", tools[0].name, {});
await bridge.disconnect("local-tools");
```

For Streamable HTTP, call `connectHttp({ id, url, headers })`. Use HTTPS for remote endpoints. The connector is available programmatically and through the Admin Panel MCP page. The panel lists configured profiles, connects/disconnects, discovers tools and invokes selected tools; profile creation/editing remains a local configuration-file operation.

## Admin Panel

The loopback Admin Panel manages the configured Raya tunnel process, workspace allowlist, selected runtime settings and preconfigured external MCP endpoints. It does not require Raya Local MCP to be installed just to run the panel.

Start from a workspace/configuration root:

```powershell
$env:RAYA_WORKSPACE_ROOT = (Get-Location).Path
$env:RAYA_PANEL_NO_BROWSER = "true" # optional for headless use
node .\node_modules\@raya\bridge\src\admin-panel\server.mjs
```

The panel binds to loopback only. It prints a one-time token URL unless a local token is provided. The panel and MCP endpoint configuration files are external to this package:

- `RAYA_MCP_ENDPOINTS_FILE` — defaults to `<workspace-root>\\raya-admin-panel\\mcp-endpoints.json`. Start from the packaged `mcp-endpoints.example.json` or the repository copy at `raya-admin-panel/mcp-endpoints.example.json`. The panel loads the file at first endpoint access; restart the panel after editing it.
- Endpoint config schema is `{ version: 1, endpoints: [...] }`. Each endpoint has a unique `id`, `name` and `transport` (`stdio` or `streamable-http`). STDIO endpoints specify `command`, optional `args`/`cwd` and an `env` map of target variable names to names in the Bridge process environment. HTTP endpoints specify `url` and an optional `headersEnv` map of header names to environment-variable names. Secret values belong only in the Bridge process environment, not in the JSON file.
- The Admin Panel API exposes `GET /api/mcp/endpoints` and authenticated POST routes `/api/mcp/connect`, `/api/mcp/disconnect`, `/api/mcp/tools` and `/api/mcp/call`. The MCP page in the panel uses these routes to connect, discover tools and invoke a selected tool.
- Only configure trusted STDIO commands and trusted MCP endpoints: connected tools execute with the Bridge user's permissions. Remote endpoints require HTTPS; plain HTTP is allowed only for loopback. The configuration file is local and is not writable through the panel API.

- `RAYA_PANEL_CONFIG_FILE` — defaults to `<workspace-root>\raya-admin-panel\panel-config.json`
- `RAYA_CONFIG_FILE` — defaults to `<workspace-root>\raya-config.json`
- `RAYA_TUNNEL_EXE` — defaults to the existing external Tunnel Client location under the workspace root
- `RAYA_TUNNEL_PROFILE` — defaults to `raya-local`
- `RAYA_TUNNEL_HEALTH_URL`, `RAYA_TUNNEL_READYZ_URL`, `RAYA_TUNNEL_MCP_HEALTH_URL` — optional health endpoint overrides
- `RAYA_PANEL_PORT` — defaults to 8766
- `RAYA_PANEL_NO_BROWSER=true` — prevents automatic browser launch

The Tunnel Client remains an external dependency and is not bundled.

## Independence boundary

- This package depends on `@ijoolaie/mcp-core`; npm resolves that dependency automatically.
- It has no dependency on `@ijoolaie/local-mcp`. It can connect to independently installed MCP servers and can run its Admin Panel without Local MCP.
- The optional root-level `raya-admin-panel/stdio-launcher.mjs` remains a compatibility adapter for launching the Local MCP server; it is not required by this Bridge package.
- Version 0.1.0 provides the programmatic connector and the Admin Panel endpoint-management workflow for preconfigured profiles. A profile editor and production release policy remain future work.

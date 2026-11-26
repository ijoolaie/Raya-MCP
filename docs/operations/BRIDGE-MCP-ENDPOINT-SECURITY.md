# Raya Bridge — MCP Endpoint Security Boundary

**Snapshot:** 2026-11-26\
**Scope:** `@ijoolaie/bridge@0.1.0` programmatic connector and Admin Panel endpoint API.

## Trust model

- The Admin Panel binds to loopback and requires its bearer token for dashboard/API access.
- MCP endpoint profiles are read from the local `mcp-endpoints.json` file. The API accepts only a configured profile ID; it does not accept a command, executable path, endpoint URL or header definitions from the browser.
- The example configuration is safe to distribute. The live `mcp-endpoints.json` is machine-local, ignored by Git and must not be committed.
- Profile edits require local filesystem access and a panel restart; there is no browser/API profile editor.

## STDIO endpoints

- A configured STDIO command starts as a child process under the Bridge user's Windows account and inherits the safe default process environment plus explicitly mapped environment overrides.
- This is not a sandbox. The executable and arguments must be trusted; connected MCP tools may perform any action permitted to that process.
- The configuration maps child environment names to names of variables in the Bridge process environment. Secret values are resolved only at connection time and are not stored in the JSON file or returned by the endpoint-list API.

## Streamable HTTP endpoints

- Remote MCP endpoints must use HTTPS. Plain HTTP is accepted only for loopback (`localhost`, `127.0.0.1`, `::1`).
- URL credentials and fragments are rejected. Authentication is supplied through headers mapped to Bridge process environment variables.
- Endpoint-list responses omit the URL, headers, environment mappings and secret values. Session summaries do not expose URL query strings.

## Tool invocation

- `/api/mcp/call` is bearer-token protected. It forwards a tool call to the selected configured endpoint.
- Tool invocation is an action against the remote server, not a read-only inspection guarantee. Only connect to MCP servers and invoke tools that the operator trusts.
- The dashboard renders tool results as text, not executable HTML.

## Validation evidence and limits

- Automated checks cover config schema, duplicate IDs, HTTPS/loopback policy, URL credential rejection, unsafe headers, environment-variable indirection, connector STDIO/HTTP operations and Admin Panel UI script parsing.
- Packed-consumer acceptance verifies token enforcement, configured endpoint listing, connection, tool discovery, tool invocation and disconnect.
- No browser visual review, external production endpoint review, Windows ACL audit of the live config file, or sandboxing claim is made.
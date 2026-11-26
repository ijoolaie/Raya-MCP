# @ijoolaie/local-mcp

Raya Local MCP provides local workspace and machine capabilities on top of Raya MCP Core. It can run directly over MCP STDIO or authenticated Streamable HTTP; it does not require Raya Bridge.

## Run

```powershell
npm install @ijoolaie/local-mcp
node --input-type=module -e "import '@ijoolaie/local-mcp'"
```

For the normal CLI-style entry point, run `node src/server.mjs` from this package directory. Add `--http` to select Streamable HTTP; HTTP mode requires `MCP_BEARER_TOKEN` with at least 32 characters. `MCP_ALLOWED_ROOTS` can be set to a semicolon-separated list of allowed workspace roots.

## Architecture

- Depends on `@ijoolaie/mcp-core` for MCP server construction and transport factories.
- Owns filesystem, process/session, search, settings, URL and document-format tool implementations.
- Does not depend on Raya Bridge.
- The current package extraction preserves the existing tool handlers and root application wrapper for compatibility.

## Maturity

Version 0.1.0 is the first extracted package boundary. Independent consumer install and full clean-room acceptance are still required before publication.

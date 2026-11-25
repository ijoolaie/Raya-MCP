# Raya Admin Panel

First-party local management UI for Raya Local MCP.

## Responsibilities

- Display local panel/tunnel health and bounded operational logs.
- Start, stop and restart only the Tunnel Client process owned by this panel.
- Manage allowed workspace roots and bounded runtime settings.
- Launch the local MCP server through the validated STDIO launcher.

## Local setup

1. Install the supported Node.js runtime.
2. Copy `panel-config.example.json` to `panel-config.json`.
3. Replace the example workspace path with the local Raya repository path and the approved workspace roots. Keep the repository root in the list.
4. Create the local runtime settings file `../raya-config.json` using the documented settings schema.
5. Start the panel using the platform-specific launcher. The panel binds to loopback port 8766 by default.

The active `panel-config.json`, `raya-config.json`, Tunnel Client profile and credential files are local runtime state and must never be committed.

## Authentication and secrets

The panel requires a high-entropy bearer token. The current Windows desktop launcher stores the API key and panel token using user-scoped DPAPI and also creates a restricted-ACL local token file for URL reuse. The panel server can load the token from its environment or that local file. Treat the file as a credential: protect its ACL, never log its value, and never copy it into source control, screenshots or support bundles.

The implementation and historical operational notes have had inconsistent descriptions of token persistence. This README follows the inspected current launcher/server behavior. Revisit the design before claiming memory-only token storage.

## Security boundaries

- The listener is loopback-only by default; do not expose it directly to the internet.
- API requests require bearer authentication and host/origin checks.
- Workspace roots must be local, existing, canonical directories. Drive roots, system locations, symlinks/junctions and nested/duplicate roots are rejected by the launcher.
- PowerShell runs with the Windows user's privileges. It is not sandboxed; keep it disabled unless explicitly needed.
- The launcher validates configured roots before passing them to the MCP server.
- Production filesystem operations remain path-based; race-free TOCTOU confinement has not been accepted.

## Verification

The panel smoke-test source is included for controlled isolated use. Do not run tests against a live panel configuration without first checking their side effects. The root `npm test` script is known to mutate `raya-config.json`; do not run it against a real configuration.

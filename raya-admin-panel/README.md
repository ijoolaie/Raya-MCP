# Raya MCP Admin Panel

A local-only Windows dashboard for managing the Raya MCP tunnel, workspace allowlist and runtime settings.

## First-time setup

1. Copy `raya-config.example.json` to `raya-config.json` in the repository root.
2. Copy `raya-admin-panel/panel-config.example.json` to `raya-admin-panel/panel-config.json`.
3. Edit both files. In `panel-config.json`, set `allowedRoots` to existing local directories and include the repository root itself. Keep `powershellEnabled` false unless privileged shell access is intentionally required.
4. Install dependencies with `npm install`.
5. Start the panel from the same PowerShell session that contains `CONTROL_PLANE_API_KEY` by running `./start-raya-admin-panel.ps1`.

The local configuration files are intentionally ignored by Git. Never commit API keys, bearer tokens, DPAPI material, or machine-specific workspace paths.

## Security model

- The dashboard binds only to loopback (`127.0.0.1`).
- API calls require a random bearer token and reject non-loopback clients and unexpected Host/Origin headers.
- Workspace roots must be existing local directories; junctions, symlinks, drive roots, Windows system directories, duplicate roots and nested roots are rejected.
- The repository root is locked and cannot be removed from the allowlist.
- The panel only stops a tunnel process started by that panel. It does not adopt or stop an independently running tunnel.
- PowerShell execution is privileged and is not a sandbox.
- The separate IranPriceScout tunnel on port 8765 is outside this panel's scope and must not be changed.

## Verification

`node raya-admin-panel/smoke-test.mjs` exercises the local panel API. It requires the two local configuration files described above and restores their contents after the test.
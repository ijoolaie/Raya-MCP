# Import Manifest

Source: `C:\Users\Ali\local-mcp-bridge`
Staging: `C:\Users\Ali\Raya-Local-MCP-staging-20261126`

## Included
- Product packages: `packages/raya-mcp-core/`, `packages/raya-local-mcp/`, `packages/raya-bridge/`, and `packages/raya-stack/`.
- Root compatibility entry points: `server.mjs`, `advanced-tools.mjs`, `format-tools.mjs`, `url-tools.mjs`, plus Windows launchers.
- `package.json`, `package-lock.json`, `scripts/verify-modular-packages.mjs`, and package-specific/root smoke and security tests.
- Raya STDIO, panel and hidden-startup launchers.
- Admin Panel source and preconfigured MCP endpoint-management UI; non-secret `mcp-endpoints.example.json` included, local `mcp-endpoints.json` excluded.
- Raya-specific docs; excluded the Raya Agent import manifest.
- `.github/` workflows and Raya MCP agent guidance.
- `assets/` branding.
- `.native-probe/` source/tests/docs; compiled outputs excluded.
- Stability baseline.
- Broker prototype source/specs/build scripts in `research/`; generated TLC state traces, fingerprints, binary artifacts, graph dumps, logs and run CSVs excluded.

## Excluded
- `tools/`: unrelated wardrobe/Blender/3D-character content.
- `workspaces/`: local workspace data.
- Panel config, MCP endpoint config, endpoint secrets and backup variants; root config and YAML backups.
- Tunnel log archive, temporary probes, unrelated marketplace artifacts.
- Dependencies, caches, builds, compiled artifacts and backups.
- Tunnel Client source (external); Raya Agent and Control Center source (separate projects).

Root compatibility entry paths were preserved while runtime code was extracted into four npm workspace packages. Core, Local MCP and Bridge have independent package identities; the Stack meta-package pins a tested component set and declares four install profiles. Bridge depends on Core public client/transport APIs but not Local MCP. The package manifests and lockfile were updated; `jszip` remains a direct dependency and optional native-test aliases pointing to excluded binaries were removed. The staging tree has not been committed or pushed.

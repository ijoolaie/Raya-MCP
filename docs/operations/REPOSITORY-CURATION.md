# Repository Curation Record

**Snapshot:** 2026-11-26  
**Repository:** `ijoolaie/Raya-MCP` (private)  
**Local source inspected:** `C:\Users\Ali\local-mcp-bridge`  
**Mode:** Inventory and classification; no local files deleted or moved.

## Repository identity

The GitHub repository already exists and is private. Do not create a duplicate repository. The local source working tree is on branch `security/hardlink-alias-policy-probe` and contains a large set of modified and untracked entries. It must not be bulk-staged or pushed.

At inspection, the local working tree reported 15 modified tracked files and 85 untracked entries. Preserve this state. No reset, clean, checkout, stash, bulk stage, or broad formatting operation is authorized by this curation record.

## Keep as first-party product source

Subject to a targeted secret/config review before upload:
- Root MCP runtime: `server.mjs`, `advanced-tools.mjs`, `format-tools.mjs`, `url-tools.mjs`.
- Package manifests and lockfile: `package.json`, `package-lock.json`.
- Maintained test sources: `smoke-test.mjs`, `format-smoke-test.mjs`, `http-smoke-test.mjs`, `url-security-test.mjs`.
- First-party startup wrappers: `start-mcp-stdio.cmd`, `start-mcp-stdio-panel.cmd`, and reviewed launcher scripts.
- First-party panel source: `raya-admin-panel/server.mjs`, `stdio-launcher.mjs`, `index.html`, and its README.
- Raya-branded assets under `assets/raya-mcp-*`.
- Durable architecture, operations, security, dependency and testing documentation.
- CI workflows and repository agent instructions that are directly relevant to this product.

## Keep separate from production source

These items are related to Raya engineering but are not part of the production runtime:
- `.native-probe/` and `broker-prototype-v2-20261106/`: filesystem security research/prototypes. Retain only in a clearly labelled research area or a separate research branch after confirming no production dependency.
- Historical validation copies such as `security-validation-20261026/`: archive/reference only; do not compile or package as the live implementation.
- Checkpoints and dated experiments: retain outside the release source tree unless a specific artifact is referenced by an active design document.

## Exclude from this repository

The following were observed in the local workspace and are not Raya Local MCP product source:
- `tools/`: contains unrelated 3D/wardrobe/Blender scripts, images and assets.
- `workspaces/ara3dgen-generator/` and `workspaces/empire-of-companies/`: separate workspaces/projects.
- `iran-marketplace-probe-*`: unrelated marketplace investigation artifacts.
- `raya-tool-test-temp/`: generated acceptance fixtures and test outputs.
- `checkpoints/`: user checkpoint archives.
- `backup-*/`, timestamped `*.bak` files and backup config copies: historical copies, not canonical source.
- `downloads/` Tunnel Client binaries, archives and third-party executable distributions.
- Tunnel/client logs, diagnostic exports, generated endpoint URL text files and other runtime artifacts.
- `node_modules/` and nested workspace dependency trees.

Exclusion means "do not include in the Raya Git repository"; it does not authorize deleting or moving these local files.

## Configuration and secret exclusion

Never commit live instances of:
- `raya-config.json`
- `raya-admin-panel/panel-config.json`
- `raya-local.yaml` or any Tunnel Client profile
- DPAPI files, panel-token files, API keys, bearer tokens, cookies or environment dumps
- Local workspace root lists, absolute user paths, runtime logs or process snapshots

Create sanitized `.example` templates with placeholder values only after schema and code-path review. The current panel config contains user-specific workspace roots and must not be copied verbatim.

## External Tunnel Client

Tunnel Client is a separately distributed external dependency. Keep only a versioned integration contract and verified provenance/checksum/license information. Do not vendor its executable or archive.

## Required review before publishing local-only files

1. Review each source file for embedded credentials, user-specific paths, runtime state and third-party copied code.
2. Confirm each panel/launcher file is required by the supported startup path.
3. Replace live config with sanitized example templates.
4. Verify that every published document distinguishes verified, observed, unknown and historical evidence.
5. Use a dedicated curation branch and a pull request. Do not push the dirty local branch directly.
6. Run only explicitly approved, non-destructive checks. In particular, the documented `npm test` flow has a config side effect and must not be used against the live config.

## Completion status

This record documents the classification and safe publishing rules. It does not claim that local files have been moved, deleted, uploaded or accepted for release. The local working tree remains untouched. The branch containing this curation record is a review branch and is not merged to `main`.

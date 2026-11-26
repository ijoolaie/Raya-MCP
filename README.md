# Raya MCP Stack

A modular MCP product family composed of **Raya MCP Core**, **Raya Local MCP**, and **Raya Bridge**. Users can choose an individual component or the integrated Stack. `@ijoolaie/mcp-core@0.1.0`, `@ijoolaie/local-mcp@0.1.0`, `@ijoolaie/bridge@0.1.0` and the `@ijoolaie/stack@0.1.0` meta-package now live in `packages/` as separate npm workspace packages. Local MCP and Bridge depend on Core; Bridge does not depend on Local MCP. Bridge's programmatic connector supports STDIO and Streamable HTTP MCP servers. The Admin Panel endpoint-management UI and GitHub Packages private publishing workflow are integrated in this working tree; hosted workflow execution and actual private publication remain pending. All four install profiles have passed clean-consumer acceptance.

The current executable is the **Raya Local MCP** implementation: a Windows-first local MCP server for development workspaces, exposed through STDIO and an optional authenticated Streamable HTTP endpoint.

## Current staging status

The current curated staging state and phase-by-phase evidence are maintained in [PROJECT-STATUS.md](PROJECT-STATUS.md), [docs/IMPORT-REVIEW.md](docs/IMPORT-REVIEW.md), and [docs/ACCEPTANCE-RESULTS.md](docs/ACCEPTANCE-RESULTS.md). Historical dated audit notes elsewhere in this repository describe the source tree at their stated snapshot dates; do not treat them as a current release declaration.

The Stack meta-package pins all three components at compatible versions and exposes Core-only, Local-MCP-only, Bridge-only and Full-Stack profiles through its manifest. See [the Stack package guide](packages/raya-stack/README.md).

The approved target product architecture is documented in [Modular Product Architecture](docs/architecture/MODULAR-PRODUCT-ARCHITECTURE.md). Raya MCP Core, Raya Local MCP and Raya Bridge are separate independently installable workspace packages with explicit package boundaries. Their GitHub Packages publishing configuration and release workflow are integrated in this working tree; hosted validation and actual publication remain pending. Root-level entry points remain compatibility wrappers for existing launchers and tests. Bridge's programmatic connector and Admin Panel workflow for preconfigured endpoints are implemented; endpoint-profile editing remains pending; the Stack publishing workflow is configured but awaits hosted validation and an intentional release.

## Implemented tool families

- Workspace: workspace_roots, workspace_list
- Files: file_read, file_read_chunk, file_write, file_write_chunk, get_file_info, read_multiple_files, create_directory, list_directory, move_file, edit_block, read_file_tail
- Process/session: start_process, read_process_output, interact_with_process, list_sessions, force_terminate, list_processes, kill_process
- Search: start_search, get_more_search_results, list_searches, stop_search
- Settings/diagnostics: get_config, set_config_value, get_usage_stats, get_recent_tool_calls, get_prompts, give_feedback_to_desktop_commander
- Web: read_url (HTTPS/text only; public-address checks and size/time limits)
- XLSX: excel_read_workbook, excel_read_range, excel_search_workbook, excel_write_cells, excel_set_formula
- DOCX: docx_read, docx_search, docx_create, docx_replace_text
- PDF: pdf_extract_text, pdf_info, pdf_create, pdf_add_text, pdf_merge

## Format limitations

- Excel: .xlsx only; .xls and .xlsm are not supported. Formula strings are blocked in excel_write_cells; the separate excel_set_formula tool permits a restricted formula subset. Complex workbook features may not round-trip perfectly.
- DOCX: text extraction/search and basic creation are supported. Text extraction does not preserve formatting; docx_replace_text edits exact matches within individual text runs and preserves surrounding package parts, but cross-run matches are not supported.
- PDF: extraction, metadata, basic text creation, and merge are supported. PDF creation and overlays use standard Helvetica and may not render Persian/Arabic or other complex scripts correctly. pdf_add_text supports overlays, not arbitrary content/object editing.
- URL fetching only supports public HTTPS text/HTML/JSON/XML and has SSRF protections, but is not a universal browser or authenticated web session.

## Safety

- File operations validate configured-root boundaries and reject tested traversal/static symlink or reparse-point escapes; race-free TOCTOU confinement is not proven. Keep workspace roots and their parent chain writable only by trusted principals; see [docs/WINDOWS-FILESYSTEM-TOCTOU-DESIGN.md](docs/WINDOWS-FILESYSTEM-TOCTOU-DESIGN.md).
- Shell and PowerShell execution are privileged, not sandboxed.
- Managed processes have bounded sessions/output and process-tree termination, but this does not create a Windows Job Object or a security sandbox.
- Never log or persist API keys, bearer tokens, cookies, or environment secrets.
- HTTP mode binds to localhost by default and requires a bearer token of at least 32 characters. Do not expose it directly to the internet.
- Do not touch or stop unrelated tunnels or services.

## Install and run

From the repository root in a fresh clone:

```powershell
npm ci
npm run check
npm run verify:modular
npm start
```

For authenticated Streamable HTTP mode, set `MCP_BEARER_TOKEN` to a private value of at least 32 characters, then run `npm run start:http`. Optional `MCP_HOST` and `MCP_PORT` default to `127.0.0.1` and `8765`.

The Admin Panel supports preconfigured external MCP endpoints through `raya-admin-panel/mcp-endpoints.json`; copy and edit the packaged `mcp-endpoints.example.json` (also present at `raya-admin-panel/mcp-endpoints.example.json`) and keep all secret values in environment variables. The panel and Windows launchers also require machine-local configuration and the separately installed Tunnel Client. See [raya-admin-panel/README.md](raya-admin-panel/README.md). Do not commit `panel-config.json`, `raya-config.json`, API keys, panel tokens, tunnel profiles or other local credentials.

## Development

- [Architecture](ARCHITECTURE.md)
- [Development guide](DEVELOPMENT.md)
- [Security](SECURITY.md)
- [Repository structure](REPOSITORY-STRUCTURE.md)
- [Import manifest](IMPORT-MANIFEST.md)
- [Operations index](docs/operations/START-HERE.md)
- [GitHub Packages publishing](docs/operations/GITHUB-PACKAGES-PUBLISHING.md)

The aggregate `npm test` is not read-only: `format-smoke-test.mjs` persists `search.maxDepth=8` to `raya-config.json` without restoring the prior value, and the HTTP smoke test starts a local listener. Review [docs/operations/DEPENDENCIES-AND-TESTS.md](docs/operations/DEPENDENCIES-AND-TESTS.md) and [docs/operations/TEST-COVERAGE.md](docs/operations/TEST-COVERAGE.md) before running tests.

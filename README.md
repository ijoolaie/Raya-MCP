# Raya Local MCP Bridge

Windows-first MCP server for local development workspaces, exposed through stdio and an optional authenticated Streamable HTTP endpoint.

## Project architecture

Raya Local MCP is one integrated product organized into three cooperating modules:

1. **MCP protocol integration** — MCP SDK integration, tool registration, STDIO and optional authenticated Streamable HTTP transports.
2. **Local MCP server** — Windows-local filesystem, search, process/session, Office/PDF and URL capabilities.
3. **Bridge & connectivity** — validated launchers, local admin panel and integration with the external Tunnel Client.

The Tunnel Client is a separately distributed dependency; its binaries and source are not vendored here. The Admin Panel and launcher are first-party Raya components and are being brought under the repository's source and release review.

Architecture: [Project architecture](docs/architecture/PROJECT-ARCHITECTURE.md)  
Repository boundaries and publishing rules: [Repository curation](docs/operations/REPOSITORY-CURATION.md)

## Current implementation status (2026-11-01)
- Core process/session, filesystem, search, configuration, diagnostics, URL retrieval, and PDF/DOCX/XLSX tools are implemented.
- GitHub repository: https://github.com/ijoolaie/Raya-MCP (Private), default branch `main`; local source was first committed and pushed on 2026-11-01. Preserve any uncommitted local changes.
- PowerShell execution is gated by MCP_ENABLE_POWERSHELL=true and runs with the server user's Windows permissions. It is not a sandbox.
- Local server verification: JSON-RPC `tools/list` and the current test inventory report 45 tools; `npm run lint` passed (Node syntax checks); latest `npm test` passed (42 core checks, 34 format/config checks, HTTP unauthorized 401 and authenticated initialize 200, 36 URL-security checks). These tests do not individually acceptance-test every tool.
- ChatGPT connector verification (2026-10-25): the connected catalog exposes 45 tools and live calls confirmed advanced filesystem, process, search, DOCX/XLSX/PDF, and URL tools are callable. This verifies availability and selected real operations, not exhaustive edge-case acceptance of every tool.

## Tool families
- Workspace: workspace_roots, workspace_list
- Files: file_read, file_write, read_multiple_files, read_file_tail, get_file_info, create_directory, list_directory, move_file, edit_block
- Process/session: run_check, powershell_execute, start_process, read_process_output, interact_with_process, list_sessions, force_terminate, list_processes, kill_process
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

Dependency security update (2026-10-26): pinned @modelcontextprotocol/sdk 1.32.2 and zod 4.6.7; added an npm override for uuid 11.1.1 to address the vulnerable uuid 8.x transitively used by ExcelJS 4.4.0. These exact versions were first tested in an isolated copy, then installed here. Current npm audit reports 0 vulnerabilities; npm test and npm run check pass. The npm install was run with --ignore-scripts. The already-running tunnel child may still have the prior dependency versions loaded; restart only from the original tunnel PowerShell window when ready to activate the disk update. Backups of the pre-update package.json and package-lock.json are in backup-security-20261026.

## Safety
- File operations validate configured-root boundaries and reject tested traversal/static symlink or reparse-point escapes; size abuse and accidental overwrites are guarded. Race-free TOCTOU confinement is not proven: a concurrently writable ancestor could theoretically change between validation and a path-based filesystem operation. Keep workspace roots and their parent chain writable only by trusted principals; see docs/WINDOWS-FILESYSTEM-TOCTOU-DESIGN.md.
- Shell and PowerShell execution are privileged, not sandboxed.
- Process termination should use managed session IDs; OS PID termination additionally requires expected process name and explicit confirmation.
- Never log or persist API keys, bearer tokens, cookies, or environment secrets.
- HTTP mode binds to localhost by default and requires a bearer token of at least 32 characters. Do not expose it directly to the internet.
- Do not touch or stop the separate IranPriceScout tunnel on port 8765.

## Install/run
    Set-Location C:\Users\Ali\local-mcp-bridge
    npm install
    npm run check
    npm test

The stdio tunnel launcher is start-mcp-stdio.cmd. Keep CONTROL_PLANE_API_KEY in the tunnel process environment; never put it in YAML or source files.

## Windows command and output compatibility (2026-10-25)
- `run_check` routes `npm_test` and `npm_lint` through `cmd.exe /d /s /c npm.cmd ...` on Windows. Node `spawn()` does not execute `.cmd` shims directly with `shell: false`; see the [Node.js child_process documentation](https://nodejs.org/api/child_process.html#spawning-bat-and-cmd-files-on-windows).
- `read_file_tail` returns the extracted end of the file in the `tail` field. It intentionally avoids naming a nested data property `content`, which can be confused with the MCP result envelope by some connector response adapters.
- Verification after the fix: `npm run check` and `npm test` pass (45 tools; 29 core assertions; 34 format/config checks; HTTP unauthenticated request blocked and authenticated initialize accepted). The connected tunnel process must be restarted to load these source changes.

## Live verification update (2026-10-25)
- The connected catalog exposes 45 Raya tools; `workspace_roots`, `workspace_list`, `get_config`, `read_multiple_files`, `read_file_tail`, `get_file_info`, and `get_prompts`; `read_url` also returned HTTP 200 were successfully called in the latest session.
- `run_check(npm_test)` passed with exit code 0 (29 core assertions, 34 format/config checks, HTTP authentication checks passed). Direct `npm run lint` passed; `lint` aliases syntax checks and is not a separate lint engine. A live `read_url` call to `https://example.com/` returned HTTP 200 and 577 bytes of HTML. A disposable managed `ping.exe` session was terminated with `force_terminate`; the `kill_process` guard correctly rejected direct termination of a Raya-managed PID.
- `run_check(npm_lint)` encountered intermittent connector `MCP server is not initialized` errors in the latest session, so direct `npm run lint` is the current passing evidence for lint/check.
- Loopback `/health/mcp` shows successful initialize and a running child, but `tools_list.ok=false` / `retained_count=0`. This records whether the tunnel observed forwarded `tools/list` traffic; it does not contradict the visible 45-tool catalog and successful calls. Do not restart the healthy tunnel just to force this observation field green.

## Dependency security review (2026-11-01)
A newly published moderate Dependabot advisory affects transitive `minimatch` instances in the ExcelJS/archiver dependency tree. It is not yet remediated; see [docs/DEPENDENCY-SECURITY-20261101.md](docs/DEPENDENCY-SECURITY-20261101.md). The earlier 2026-10-26 `npm audit` result is historical, not the current dependency status.

## References
- Desktop Commander MCP: https://github.com/wonderwhy-er/DesktopCommanderMCP
- Docs: https://desktopcommander.app/mcp/

Parity means equivalent user outcomes, not identical internals. Keep the parity matrix and handoff synchronized with real test evidence.





## Follow-up validation (2026-10-25)`r`n- Live tunnel call `read_file_tail` returned the expected `tail` field with actual file text; live `run_check(npm_test)` returned exit code 0.`r`n- Added a `lint` npm script that delegates to the existing `check` script (Node syntax checks), because the `npm_lint` preset previously targeted a missing script. This is a syntax-check alias, not a separate ESLint/biome ruleset.`r`n- `npm run lint`, `npm test`, and live `run_check(npm_lint)` all returned exit code 0.`r`n- `/healthz` and `/readyz` both returned HTTP 200. `/health/mcp` reports successful initialize on the running child, but `tools_list.ok=false` and no observed tool names. Tunnel-client health documentation says this is historical discovery evidence and is only updated after forwarded `tools/list` traffic; health reads do not trigger discovery. This remains unresolved at the tunnel/connector discovery layer, not proven to be a bridge tool failure.

## Tunnel discovery observation (2026-10-25)
A redacted support export of the most recent 10 minutes showed successful initialize and 	ools/call forwarding, with HTTP 200 delivery, but no 	ools/list RPC. Therefore the tunnel's 	ools_list.ok=false accurately means that discovery RPC was not observed; it does not contradict the fact that ChatGPT exposes the tool catalog and calls are succeeding. Official troubleshooting recommends connector discovery/testing while the runtime is running and verifying that ChatGPT selects the same tunnel ID. Do not change the MCP server merely to force this health observation; revalidate connector discovery first.

## Additional live acceptance — 2026-10-26
- Live diagnostic calls succeeded for usage statistics, recent call metadata, configuration, prompts, sessions, and directory listing. Recent call metadata is bounded and does not include command contents.
- Live XLSX workbook/range/search reads passed against the controlled fixture. Live DOCX create/read/search/exact replacement/readback passed using a new fixture. Live PDF create/merge/info/text-overlay/extraction passed using controlled fixtures; these tests created or modified only new files under `raya-tool-test-temp`.
- Three stale search sessions were stopped and confirmed removed.
- The intermittent `MCP server is not initialized` error reproduced on a batched format call; sequential recovery and individual calls succeeded. In the 2026-10-26 follow-up, eight sequential `get_config` calls succeeded and `npm test`/`npm run lint` passed. Root cause remains unconfirmed; evidence points to an intermittent connector lifecycle/concurrency issue, not a proven bridge process crash. Existing logs contain an older duplicate-listener `EADDRINUSE` on diagnostic port 18767; do not assume that explains the current connector error. No tunnel restart, root/config change, or IranPriceScout tunnel change was made. The stdio launcher was also corrected to stop explicitly setting `MCP_ENABLE_POWERSHELL=true`, preserving disabled-by-default PowerShell.

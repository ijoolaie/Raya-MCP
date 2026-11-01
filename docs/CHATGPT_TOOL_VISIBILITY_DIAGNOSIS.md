# ChatGPT MCP tool visibility diagnosis

Updated: 2026-10-25 (historical diagnosis preserved below; latest verification is at the end)

## Initial verification (2026-10-23; superseded by latest live verification below)
- The live ChatGPT tool catalog exposes only six Raya MCP tools: workspace_roots, workspace_list, file_read, file_write, run_check, powershell_execute.
- Local server tests have been run using the absolute npm.cmd path. `npm run check` passed with exit code 0. `npm test` passed with exit code 0: core smoke reported 45 tools and 25 successful checks; format/config smoke reported 45 tools and 34 successful checks; HTTP smoke verified unauthenticated requests are rejected (401) and authenticated initialize returns 200.
- The predefined run_check(npm_test) fails with `spawn npm ENOENT`; this is a PATH issue in that runner. It does not reproduce when npm.cmd is called by absolute path.
- Intermittent `MCP server is not initialized` errors have occurred, followed by successful sequential retries. Avoid parallel calls and do not restart a healthy tunnel solely on this evidence.
- No source code, secrets, allowlists, or tunnel process were changed during this diagnosis.

## Initial conclusion (2026-10-23; superseded)
The local server and its test suite support a 45-tool inventory, but the currently exposed ChatGPT connector schema contains six tools. This is a mismatch between local server registration and the ChatGPT-side published tool catalog. The currently exposed local MCP tools do not provide an operation to edit the ChatGPT connector manifest/schema, so the catalog cannot be safely repaired from this bridge alone.

## Original remediation plan (historical; live verification now passes)
The live connector catalog now exposes 45 Raya MCP tools, and the advanced-tool invocation checks below pass. The connector manifest/control-plane was not edited from this bridge during the latest session; the observed outcome is verified, while the exact reason the catalog refreshed remains unknown.

Do not claim the connector is fixed until steps 3–4 pass. Do not place tokens in source/config files, widen allowed roots, disable path checks, or touch the separate IranPriceScout tunnel on port 8765.

## Reporting rule
Local test success is not proof of ChatGPT-side tool availability. Report the two states separately.

## Direct system-level follow-up - 2026-10-23 17:55 local
- Used the running tunnel's loopback health API. `/healthz` and `/readyz` returned live/ready. The active tunnel is version `0.0.17+a248b7f725fc989c1eb26680cf4351ee33421f61`; control-plane polling showed zero consecutive failures and dispatcher activity continued.
- The tunnel health snapshot reports MCP `initialize.ok=true`, child running, but `tools_list.ok=false` and `retained_count=0`. This is direct evidence the tunnel's observed discovery state does not contain the local tools, even though tool calls are being dispatched. Logs show repeated `initialize` and `tools/call`, but no recorded `tools/list` RPC method.
- Repeated the local protocol test with the tunnel's negotiated protocol version `2025-11-25`, sent `notifications/initialized`, then `tools/list`: **45 tools**, 21,229-byte response. A schema-shape audit found no missing descriptions, wrong root schema type, missing required properties, or invalid tool-name formats. A direct `tools/call` to the otherwise ChatGPT-hidden `get_config` returned a successful response.
- The read-only `tunnel-client admin tunnels get` confirmed the active tunnel ID and metadata. The `doctor --profile raya-local` command failed only because it tried to bind the health port `127.0.0.1:8080` already occupied by the currently running healthy daemon; do not stop the daemon based on that result.
- Current best diagnosis: local server definitions and schemas are valid; the connected ChatGPT catalog still statically exposes six tools, and the tunnel's own observed discovery cache also shows no retained tool list. The precise control-plane/connector-side cause remains unproven. No API keys were printed or changed, no tunnel was restarted, and IranPriceScout was untouched.
- Next action requires a connector/control-plane operation that triggers fresh MCP `tools/list` discovery or access to the hosted Raya MCP app tool declaration. Neither is exposed by the currently available six-tool connector interface. Do not falsely mark the visibility issue fixed.

## Latest live verification - 2026-10-25 19:40 local
- The currently connected ChatGPT tool catalog now exposes the extended `mcp__Raya_MCP_Local__*` tools, including `get_config`, `read_multiple_files`, `list_directory`, `read_url`, and the format-adapter tools; this is no longer the earlier six-tool-only view.
- Called `workspace_roots` successfully. It returned the five configured roots listed above.
- Called `get_config` successfully. It returned Node `v22.23.2`, settings/limits, and capabilities `processSessions=true`, `progressiveSearch=true`, `formatAdapters=true`, `arbitraryPowerShell=true`; the response explicitly excluded secrets. The shell is not sandboxed, so use PowerShell cautiously.
- Called `read_multiple_files` successfully for this diagnosis document and `package.json`; both file contents were returned, total 5,708 bytes. This verifies an advanced tool end-to-end, not just catalog visibility.
- Current conclusion: the tool visibility and invocation blocker observed on 2026-10-23 is resolved in this live session. This verification confirms the tools invoked above, not that every one of the 45 tools has been individually exercised.
- No tunnel restart, allowlist change, secret change, or change to the separate IranPriceScout tunnel was made.

## Follow-up validation - 2026-10-25 19:45 local
- Live catalog introspection confirms 45 `mcp__Raya_MCP_Local__*` tool declarations.
- `read_file_tail` returned the expected `tail` field and actual final lines from the diagnosis file. `run_check(npm_test)` returned exit code 0 with 29 core assertions, 34 format/config checks, and successful HTTP auth checks.
- Direct `npm run lint` returned exit code 0; `lint` is an alias for syntax checks (`npm run check`), not a separate lint engine.
- `run_check(npm_lint)` twice failed with connector-layer `MCP server is not initialized` despite an intervening successful `workspace_roots` call. This is an intermittent MCP lifecycle error, not a reported lint/test failure.
- Loopback health endpoints responded successfully. `/health/mcp` reported `status=ok`, child `running`, initialize successful with protocol `2025-11-25`, but `tools_list.ok=false` and `retained_count=0`. The currently visible 45-tool catalog and successful tool calls indicate this field reflects whether forwarded `tools/list` traffic has been observed, not whether the catalog is currently absent.
- Live `read_url` succeeded against `https://example.com/` with HTTP 200 and 577 bytes of HTML. This verifies one public HTTPS success path only.
- Started a disposable managed `ping.exe` session. `kill_process` correctly refused because Raya owns the managed session; `force_terminate` then accepted the managed session ID, and a separate process check confirmed the PID had exited. This verifies managed termination and the ownership guard, but not the positive path for terminating a non-managed OS process.
- Do not restart the healthy tunnel or touch the IranPriceScout tunnel just to force the observation field green. The intermittent initialization issue and tunnel discovery observation remain documented for non-disruptive follow-up.

## Follow-up live verification - 2026-10-25 19:45 local
- Re-ran `npm test` using the absolute `npm.cmd` path: all three test suites passed. Core smoke: 45 tools, 29 checks; format smoke: 45 tools, 34 checks; HTTP smoke: unauthorized requests blocked and authenticated initialize returned 200 with `text/event-stream`.
- `npm run check` passed (syntax checks).
- Live `/health/mcp` still reports `initialize.ok=true`, child running, `evidence=same_child`, but `tools_list.ok=false`, `retained_count=0`, `complete=false`. `/api/status` reports the `main` stdio channel enabled with probe status `ok`; tunnel-client remains running.
- Current metrics include two successful `tools/list` HTTP responses, but the health snapshot still retains no tool names. HTTP status alone does not establish that the tunneled JSON-RPC result contained or retained the tool list.
- Old diagnostic logs include a failed attempt to start a second local HTTP server because port 18767 was already in use; this is not evidence that the active stdio server failed. Historical Cloudflare edge connectivity errors are dated and do not establish the current tunnel's state.
- No source code, credentials, allowlists, or running tunnel processes were changed.

## Latest live verification - 2026-10-26 23:58 local
- The active ChatGPT catalog exposes 45 `mcp__Raya_MCP_Local__*` tool declarations (counted from the current callable tool registry), not six.
- End-to-end calls succeeded for `workspace_roots`, `get_config`, `get_usage_stats`, and `read_multiple_files`. The configured five roots match the expected allowlist; `get_config` returned configuration and limits without secrets.
- Ran `npm run check` and `npm test` through the absolute `npm.cmd` path. Both returned exit code 0. Core smoke: 45 tools and 30 checks; format/config smoke: 45 tools and 34 checks; HTTP smoke blocked unauthorized requests and authenticated initialize returned 200; URL security smoke passed all 3 redirect/SSRF checks.
- Current conclusion: the earlier ChatGPT catalog visibility blocker is resolved in this session. The 45-tool catalog is confirmed and several tools were invoked successfully; not every tool has been manually exercised individually.
- No source code, credentials, allowlists, or tunnel processes were changed. The separate IranPriceScout tunnel was not touched. The remaining issue is the discrepancy between successful local tests (45 tools) and the active tunnel's empty discovery cache; further fix requires inspecting the actual tunneled `tools/list` response/dispatcher trace or triggering a supported fresh discovery operation.

## Latest verification - 2026-10-26 23:55 local
- Live `ALL_TOOLS` catalog now contains 45 callable `mcp__Raya_MCP_Local__*` declarations (not six).
- Successfully invoked `get_config` and received the supported settings/capabilities without secrets; successfully invoked `read_multiple_files` for this document and `package.json`; successfully invoked `list_directory` against the bridge root (63 entries, not truncated). These are direct end-to-end calls through the connected ChatGPT tool surface.
- Re-ran the full test command with the absolute `C:\\Program Files\\nodejs\\npm.cmd` path. Exit code 0. Core smoke: 45 tools, 30 checks all passed. Format/config smoke: 45 tools, 34 checks passed. HTTP smoke: unauthorized access blocked, authenticated initialize returned 200 and `text/event-stream`. URL security smoke: 3 checks passed (private redirect blocked, public redirect revalidated, hop limit enforced).
- Current conclusion: the earlier tool visibility issue is resolved in the current live session; 45 tools are declared and representative advanced tools execute successfully. The local test suite also passes, including URL redirect security checks. Not every tool has been manually exercised end-to-end.
- No tunnel restart, root/allowlist change, credential change, or IranPriceScout tunnel change was made in this verification.

## Latest verification - 2026-10-26 23:58 local
- Confirmed the current callable registry exposes 45 `mcp__Raya_MCP_Local__*` tools. Direct calls to `workspace_roots`, `get_config`, `get_usage_stats`, and `read_multiple_files` succeeded.
- Ran `npm run check` and the full `npm test` suite through the absolute `npm.cmd` path; both returned exit code 0. Core smoke passed 30 checks, format/config smoke passed 34 checks, HTTP auth/initialize checks passed, and URL security smoke passed 3 redirect/SSRF checks.
- Current conclusion: the ChatGPT-side tool visibility issue is resolved in this live session. The 45-tool catalog and representative tool calls are verified; not every tool has been manually exercised individually.
- No source, credentials, roots/allowlist, or tunnel process was changed. The separate IranPriceScout tunnel was not touched.

## Live ChatGPT connector and search-session verification — 2026-10-30
- The current callable ChatGPT registry exposes 45 `mcp__Raya_MCP_Local__*` declarations. The live `get_config` response confirms Windows / Node v22.23.2, six configured roots, `maxSearches=8`, 15-minute search-session expiry behavior, process sessions and format adapters enabled, and arbitrary PowerShell execution disabled.
- A live filename search for the newly added native race test returned the expected path. The response included an `expiresAt` timestamp 15 minutes after creation. `stop_search` returned `removed:true`; after cleanup, `list_searches` returned an empty list. The six content-search sessions created during this inspection were also explicitly stopped; final live search list is empty.
- This confirms representative end-to-end operation of the connected progressive-search lifecycle. It does not mean all 45 tools have been individually exercised.
- Initial parallel diagnostics produced `MCP server is not initialized`. Sequential retries and subsequent individual calls succeeded. Continue using sequential local MCP calls; root cause of the concurrency/lifecycle error is not established.
- `tunnel-client` was visible in the Windows process inventory (PID 58724), but public tunnel connectivity/health was not independently probed in this continuation. No tunnel restart was performed.
- One pre-existing managed build session remains `running` with `timedOut:true` (PID 22608, `build-handle-relative.cmd`, started 2026-10-30 08:44:22Z). Its last output stopped during MSVC linking. It was not terminated because the prior handoff records a locked build artifact and explicitly requires preserving the uncertain process/artifact; do not start another build to the same output name or kill it without renewed identification/approval.
- `local-mcp-bridge` is not a Git repository (`git_status` exit 128); there is no branch, commit, PR, or remote handoff for this workspace. Documentation and source changes are local files.

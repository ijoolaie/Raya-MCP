# Raya MCP Local Bridge — Tool Audit
**Audit date:** 2026-10-26  
**Scope:** `C:\Users\Ali\local-mcp-bridge` only.  
**Rule:** Catalog presence is not treated as proof of operation. Status distinguishes live calls, automated test evidence, and conditional or intentionally untested behavior.

## Summary
- Local protocol/catalog: **45 tool definitions**.
- Latest regression run: **PASS** — `npm test` exit 0; core suite 30 checks, format/config suite 34 checks, HTTP authentication checks passed, and 3 deterministic URL redirect tests passed.
- Latest syntax check: **PASS** — `npm run lint` exit 0 (alias for `npm run check`, not a dedicated linter).
- Live bridge connection: **PASS** — `get_config` and `workspace_roots` respond after reconnect.
- Process/session fixes: **PASS** — managed stdin round-trip; controlled termination ends as `terminated`; positive `kill_process` test against a disposable unmanaged Node child succeeded and the PID was absent on follow-up process enumeration.
- Live format/URL evidence: see tool rows; controlled fixtures were kept under `raya-tool-test-temp`.
- No other project was edited. The separate IranPriceScout tunnel on port 8765 was not touched.

## Per-tool status (all 45)

| # | Tool | Status | Evidence / limitation |
|---:|---|---|---|
| 1 | `workspace_roots` | LIVE PASS | Returned the five configured roots. |
| 2 | `workspace_list` | LIVE PASS | Enumerated bridge workspace entries. |
| 3 | `file_read` | LIVE PASS | Prior live UTF-8 read; boundary/outside-root rejection is also covered by automated tests. |
| 4 | `file_write` | LIVE PASS | Prior live fixture writes; overwrite protection is covered by tests. |
| 5 | `run_check` | LIVE PASS | `npm_test` and `npm_lint` returned exit code 0 in this audit. |
| 6 | `powershell_execute` | CONDITIONAL | Historical live success documented on 2026-10-23; current test confirms the feature gate blocks execution when `MCP_ENABLE_POWERSHELL` is disabled. Not enabled for this audit. PowerShell is not sandboxed when enabled. |
| 7 | `get_config` | LIVE PASS | Returned settings and security flags; no secrets included. |
| 8 | `set_config_value` | LIVE PASS | Set `sessions.maxSessions=32` to its existing value; response said persisted and subsequent config read remained 32. No effective setting change. |
| 9 | `get_usage_stats` | LIVE PASS | Returned bounded runtime counters. |
| 10 | `get_recent_tool_calls` | LIVE PASS | Returned call metadata without command contents or secrets. |
| 11 | `create_directory` | LIVE PASS | Prior live controlled-directory creation. |
| 12 | `get_file_info` | LIVE PASS | Prior live metadata query. |
| 13 | `read_multiple_files` | LIVE PASS | Prior live multi-file read. |
| 14 | `list_directory` | LIVE PASS | Enumerated 12 entries in the controlled fixture directory. |
| 15 | `move_file` | LIVE PASS | Prior live move/rename test in controlled workspace. |
| 16 | `edit_block` | LIVE PASS | Prior live preview and exact-match apply. |
| 17 | `start_process` | LIVE PASS | Started disposable Node processes with returned session IDs/PIDs. |
| 18 | `read_process_output` | LIVE PASS | Read initial and subsequent output, including echoed stdin. |
| 19 | `interact_with_process` | LIVE PASS | Sent `RAYA-STDIN-ENDCHECK`; child echoed it and exited 0. |
| 20 | `list_sessions` | LIVE PASS | Prior live session listing. |
| 21 | `force_terminate` | LIVE PASS | Disposable managed process ended with status `terminated`. |
| 22 | `list_processes` | LIVE PASS | Returned 341 visible process entries; follow-up verified disposable PID 87932 was absent. |
| 23 | `kill_process` | LIVE PASS | Terminated only disposable unmanaged Node PID 87932 with exact name confirmation; PID absent on follow-up. |
| 24 | `start_search` | LIVE PASS, LIMIT OBSERVED | Live searches created sessions and returned bounded results. Broad scans can hit safety/time caps; narrow roots and filters. |
| 25 | `get_more_search_results` | LIVE PASS | Prior live content search pagination returned the next page (100 initial results plus 50 next-page results). Fresh one-result search had no next page, as expected for its capped result set. |
| 26 | `list_searches` | LIVE PASS | Prior live session listing. |
| 27 | `stop_search` | LIVE PASS | Removed fresh test search session; prior stale-session cleanup also returned `removed=true`. |
| 28 | `get_prompts` | LIVE PASS | Returned built-in workflow prompts. |
| 29 | `give_feedback_to_desktop_commander` | AUTOMATED TEST ONLY | Automated smoke test verifies feedback remains local. No live feedback note was submitted to avoid adding unnecessary user-visible feedback. |
| 30 | `read_file_tail` | LIVE PASS | Returned `tail` field with the last line(s) from a controlled diagnostic text file. |
| 31 | `excel_read_workbook` | LIVE PASS | Read fixture workbook/sheet and cell A1. |
| 32 | `excel_write_cells` | LIVE PASS | Prior live XLSX fixture write; overwrite guard is tested. |
| 33 | `docx_read` | LIVE PASS | Extracted fixture paragraphs. |
| 34 | `docx_create` | LIVE PASS | Created controlled DOCX fixture. |
| 35 | `pdf_extract_text` | LIVE PASS | Extracted text from controlled PDF fixtures, including merged output. |
| 36 | `pdf_info` | LIVE PASS | Returned metadata and expected page counts. |
| 37 | `pdf_create` | LIVE PASS | Created a controlled one-page PDF fixture. |
| 38 | `excel_read_range` | LIVE PASS | Read a bounded worksheet range from fixture workbook. |
| 39 | `excel_search_workbook` | LIVE PASS | Found a matching fixture cell. |
| 40 | `docx_search` | LIVE PASS | Found expected text in controlled DOCX fixture. |
| 41 | `pdf_merge` | LIVE PASS | Merged two fixture PDFs; output page count and text verified. |
| 42 | `docx_replace_text` | LIVE PASS | Replaced one exact occurrence and verified readback. Cross-run text replacement is unsupported. |
| 43 | `excel_set_formula` | LIVE PASS | Prior live formula write; automated tests cover the restricted formula/security guard. |
| 44 | `pdf_add_text` | LIVE PASS | Added confirmed overlay to a fixture PDF and verified extracted text. |
| 45 | `read_url` | LIVE PASS, LIMITED | Public HTTPS `https://example.com/` returned HTTP 200. SSRF/HTTP-scheme guards pass automated tests; this is not exhaustive redirect acceptance. |

## Security edge tests — 2026-10-26 live
- Outside-root `C:\\Windows\\win.ini`: blocked with `Path is outside the configured workspace allowlist.`
- Windows device path `\\\\?\\C:\\Windows\\win.ini`: blocked by the allowlist.
- UNC path `\\\\localhost\\C$\\Windows\\win.ini`: blocked by the allowlist.
- Dot-dot traversal from the bridge root to Windows: blocked with `Path is outside the configured workspace allowlist.`
- Reparse-point escape: created a temporary junction inside `raya-tool-test-temp` pointing to `C:\\Windows`; reading `escape-link\\win.ini` was blocked with `Path resolves outside the configured workspace allowlist.` The junction and its temporary directory were then removed successfully.
- URL guards: `http://example.com/` was rejected because only HTTPS is allowed; `https://127.0.0.1/` was rejected as a non-public IP.
- Positive `kill_process` path: spawned a disposable unmanaged Node child (PID 87932), terminated it with `expectedName=node` and explicit confirmation, then verified the PID was absent. No unrelated process was targeted.
- Interactive process: stdin round-trip echoed `RAYA-STDIN-ENDCHECK` and the process exited with code 0.
- One fourth sequential path-traversal probe hit the known intermittent `MCP server is not initialized` connector error; a `workspace_roots` recovery call followed by retry produced the expected allowlist rejection. This remains a connector reliability observation, not a path-security bypass.
- Size boundary: writing exactly 262,144 UTF-8 bytes succeeded and metadata reported 262,144 bytes; a 262,145-character payload was rejected by schema validation. The exact-size fixture was removed afterward.
- Process timeout: a disposable long-running Node child with a 1,000 ms timeout ended as `timed_out` with `timedOut=true`.
- Process output cap: a disposable child wrote 300,000 characters; retained output was 131,094 characters, consisting of the 128 KiB retained-output cap plus the documented 22-character truncation marker. The child exited 0.
- Invalid regex: a filename search with regex pattern `[` was rejected with `Invalid regular expression.`
- Redirect guard probe: requesting a redirect endpoint intended to target `https://127.0.0.1/` was blocked with the non-public-IP rejection before content was returned. A second public redirect endpoint returned HTTP 402 even for a redirect to `https://example.com/`. The available responses do not establish whether the first hostname resolution or redirect target triggered the guard, so redirect revalidation remains pending a usable endpoint/network path.
- Search behavior: broad scans can hit the built-in scan/time cap and return `truncated=true`; the session was stopped after the pagination probe.

## Filesystem TOCTOU / reparse-point disposition — 2026-10-26
- Source review confirms `checkedPath` (`server.mjs`) and `securePath`/`noSymlinkAncestors` (`advanced-tools.mjs`) validate canonical paths and block static symlink/junction escapes. However, the subsequent filesystem actions use ordinary path-based Node APIs (`stat`, `readFile`, `writeFile`, `rename`, and related operations) after validation.
- Residual risk: a hostile local process with write access to a workspace parent could theoretically replace a directory/reparse point between validation and the actual operation. Rechecking `lstat`/`realpath` would narrow the timing window but would not provide a race-free guarantee; no superficial patch was applied.
- Operational mitigation: treat configured workspace roots and their parent chain as trusted; restrict write access to root ancestors; keep roots narrow; do not run the bridge under a privileged account when unnecessary.
- Stronger closure requires a Windows-specific native handle-relative implementation plus adversarial race tests. Until that work is implemented and verified, **TOCTOU resistance is not accepted as proven**. Static junction escape denial and atomic no-overwrite creation are separate controls and must not be represented as race protection.
- No production code changes were made during this investigation. Current validation: `npm run lint` exit 0 and `npm test` exit 0 (30 core checks, 34 format/config checks, HTTP auth checks, 3 URL redirect tests).

## Parity-gap disposition
- **Keep implemented / verified:** all 45 catalog tools, with per-tool evidence in the table above.
- **Documented exclusion (do not build just for nominal parity):** desktop UI-only preview/open-in-folder actions and a general binary/media viewer; these require client UI capabilities not exposed by this bridge.
- **Defer with explicit limits:** legacy XLS/XLSM; DOCX edits preserving styles/relationships or matching across runs; arbitrary PDF object/content editing and embedded Persian/Arabic fonts. Existing narrow XLSX/DOCX/PDF operations remain in scope.
- **Compose rather than add dedicated APIs:** SSH/database/dev-server workflows can use managed terminal sessions; do not add dedicated APIs unless a concrete workflow requires them.
- **Security acceptance still open:** adversarial filesystem TOCTOU race tests and a Windows handle-relative mitigation; DNS-rebinding behavior beyond current URL regression tests; and deeper timeout/output stress tests. Existing size-boundary, timeout/output-cap, invalid-regex, static junction escape, concurrent no-overwrite, and mocked redirect tests have passed. Do not weaken protections to make a test pass.

## Findings and known limitations
1. **No broad claim of perfect parity.** The above verifies implemented tools along tested paths, not every input, race, malformed file, timeout, or OS-specific edge case.
2. **PowerShell is high-risk when enabled.** Runtime configuration reports `shellIsSandboxed=false`; only enable for trusted use and never assume workspace-root validation sandboxes arbitrary commands.
3. **Feedback tool:** only automated local-only behavior was verified; live mutation was intentionally skipped.
4. **Search:** a broad scan may hit the built-in scan/time cap and return `truncated=true`; narrow roots, depth, and extension filters for reliable results.
5. **Format limitations:** DOCX replacement is limited to individual text runs; legacy XLS/XLSM and complex-script PDF font rendering are unsupported/limited as documented in README.
6. **Connector transient errors:** previous `Session terminated` / `MCP server is not initialized` incidents were intermittent. Current sequential calls succeeded; root cause is not proven fixed.
7. **Tunnel health:** loopback `/health` is live/ready. The separate observed `tools_list.ok=false` field has previously reflected lack of observed forwarded `tools/list` traffic; do not treat it alone as proof that the current 45-tool catalog is unavailable.

## Fixed continuation path (do not repeat completed phases)
1. **Next:** review `docs/DESKTOP_COMMANDER_PARITY.md` and turn each remaining documented parity gap into one of: implement, explicitly exclude, or defer with reason.
2. Add targeted automated tests for security edge cases not yet evidenced: symlink/reparse-point escape behavior on this host, malformed/UNC/device paths, file-size boundaries, timeout/output caps, and URL redirect/DNS-rebinding cases.
3. Review dependency advisories and runtime permissions without running `npm audit fix --force`; the isolated security validation and active-tree audit previously reported zero vulnerabilities after dependency pins, but repeat the audit if dependencies change.
4. Only after a code change: run `npm run check`, `npm test`, and the directly affected live acceptance. Record output and update this report.
5. Do not restart a healthy tunnel just to chase an unproven health-field discrepancy. Never touch the independent IranPriceScout tunnel or other project roots.

## Latest continuation — URL redirect regression coverage (2026-10-26)
- Added `url-security-test.mjs` and wired it into `npm run check` and `npm test`; preserved the pre-change package manifest at `backup-security-20261026\\package.json.before-url-tests.json`.
- Three deterministic mocked-HTTPS tests pass without relying on external endpoints: private redirect rejected before a second request; public redirect target revalidated and followed; redirect hop limit enforced.
- `npm run lint`: exit 0. `npm test`: exit 0 — 30 core checks (including concurrent-create race handling), 34 format/config checks, HTTP auth checks, and 3 URL redirect tests.
- Active-tree `npm audit --json`: exit 0, 0 vulnerabilities (301 total dependency entries: 292 prod, 10 optional).
- No production URL-fetch code changed in this step; the change adds regression coverage around existing redirect behavior.
- Added a core smoke assertion for two simultaneous no-overwrite writes to the same new path. It passed: exactly one writer succeeded, the other received a file-state error, and the final file contained one complete writer payload.
- Post-change validation rerun: `npm run lint` exit 0; `npm test` exit 0, including the concurrent-create assertion and all 3 URL redirect tests.
- Connector reliability remains intermittent: two calls immediately after the source edit returned `MCP server is not initialized`; a later `get_config` recovered the session and the checks passed. No tunnel restart was performed.

**Next fixed step:** design a Windows handle-relative filesystem operation layer and adversarial race tests before claiming TOCTOU closure. Do not repeat the general inventory or restart a healthy tunnel.

**Current checkpoint:** all 45 tools have a status row; live checks include positive `kill_process`, junction escape denial, UNC/device/dot-dot path denial, exact write-size boundary, timeout/output caps, invalid-regex rejection, and HTTPS/loopback URL guards. Regression checks passed again after acceptance. One search session created by this audit was stopped; a separate active search with pattern `(staging|deploy|bundle|archive|wrangler|index\\.js(\\.map)?)` was left untouched because it was not created by this audit and may belong to other work. Next work is a design/implementation task for Windows handle-relative filesystem operations and adversarial TOCTOU tests, plus any remaining DNS-rebinding coverage—not another general health check.

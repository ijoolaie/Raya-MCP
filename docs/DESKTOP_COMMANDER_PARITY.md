# Desktop Commander MCP â€” Detailed Parity Specification

Reviewed for planning on 2026-10-23. References: https://github.com/wonderwhy-er/DesktopCommanderMCP ; https://github.com/wonderwhy-er/DesktopCommanderMCP/blob/main/FAQ.md ; https://desktopcommander.app/mcp/ . Upstream changes; recheck schemas before implementation.

Status: planned = not implemented; implemented = code exists but lacks adequate tests; tested = automated checks passed; verified = automated and manual acceptance passed; not applicable = excluded with rationale. This is a plan, not evidence of parity.

## Original baseline inventory (six-tool snapshot, 2026-10-23; superseded by the live 45-tool catalog below)
| Tool | Status | Current scope/gap |
|---|---|---|
| workspace_roots | implemented | Lists roots |
| workspace_list | implemented | One directory; no recursive/depth options |
| file_read | implemented | UTF-8, 256 KiB; no multi-file/URL mode |
| file_write | implemented | UTF-8, 256 KiB; overwrite opt-in |
| run_check | implemented; Windows npm command path fixed | Preset checks; npm presets invoke npm.cmd through cmd.exe on Windows; no persistent session |
| powershell_execute | implemented; live smoke verified | Windows-only arbitrary PowerShell, max 16 KiB, 1â€“120 s, not sandboxed |

## Current live catalog and verification (2026-10-25)

The connected ChatGPT catalog exposes 45 tools. In the latest live session, calls to `workspace_roots`, `workspace_list`, `get_config`, `read_multiple_files`, `read_file_tail`, `get_file_info`, and `get_prompts` succeeded end-to-end; `read_url` also returned HTTP 200. `run_check(npm_test)` passed (29 core assertions, 34 format/config checks, HTTP 401/200 auth checks); direct `npm run lint` passed, where `lint` is an alias for syntax checks rather than a separate lint engine. This is not exhaustive manual acceptance of all 45 tools.

| Family | Tools | Evidence / remaining caveat |
|---|---|---|
| Workspace | `workspace_roots`, `workspace_list` | Five configured roots; live roots call passed |
| Basic / advanced filesystem | `file_read`, `file_write`, `read_multiple_files`, `read_file_tail`, `get_file_info`, `create_directory`, `list_directory`, `move_file`, `edit_block` | `read_multiple_files` and `read_file_tail` live-verified; core automated tests passed |
| Process/session | `start_process`, `read_process_output`, `interact_with_process`, `list_sessions`, `force_terminate`, `list_processes`, `kill_process` | Managed lifecycle tests passed; a disposable `ping.exe` session was terminated through `force_terminate` and OS-checked as exited. `kill_process` correctly refused a Raya-managed PID; positive-path termination of a non-managed disposable process remains untested |
| Checks / config | `run_check`, `powershell_execute`, `get_config`, `set_config_value` | `run_check(npm_test)` passed; direct `npm run lint` passed; `run_check(npm_lint)` had transient connector initialization errors in this session; PowerShell is privileged, not sandboxed |
| Search | `start_search`, `get_more_search_results`, `list_searches`, `stop_search` | Pagination and explicit cleanup tested; sessions now expire after 15 minutes and the oldest snapshot is evicted when the 8-session cap is reached. Capacity/eviction regression test added; live re-verification pending |
| Diagnostics | `get_usage_stats`, `get_recent_tool_calls`, `get_prompts`, `give_feedback_to_desktop_commander` | Implemented with bounded metadata/local feedback |
| URL | `read_url` | SSRF protections tested; one live public-HTTPS fetch (`https://example.com/`) returned HTTP 200; redirect/SSRF edge cases still need broader acceptance |
| XLSX | `excel_read_workbook`, `excel_write_cells`, `excel_read_range`, `excel_search_workbook`, `excel_set_formula` | Format/config tests pass; XLS/XLSM unsupported |
| DOCX | `docx_read`, `docx_create`, `docx_search`, `docx_replace_text` | Format/config tests pass; cross-run and style-preserving edits unsupported |
| PDF | `pdf_extract_text`, `pdf_info`, `pdf_create`, `pdf_merge`, `pdf_add_text` | Format/config tests pass; arbitrary PDF object editing and complex-script font embedding unsupported |

Tunnel `/health/mcp` currently reports successful initialize and a running child but `tools_list.ok=false` / `retained_count=0`. The connected 45-tool catalog and successful live calls show this health field is observation of forwarded `tools/list` traffic, not proof that the catalog is missing tools. Intermittent `MCP server is not initialized` errors remain a connector lifecycle concern. No tunnel restart or unrelated tunnel change was made.

## Full upstream capability inventory and acceptance criteria

### Configuration and diagnostics
- get_config: report supported settings (shell, blocked commands, allowed directories, read/write limits, telemetry). Schema/version visible; redact secrets.
- set_config_value: allowlisted keys, strict type validation, explicit persistence semantics, rollback on failure; reject invalid updates.
- get_usage_stats: bounded local usage/performance statistics; no secrets/content; retention documented.
- get_recent_tool_calls: paginated recent metadata/output previews; redact tokens and sensitive output; bounded retention/rotation.
- get_prompts: workflow templates if available in selected distribution; otherwise mark not applicable.
- give_feedback_to_desktop_commander: vendor-specific flow; never send externally without user consent.

### Terminal and process/session management
- start_process: launch shell/command with cwd, timeout, initial output and background continuation; return stable MCP session ID, OS PID if applicable, state and timestamps.
- read_process_output: incremental/paginated output using offset/length; bounded output and explicit running/finished/timed-out state.
- interact_with_process: send stdin to managed process/REPL; cap input; optional prompt wait; report if exited.
- force_terminate: stop a managed terminal session; verify ownership, clean children, idempotent result; never kill by command name.
- list_sessions: list managed sessions with status/runtime/cwd/PID/last output/waiting state; omit secrets.
- list_processes: inspect accessible OS processes, CPU/memory where available; represent permission-denied metrics honestly.
- kill_process: terminate a specific process only after identity checks; protect against PID reuse; report OS errors.
- Background/streaming execution: process must outlive initial tool return; handle permits later reads and cancellation.
- In-memory execution/data analysis: optional Python/Node/R workflow without script files; do not claim isolation unless implemented.

### Filesystem
- read_file: text and optional URL/media modes in upstream variants; encoding, size, binary detection, URL scheme/host allowlist, timeout, no credential URL leaks.
- read_multiple_files: multiple paths per call, per-file and aggregate limits, individual errors, root enforcement.
- write_file: create/replace with explicit overwrite, parent policy, size cap, atomic write where possible.
- create_directory: create hierarchy under roots; clear idempotency behavior.
- list_directory: directory entries and optional recursive depth; pagination/entry cap and context-safe output.
- move_file: move/rename; both source and destination scoped; no accidental overwrite; document cross-volume failures.
- get_file_info: metadata (type, size, timestamps, attributes), no content disclosure.
- Tail/negative offsets: efficient end-of-file log reading; document offset units and caps.
- write_pdf: create/modify PDF; validate paths, bound input/pages, preserve original unless authorized, verify output.
- Excel: format-aware XLSX/XLS/XLSM read/write/edit/search where supported; test sheets/ranges, formulas and round trips.
- DOCX: format-aware read/create/edit/search and surgical XML; test styles/relationships, round trips and malformed files.
- Images/media: binary-aware operations, not UTF-8 text corruption; document supported formats.

### Surgical edits
- edit_block: exact-string SEARCH/REPLACE. Fail on zero matches; reject ambiguous matches unless expected count provided; preserve encoding/newlines; provide diff/backup.
- Multi-file/pattern edits: preview scope and count first; explicit paths and counts; dry run and rollback; never broad replacement silently.

### Progressive search
- start_search: filename/content search across approved roots, often ripgrep-backed; search type/pattern/roots/filters/timeout/session ID/first page; protect against regex/path abuse.
- get_more_search_results: stable cursor/session, bounded pages, no duplicate/missing results.
- list_searches: active sessions/status/age and safe query summary; resource caps.
- stop_search: cancel/release resources; confirm cleanup; repeated calls safe.
- Lifecycle: concurrency cap, automatic expiry, error state, binary/large-file handling and bounded result sizes.

### Workflow/transport capabilities
- SSH/database/dev server: compose from persistent terminal sessions; not necessarily dedicated API.
- Cross-project work: multiple roots, but every file operation enforces its own scope.
- URL retrieval: optional only with scheme/host/time/size controls; never SSRF or local-file bypass.
- Local/remote MCP: stdio and authenticated HTTP/tunnel, verified separately.
- UI-only previews/open-in-folder: mark not applicable where ChatGPT cannot expose the UI; document an alternative.
- Telemetry: do not copy blindly; opt-in only with documented privacy model.

## Security boundaries
PowerShell and shell are privileged, not sandboxed; cwd validation is not confinement. Protect against symlink/reparse-point escapes and races. Never log credentials/environment maps. Use stable session IDs and ownership checks. HTTP must authenticate requests, bind locally by default and never expose a public listener directly. Bound output, runtime, concurrency and storage.

## Implementation order
1. Safety tests, canonical paths/reparse points, structured errors, redacted logs and limits.
2. Process/session manager: start/read/interact/list/terminate.
3. Filesystem: multi-read, metadata, create/list/move, edit blocks.
4. Search sessions: pagination/cancel/list/expiry/concurrency caps.
5. Validated configuration, recent calls and usage stats.
6. PDF/DOCX/Excel adapters and data analysis.
7. Final parity audit with automated matrix, manual acceptance and explicit exclusions.

## Acceptance checklist
- [ ] Every upstream tool has a status/decision.
- [ ] Every implemented tool has schema, result contract, errors, limits and tests.
- [ ] Session tests cover completion, long-running/interactive jobs, pagination, timeout, cancel and restart.
- [ ] File tests cover traversal, reparse points, size, overwrite, encoding, binary and races.
- [ ] Search tests cover pagination, cancel, expiry, concurrency, huge trees and invalid patterns.
- [ ] Document tests cover malformed input, limits, preservation and round trips.
- [ ] Logs/config/history redact secrets and retention is bounded.
- [ ] Tunnel/stdio integration has live smoke test.
- [ ] No unrelated service, including IranPriceScout tunnel, was changed.
- [ ] README and handoff reflect actual tested state.


## Implemented status update (2026-10-23)
Additional implemented families:
- Runtime config: get_config, set_config_value (allowlisted numeric settings persisted to raya-config.json).
- Process/session: start_process, read_process_output, interact_with_process, list_sessions, force_terminate, list_processes, kill_process.
- Filesystem: read_multiple_files, read_file_tail, get_file_info, create_directory, list_directory, move_file, edit_block.
- Progressive search: start_search, get_more_search_results, list_searches, stop_search.
- Diagnostics: get_usage_stats, get_recent_tool_calls, get_prompts, give_feedback_to_desktop_commander (local-only note, no external send).
- URL: read_url with HTTPS-only policy, public-address validation, DNS pinning, redirect validation, timeout and byte limits.
- Formats: excel_read_workbook, excel_read_range, excel_search_workbook, excel_write_cells (.xlsx only); docx_read/docx_search/docx_create; pdf_extract_text/pdf_info/pdf_create/pdf_merge.

Verified tests from the last complete test run before the latest additions: core smoke 22 checks; format/config smoke 19 checks; HTTP smoke unauthorized request 401 and authenticated initialize 200. Latest added range/search/merge/URL hardening changes have syntax checks but require a new full npm test run before they can be called verified.

Intentional limitations / not yet implemented:
- DOCX edits that preserve original styles, relationships, comments, tracked changes, and embedded objects.
- XLS/XLSM support and formula authoring.
- Arbitrary PDF content editing and Persian/Arabic PDF font embedding.
- General image/media viewer, desktop preview/open-in-folder UI, and dedicated SSH/database APIs.
- URL fetch is not a browser login/session and must not be used for authenticated pages.


## Latest final verification â€” 2026-10-23
- npm run check passed (exit 0).
- npm test passed (exit 0): 45 tools listed, 25 core smoke checks, 34 format/config checks, HTTP auth 401/200 checks.
- XLSX range/search, DOCX search, PDF merge, and URL DNS-pinning changes are now covered by smoke tests or syntax checks; details in HANDOFF.md.
- npm install reported two moderate vulnerabilities and deprecated transitive packages; audit details were unavailable from the registry response. Review dependencies before production deployment.



## Latest format parity additions — 2026-10-23
- Added docx_replace_text for exact matches within a single Word text run, excel_set_formula with a restricted formula allowlist, and pdf_add_text for confirmed overlays. These are implemented and covered by smoke tests.
- Latest full validation: npm run check and npm test passed; 45 tools, 25 core checks, 34 format/config checks; HTTP auth 401/200.
- Remaining exclusions: DOCX cross-run text edits, legacy XLS and macro-enabled XLSM, general PDF object editing and complex-script font embedding, client UI-specific preview/open-in-folder actions.

## Connector-level audit - 2026-10-23
- Local stdio MCP protocol test: `initialize` + `tools/list` returned 45 tool definitions.
- Connected ChatGPT catalog: only six tools are exposed, so local implementation is broader than the live connector schema.
- Live checks through the connected tools: workspace roots/list, read/write, PowerShell execution, and predefined `git_status` all returned responses. An outside-root read was rejected and a second write without overwrite permission was rejected.
- One rapid follow-up call returned `MCP server is not initialized`; retrying calls individually worked. This is an observed transient, not enough evidence to determine whether it originates in the connector lifecycle or tunnel.
- `run_check git_status` against the bridge directory returned exit code 128 because the bridge folder is not a Git repository; the same check against an allowed Git workspace returned exit code 0. No repository files were modified by that check.
- No positive `kill_process` test was run because that would terminate an OS process. The implementation requires an explicit confirmation and a matching live process name; this destructive path still needs a controlled isolated-process acceptance test.
- Do not claim 45 tools are available through ChatGPT until connector declarations are updated and a newly added tool is successfully called from the connected catalog.

## Expanded test results - 2026-10-23
- Latest core smoke: 29 assertions passed, including Windows process enumeration, PID identity-mismatch safety, local-only feedback, and PowerShell feature gate.
- Latest format/config smoke: 34 checks passed. Latest HTTP smoke: unauthenticated request blocked with 401; authenticated initialize returned 200.
- Remaining acceptance gaps: positive `kill_process` against a disposable process, successful public-HTTPS `read_url` fetch, symlink/reparse-point edge cases under concurrent mutation, and end-to-end exposure of newly added tools through ChatGPT connector.

## Live acceptance update — 2026-10-26
- Connected catalog exposes 45 tools. `get_usage_stats`, `get_recent_tool_calls`, `get_config`, `get_prompts`, `list_sessions`, and `list_directory` were live-called successfully. Recent-call metadata did not expose command contents or secrets.
- Live XLSX reads passed for workbook, bounded range, and cell search against the controlled `raya-tool-test-temp/sample.xlsx` fixture (A1=`test`). Live PDF metadata and text extraction passed against `sample.pdf` (one page; extracted `Tool Test` / `hello world`). These were read-only operations.
- Three stale search sessions discovered during diagnostics were stopped; each returned `removed=true`. The tool-usage snapshot after 106 calls reported zero active process sessions and three active searches before that cleanup.
- A batched format-test request encountered the known intermittent `MCP server is not initialized` connector error. After a sequential `workspace_roots` recovery, individual format calls passed. The connector issue remains unresolved.
- `kill_process` positive-path testing and reparse-point race tests remain open. No config/root/tunnel changes were made; IranPriceScout was untouched.
- Additional live DOCX acceptance passed using a new controlled fixture: create, read, search, exact single-run replacement, and readback verified `ORCHID` → `VIOLET`.
- Additional live PDF acceptance passed using controlled fixtures: create, merge two PDFs into two pages, inspect page count, add an explicitly confirmed text overlay, and extract the resulting two-page text including `LIVE ACCEPTANCE`.
- These write tests touched only new fixtures under `raya-tool-test-temp`; they did not modify existing project files.

## Current authoritative verification — 2026-10-28 16:32 UTC
This section supersedes older status statements above where dates/results differ.
- Fresh `run_check(npm_test)` exited 0. Core smoke: 33 checks; format/config smoke: 34 checks; HTTP auth: unauthorized request blocked and authenticated initialize HTTP 200 (`text/event-stream`); URL security suite: 36 checks, including deterministic DNS timeout enforcement, IPv4/IPv6 special-use ranges, IPv4 boundary allow/block cases, redirect revalidation and hop cap.
- Fresh `run_check(npm_lint)` exited 0. The `lint` script delegates to `check`, which runs `node --check`; this is syntax validation, not comprehensive static analysis.
- Local tests exercise 45 tool definitions. This does not by itself mean each of the 45 has been exhaustively manually accepted through the connected runtime.
- Search lifecycle code locally includes 15-minute expiry metadata, pruning, bounded capacity and oldest-session eviction after a successful scan. However, the connected `list_searches` previously returned eight older sessions without `expiresAt`; the patch is not accepted for the live runtime until an owner-controlled, observable reload and live lifecycle acceptance are completed.
- Latest live local health snapshot previously returned HTTP 200 and an initialized/running child, but `tools_list.ok=false`, `retained_count=0`, `complete=false`; this observer state is not evidence that the visible connector catalog lacks tools. Cloudflare stderr still contained QUIC/DNS timeouts as of the last inspected log timestamp, so public end-to-end connectivity is unverified.
- Remaining acceptance gaps include a controlled positive-path `kill_process` test, reparse-point/TOCTOU race acceptance, exhaustive special-use IP canonicalization review, broader manual acceptance of all 45 tools, and safe live activation of the search lifecycle patch. Code inspection confirms canonical-path validation occurs separately from later filesystem operations, so resistance to concurrent symlink/reparse-point mutation remains unproven; this is an open test gap, not a confirmed exploit. Do not restart the owner-managed tunnel, clear legacy sessions, or touch the separate IranPriceScout tunnel without an observable recovery path.
- Follow-up on 2026-10-28: `privateIp()` explicitly rejects IPv6 discard-only `2001:100::/48`; tests cover compressed/expanded discard-only and IPv4-mapped loopback forms. The IPv4 classifier was narrowed to avoid blocking all of `192.0.0.0/16`, with a positive test for `192.0.1.1`. The latest fresh `npm_test` reports 36 URL security checks and exited 0; `npm_lint` exited 0 (Node syntax checks only). New boundary tests allow `172.15.255.255`, `172.32.0.0`, and `100.128.0.1`, and block `172.16.0.0`, `172.31.255.255`, and `100.127.255.255`. This targeted coverage does not close the broader IPv6 canonicalization/CIDR or filesystem reparse-point/TOCTOU audit, and it does not establish live-runtime activation.

## Filesystem junction regression — 2026-10-29 07:15 UTC
- Added and ran a stable Windows directory-junction escape regression in `smoke-test.mjs`. A junction from the disposable workspace fixture to a temporary directory outside the configured roots was rejected by `file_read`, `edit_block`, and `move_file` (three assertions passed).
- Latest fresh `npm_test` on 2026-10-29 08:16 UTC exited 0: 42 core smoke checks, 34 format/config checks, HTTP authentication checks, and 36 URL-security checks.
- Stable-junction regression covers `file_read`, `edit_block`, `move_file`, `get_file_info`, `list_directory`, `read_multiple_files`, `file_write`, and `create_directory`, plus an assertion that the outside target's contents remain unchanged. Junction creation succeeded, so the tests ran rather than being skipped.
- Fresh `npm_lint` on 2026-10-29 08:16 UTC exited 0; `lint` delegates to `node --check` syntax validation only, not comprehensive static analysis.
- Scope limit: this confirms rejection when the junction exists before path validation, not concurrent replacement (TOCTOU). Source review found separate path-based validation and later filesystem calls; another path check alone cannot make that sequence atomic. A robust Windows fix needs handle/reparse-point-aware operations. No filesystem implementation changed.
- Native prototype review on 2026-10-29: prior isolated synchronized swap runs are documented in the design note, but the harness is not ready for unattended CI. On 2026-10-29, cleanup was hardened to track/terminate/await both worker processes, explicitly record each worker's `close` event instead of relying on `proc.closed`, verify junction targets before removal, restore renamed fixture directories, and report cleanup failures. `package.json` syntax checks now include both native probe scripts; `npm_lint` passed after the change. On 2026-10-29, the native harness passed normal synchronized descendant and root-chain swaps. Controlled failure-path tests also passed their cleanup expectations: injected assertion failure (expected exit 1), suppressed synchronization signal with 250 ms timeout (expected exit 1), and simulated junction-creation denial (swap skipped after restoring the renamed directory, then root-chain race passed). No new fixture directories remained after these runs; three older fixtures were left untouched because provenance/targets were not fully verified. A subsequent normal harness run exited 0. Fresh `npm_lint` and `npm_test` exited 0 (42 core, 34 format/config, HTTP auth, 36 URL-security checks). This is scoped harness evidence only; production filesystem handlers are unchanged and TOCTOU remains open. Do not promote the harness to unattended CI until broader operation-family tests pass.


## HANDLE-relative filesystem acceptance status — 2026-11-02

The upstream parity row move_file remains implemented in the Production bridge but not accepted against concurrent path/reparse races. A separate native rename prototype under .native-probe/handle-relative-rename-probe.c is test-only and has passed:
- 15 functional/adversarial checks (same/distinct parent, no-overwrite collision, missing/unsafe/reparse source, directory rejection, cross-volume rejection, injected pre/post-commit failure, source/destination parent namespace swaps, HANDLE accounting).
- Five synchronized worker rounds, 20 workers and 120 assertions against concurrent source/destination path replacement by outside junctions.
- Live disposable-fixture ACL checks for source DELETE and destination FILE_ADD_FILE denial; both operations return STATUS_ACCESS_DENIED and succeed after fixture ACL restoration.

This does not change the Production move_file status to verified. Existing-file atomic edit remains unsupported/fail-closed in the test-only mutation contract. Metadata FILE_READ_ATTRIBUTES denial remains unresolved; strict hard-link policy is pending user decision; provider portability is untested. Production integration remains NO-GO.


## Directory move candidate — 2026-11-02

The Production move_file tool is still not accepted against filesystem races. A separate directory-capable HANDLE-relative native candidate passed 12 local NTFS checks for empty/non-empty directory rename, no-overwrite collision, reparse rejection, source DELETE and destination FILE_ADD_SUBDIRECTORY ACL denial, held-parent namespace swaps, cross-volume rejection and HANDLE cleanup. The test is registered as npm run test:native-rename-directories. Multi-worker directory stress and provider portability are not yet tested; no Production handler has been migrated.


## Directory rename worker stress — 2026-11-02

The isolated directory-capable native rename candidate passed five synchronized stress rounds (20 independent workers, 180 assertions) in which both source and destination parent paths were replaced by outside junctions after workers acquired their HANDLEs. Each operation completed in the original held destination directory with nested canary content intact and outside sentinels unchanged. Test command: npm run test:native-rename-directory-workers. This remains local NTFS prototype evidence; Production move_file is unchanged and not accepted.


## Native rename cancellation boundary — 2026-11-02

The isolated synchronous rename candidate passed four cancellation-boundary checks via npm run test:native-rename-cancellation. Pre-cancel prevents mutation; cancellation queued on the Node event loop cannot interrupt the synchronous native call; committed results and committed_but_cleanup_failed state are preserved. This does not establish in-kernel cancellation or Production move_file acceptance.

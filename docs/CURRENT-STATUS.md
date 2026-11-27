# Current Project Status — Raya Local MCP Bridge

Last updated: 2026-11-27

## Mission
Build a secure, reliable Windows local MCP bridge with practical Desktop Commander-like capabilities.

## Repository and security status — 2026-11-27
- Private GitHub repository: https://github.com/ijoolaie/Raya-MCP; default branch `main`; initial source push verified. See HANDOFF.md for the import manifest and exclusions.
- Local baseline checks passed: `npm run lint`; `npm test` (42 core, 34 format/config, HTTP auth/initialize, 36 URL-security checks). The catalog/test inventory reports 45 tools; this does not mean every tool has independent acceptance coverage.
- The default-branch `package.json` now carries the verified dependency overrides `archiver@8.0.0`, `unzipper@0.12.5`, and `minimatch@11.0.1` (with `uuid@11.1.1` retained). PR #8 was merged on 2026-11-26; its Windows CI completed successfully, including the dependency-audit step. The current Dependabot alert #1 state has not been independently queried, so do not claim it is closed until GitHub Security confirms it. See `docs/DEPENDENCY-SECURITY-20261101.md`.
- A local change in `.native-probe/hardlink-sharing-experiment.c` remains uncommitted and preserved; `to-iman-hand-off.md` is ignored and unpushed.

## Overall status
<badge>Active development</badge> Phase 1 — filesystem confinement is the active phase. Production filesystem confinement is **NOT ACCEPTED as race-free**.

## Phase status
| Phase | Status | Notes |
|---|---|---|
| 0 — Baseline and architecture | Implemented; ongoing | Workspace, tool families, threat/acceptance gaps and phase policy documented |
| 1 — Filesystem confinement | In progress / Not accepted | Held-parent read, exclusive zero-byte create, identity readback, finalizer, close-fault and selected cleanup tests pass in isolated prototype; parent/leaf race and operation-family gates remain |
| 2 — Safe filesystem operations | Planned | Depends on accepted filesystem primitives and operation-family semantics |
| 3 — Process lifecycle | Planned | Includes safe launch, output, timeout, cancellation and termination |
| 4 — Developer/workspace capabilities | Planned | Project, Git, test and diagnostic workflows |
| 5 — MCP management/observability | Planned | Schemas, roots, sessions, structured errors, client compatibility |
| 6 — Integrated security/reliability | Planned | Adversarial cross-tool and resource/lifecycle acceptance |
| 7 — Setup/UX/compatibility | Planned | Installation and clean-client acceptance |
| 8 — Release/maintenance | Planned | Release candidate and maintenance controls |

## Latest verified evidence (2026-10-30)
- Native isolated build-and-test script completed successfully (exit 0); MSVC compiled the prototype.
- Native harness reported `ok: true`, including parser/root-chain rejection, repeated handle-count checks, bounded-read cap, traversal/missing/type rejection, static junction checks, held-parent child read after rename/outside-junction replacement, child reparse rejection, synchronized descendant swap staying in-root, and configured-root-chain swap failing closed.
- `npm_lint` passed (exit 0); this is Node syntax validation via `node --check`, not comprehensive static analysis.
- Fresh `npm_test` passed (exit 0): 42 core checks, 34 format/config checks, HTTP unauthorized/authenticated initialization checks, and 36 URL-security checks.
- Parent external-finalizer fallback is now independently verified in a disposable child process using Node `--expose-gc`: one transferred HANDLE increased the process count by exactly one, explicit GC triggered finalization, and the count returned exactly to baseline.
- Latest 2026-10-30 native rerun (MSVC x64, exit 0) additionally passed N-API wrapper mutation/deletion and copied-object non-alias tests; Windows leaf-name rejection tests for trailing dot/space, CON/PRN/AUX/NUL, CONIN$/CONOUT$, COM/LPT numeric and superscript forms, and forbidden punctuation.
- Native close handling now routes all direct close operations through a consume-once helper; explicit owner close, finalizer, and root-chain intermediate transition have injected post-close failure coverage. The finalizer test is verified in a disposable `--expose-gc` child. Cleanup-path close failures remain best-effort and aggregate-counted; the injector does not model every kernel failure mode.
- Fresh `npm_lint` and `npm_test` passed after these changes. Full npm test: 42 core, 34 format/config, HTTP auth 401/200, and 36 URL-security checks.

## Latest work — 2026-10-30
- Added isolated `openParentRelative`, `inspectParentHandle`, and idempotent `closeParentHandle` APIs; native ownership is now bound to the returned JS object with `napi_wrap` and finalized when the object is collected.
- Added and exercised `readChildFromParent` against the held parent after renaming the directory and replacing its former path with an outside junction. The helper returned the in-root marker (`PARENT-IDENTITY`), not the outside marker.
- Invalid child components, missing child, read-cap rejection, directory child, and child junction/reparse rejection passed. The child-junction case was actually created and rejected (not skipped).
- Repeated successful and failing child reads showed stable process handle counts. Explicit close, double-close, inspection-after-close rejection, and 300 invalid/missing/trailing-path failures also passed with stable handle counts.
- Latest native MSVC build and adversarial harness exited 0; outcomes include the parent-relative read, child reparse rejection, existing descendant race and configured-root-chain race tests.
- Fresh `npm_lint` exited 0. Fresh `npm_test` exited 0: 42 core checks, 34 format/config checks, HTTP unauthorized/authenticated initialization checks, and 36 URL-security checks.
- Added a deterministic child-process finalizer test to the native harness. Under `--expose-gc`, the transferred parent HANDLE raised process handle count from 164 to 165; after explicit GC the N-API external finalizer returned it to 164 on the first collection cycle. Explicit close/double-close remains separately tested.
- Reviewed the harness failure path and moved the finalizer subprocess inside the fixture's outer `try/finally`, so a finalizer-test failure also triggers worker reaping and fixture cleanup. The timeout path kills and awaits the child `close` event before failing.
- Rebuilt and reran the native harness after this cleanup correction; exit 0. Re-ran `npm_lint` and `npm_test`; both exit 0. No create/write API or production integration was added.

## Latest work — FILE_CREATE acceptance expansion (2026-10-30)

- Added an eight-independent-process barrier test against one shared leaf. Exactly one FILE_CREATE succeeded and seven received collisions; the created file was zero bytes.
- Added 30 repeated create/collision pairs and verified the current process HANDLE count returned to its pre-loop baseline.
- Added Unicode BMP/supplementary-plane filename creation; ADS colon, `::$DATA`, named-stream `:$DATA`, and control-character leaf names were rejected. An ASCII case-only collision was rejected without replacing the original file. Composed `é` and decomposed `e + combining acute` were distinct files on this NTFS test volume; this is recorded as observed filesystem behavior, not a normalization guarantee.
- Added injected post-create close-status failure. The API throws and increments the close-failure counter, no HANDLE remains leaked, and the already-created zero-byte file remains. This is an explicit commit/outcome ambiguity, not rollback.
- The concurrency harness now uses filesystem ready/go/report barriers and runs with --expose-gc; explicit GC stabilizes child-process HANDLE accounting before and after the race.
- Native MSVC x64 build plus complete native harness exited 0. npm_lint and npm_test exited 0: 42 core checks, 34 format/config checks, HTTP auth/initialize checks, and 36 URL-security checks.
- Added `inspectChildFromParent`: new-child FILE_ID_INFO, volume serial, size and regular/non-reparse metadata are read from a handle-relative child open. The test confirms nonzero 128-bit file ID, same volume as held parent, stable repeated identity, and rejection of missing/traversal/ADS names after parent rename and old-path junction replacement. Node bigint `fs.stat().ino` independently matched the low 64 bits of FILE_ID_INFO on this volume.
- The previous locked build artifact was left untouched. A separately named acceptance-probe addon was built and tested successfully; no uncertain fixture directory was accessed.

## Known blockers and limitations
1. createChildExclusive is an isolated zero-byte create-only prototype; no content-write, overwrite, directory-create, rename, delete, or production integration exists.
2. Native prototype is not integrated with production handlers.
3. Production handlers still perform path validation separately from later path-based filesystem operations; concurrent reparse-point replacement is not ruled out.
4. New-child FILE_ID_INFO and volume-serial readback now pass through a held parent HANDLE, including regular-file/type checks, stable repeated identity, same-volume comparison, nonzero file ID, and missing/traversal/ADS rejection. Broader operation-family race matrices remain outstanding.
5. Therefore production filesystem confinement remains **NOT ACCEPTED as race-free**.
6. The connected ChatGPT catalog currently exposes 45 tools and representative progressive-search lifecycle calls succeed; public tunnel connectivity/health remains unverified. A pre-existing timed-out native build session (PID 22608) remains running and was preserved.
7. Native close failures now pass through a single consume-once helper and increment an observable failure counter; explicit close, finalizer, and root-chain transition failure injection are tested. Cleanup-path close failures remain best-effort and observable only through the aggregate counter; the fault injector simulates a post-close failure status, not every possible kernel/handle-table failure mode.

## Immediate next action

The isolated read-only adapter now matches Node for ordinary files/directories and the known mount-point/symlink reparse tags, including size, timestamps, readonly, and root/parent replacement schedules. NTSTATUS categories are implemented in the test-only native probe; actual missing-leaf, missing-parent, missing-root and directory-as-file failures now assert `not_found`/`is_directory`, and one-shot native access-denied injection verifies `access_denied` plus HANDLE cleanup without changing ACLs. Live FILE_CREATE collision, invalid leaf-name, and injected close-failure categories now also pass. Next, resolve rootId-to-configured-ROOTS binding and request-scoped HANDLE lifecycle; then expand operation-family race/transaction acceptance and Production-equivalent integration. Live ACL permission-denied remains open. Keep all addons/adapters test-only; do not modify Production handlers or add a path-based runtime fallback. Production integration remains NO-GO until the complete Phase 1 acceptance gate passes.

## Binding execution and documentation policy
See PROJECT-ROADMAP.md and AGENTS.md. In short: deliver each phase as a distinct unit; work pragmatically without lowering acceptance standards; run comprehensive adversarial phase-end tests; continuously update CURRENT-STATUS.md, PROJECT-ROADMAP.md, HANDOFF.md and relevant technical docs; transfer a precise handoff before the next phase. Blocked tests remain blocked, not passed.

## Synchronized held-parent FILE_CREATE race — 2026-10-30

Added a separate isolated acceptance pair under `.native-probe`: `handle-relative-create-race-test.cjs` and `handle-relative-create-race-worker.cjs`. The worker opens the configured in-root parent directory and signals the harness only after the parent HANDLE has been acquired. The harness then renames that directory, installs an outside-root junction at its former path, and releases the worker to call the existing `createChildExclusive` primitive through the held HANDLE.

Verified on the current Windows/Node 22 x64 host:
- The synchronized race exited 0. The zero-byte file was created only in the renamed original directory; it was absent from the outside junction target.
- Both in-root and outside sentinel files retained their original bytes.
- The worker's own HANDLE count increased by exactly one for the parent HANDLE and returned to its pre-open baseline after explicit close.
- A second synchronized run injected a worker failure after parent acquisition and after the same parent-path replacement. The worker exited nonzero as expected, its `finally` closed the held parent HANDLE, and no outside file was created.
- The standalone test's temporary fixture was removed in its `finally` path.

The first draft of this new test incorrectly compared the parent harness's process HANDLE count across spawned-child stdio lifetime and failed (165 vs 175); this was test-accounting noise, not evidence of a native leak. The test was corrected to measure HANDLE ownership inside each worker process, after its stdio stream was initialized. The corrected isolated test passed with both workers returning to their own baselines.

The new files are included in `npm run lint` syntax checking. This is additional evidence for the isolated zero-byte FILE_CREATE primitive only. It does not cover a concurrent leaf-name replacement, content writes, overwrite/publish, directory creation, enumeration/search, metadata, move/rename, or production-handler integration. Phase 1 remains IN PROGRESS / NOT ACCEPTED; production filesystem confinement remains NOT ACCEPTED as race-free.

Exact next action: add a synchronized leaf replacement/collision schedule and cleanup-fault coverage, then expand the isolated operation-family matrix. Keep production handlers and dependencies unchanged.

## Live ChatGPT connector and search-session verification — 2026-10-30
- The current callable ChatGPT registry exposes 45 `mcp__Raya_MCP_Local__*` declarations. The live `get_config` response confirms Windows / Node v22.23.2, six configured roots, `maxSearches=8`, 15-minute search-session expiry behavior, process sessions and format adapters enabled, and arbitrary PowerShell execution disabled.
- A live filename search for the newly added native race test returned the expected path. The response included an `expiresAt` timestamp 15 minutes after creation. `stop_search` returned `removed:true`; after cleanup, `list_searches` returned an empty list. The six content-search sessions created during this inspection were also explicitly stopped; final live search list is empty.
- This confirms representative end-to-end operation of the connected progressive-search lifecycle. It does not mean all 45 tools have been individually exercised.
- Initial parallel diagnostics produced `MCP server is not initialized`. Sequential retries and subsequent individual calls succeeded. Continue using sequential local MCP calls; root cause of the concurrency/lifecycle error is not established.
- `tunnel-client` was visible in the Windows process inventory (PID 58724), but public tunnel connectivity/health was not independently probed in this continuation. No tunnel restart was performed.
- One pre-existing managed build session remains `running` with `timedOut:true` (PID 22608, `build-handle-relative.cmd`, started 2026-10-30 08:44:22Z). Its last output stopped during MSVC linking. It was not terminated because the prior handoff records a locked build artifact and explicitly requires preserving the uncertain process/artifact; do not start another build to the same output name or kill it without renewed identification/approval.
- `local-mcp-bridge` is not a Git repository (`git_status` exit 128); there is no branch, commit, PR, or remote handoff for this workspace. Documentation and source changes are local files.

## Synchronized leaf-race acceptance expansion — 2026-10-30

Extended the isolated standalone harness `.native-probe\handle-relative-create-race-test.cjs` and worker with two additional deterministic schedules. Each worker acquires the parent directory HANDLE and signals readiness before the harness mutates the leaf name and releases it:

- **Regular-file leaf insertion:** the harness writes a temporary sentinel file and atomically renames it to the requested leaf after parent-HANDLE acquisition. `createChildExclusive` returns the expected FILE_CREATE collision; the installed leaf bytes remain exactly `LEAF-CANARY-MUST-SURVIVE`, and no outside file appears.
- **Leaf junction insertion:** the harness inserts an outside-root junction at the requested leaf after parent-HANDLE acquisition. `createChildExclusive` fails on collision without following the junction; no file appears in the outside target and the outside sentinel remains unchanged.

Both worker reports assert that acquiring the parent adds exactly one HANDLE and explicit close returns the worker's process HANDLE count to its pre-open baseline. Worker timeout/error paths now terminate and reap the worker before fixture cleanup; the fixture cleanup verifies expected junction targets before removing them. The standalone harness exited 0 with four outcomes: parent-path swap, injected post-open failure cleanup, regular-file leaf collision, and leaf-junction collision. No temporary fixture remained.

The first implementation of the test's parent-process HANDLE accounting was intentionally rejected after it counted Node child/stdio handles (not the native parent HANDLE); final assertions are made inside each worker process and pass. Fresh `npm_lint` exited 0 (including syntax checks for both new scripts). Fresh `npm_test` exited 0: 42 core checks, 34 format/config checks, HTTP auth/initialize 401/200, and 36 URL-security checks. Note: `npm_test` does not execute this standalone native test; it was run separately against the existing acceptance addon. No native C source, production handler, dependency, runtime setting, service, or tunnel was changed.

This remains isolated evidence for zero-byte `FILE_CREATE` only. It does not establish content-write, overwrite/publish, directory-create, enumeration/search, metadata, or move/rename confinement. Phase 1 remains IN PROGRESS / NOT ACCEPTED as race-free.

Exact next action: broaden the isolated operation-family race matrix, beginning with read/multi-read/tail and directory creation; add cleanup/fault schedules for each family. Keep all production handlers unchanged until the complete Phase 1 gate is accepted.

## Synchronized held-parent child-read race — 2026-10-30

Extended the same isolated worker/harness to exercise `readChildFromParent` under synchronized leaf mutation:

- **In-root leaf insertion:** after the worker acquires its parent HANDLE, the harness atomically renames a file containing `READ-IN-ROOT-CANARY` into the requested leaf. The native helper returns exactly those bytes and the expected size; the worker's parent HANDLE count returns to baseline after close.
- **Outside leaf junction insertion:** after parent-HANDLE acquisition, the harness inserts a junction at the requested leaf pointing to an outside directory containing `READ-OUTSIDE-SECRET`. The native helper rejects the reparse point; the worker receives no outside bytes, the outside canary remains unchanged, and its HANDLE count returns to baseline.

The standalone harness now exits 0 with six outcome groups: parent-path replacement for FILE_CREATE; injected post-open worker failure cleanup; regular-file leaf collision; leaf-junction collision; handle-relative read of an inserted in-root leaf; and rejection of a leaf junction during handle-relative read. All worker-local HANDLE accounting passes and no temporary fixture remains. Fresh `npm_lint` and `npm_test` both exited 0: 42 core checks, 34 format/config checks, HTTP auth/initialize 401/200, and 36 URL-security checks. The standalone native harness remains separate from `npm_test` and uses the existing acceptance addon; no native C source or production handler was changed.

This adds evidence for the isolated single-child read helper only. It does not verify production `file_read`, multi-read, tail, directory enumeration, or search under concurrent replacement. Phase 1 remains IN PROGRESS / NOT ACCEPTED.

Exact next action: extend handle-relative read coverage to multi-read/tail semantics and add a handle-relative directory-creation primitive/test; then continue enumeration/search, metadata, write/edit, and move/rename families with cleanup/fault schedules. Keep production handlers unchanged.

## Isolated handle-relative directory creation — 2026-10-30

Added a new isolated native API, `createDirectoryChildExclusive(parent, childName)`, in the separate source copy `.native-probe\handle-relative-probe-phase1-dircreate.c`. It reuses the validated single-leaf policy and held parent HANDLE, calls `NtCreateFile` with `FILE_CREATE`, `FILE_DIRECTORY_FILE`, `FILE_OPEN_REPARSE_POINT`, and `OBJ_DONT_REPARSE`, verifies the created object is a directory and not a reparse point, and closes the returned HANDLE through the consume-once helper. The original native C source and production handlers were not changed.

A separate MSVC x64 build script `build-raya-phase1-dircreate.cmd` produced `build\Release\napi_probe_raya_phase1_dircreate.node` (164,352 bytes; build exit 0). This unique source/object/library/addon naming avoids the old timed-out build's locked artifact. The standalone test `.native-probe\handle-relative-dircreate-race-test.cjs` exited 0 with four outcome groups:
- Parent directory renamed and old path replaced with an outside junction after the worker acquired its HANDLE; the new directory was created only beneath the renamed original parent.
- Existing directory collision returned the expected failure and preserved its marker file.
- Outside-root junction inserted at the new directory's leaf caused a collision without following or changing the outside target.
- Injected post-create close-status failure was surfaced and counted; the directory remained committed (ambiguous outcome), and the worker's parent HANDLE returned to baseline.

Every worker report verified exactly one additional HANDLE while the parent was open and return to the pre-open count after close. Fresh `npm_lint` exited 0 and includes syntax checks for the new test/worker. Fresh `npm_test` exited 0: 42 core checks, 34 format/config checks, HTTP auth/initialize 401/200, and 36 URL-security checks. The native directory test is deliberately separate from `npm_test`; run it with `node --expose-gc handle-relative-dircreate-race-test.cjs` from `.native-probe` after building with `build-raya-phase1-dircreate.cmd`. A convenience script `npm run test:native-dircreate-race` is available from the project root once the addon has been built. The existing file create/read harness is available as `npm run test:native-create-race`.

This is still an isolated prototype and does not establish production mkdir safety or integration. It does not cover multi-component recursive directory creation, directory-handle transfer/descendant operations, concurrent enumeration, or cross-operation cleanup. Phase 1 remains IN PROGRESS / NOT ACCEPTED as race-free.

Exact next action: implement and test safe handle-relative directory open/descendant traversal and enumeration primitives, then bind production-equivalent list/search and metadata behavior to held handles. Continue write/edit and move/rename only after their own same-handle and failure-cleanup designs are independently tested. Keep production handlers unchanged.

## Native race command verification — 2026-10-30

Verified both root-level convenience scripts after adding them to `package.json`:
- `npm run test:native-create-race`: exit 0; six outcomes covering parent/leaf create races, child-read leaf insertion/reparse rejection, and worker cleanup.
- `npm run test:native-dircreate-race`: exit 0; four outcomes covering parent-path replacement, existing-directory collision, leaf-junction collision, and post-create close-status ambiguity.

Both scripts use `--expose-gc` and run separate standalone harnesses. Neither is included in the default `npm_test` chain; their syntax is included in `npm_lint`. The directory-create test requires the separate addon built by `.native-probe\build-raya-phase1-dircreate.cmd`. Both test runs exited 0 and their fixture directories were cleaned.

## Isolated bounded tail-read and multi-read family — 2026-10-30

Added a separate native source copy `.native-probe\handle-relative-probe-phase1-tail.c` and unique MSVC x64 build script `.native-probe\build-raya-phase1-tail.cmd`. The new `readTailChildFromParent(parent, childName, tailBytes)` opens exactly one safe leaf relative to the held parent HANDLE, rejects reparse points/directories, caps the returned tail to 1 MiB, seeks to `fileSize - min(fileSize, tailBytes)`, reads the exact bounded byte count, and returns `content`, total `size`, `offset`, and `bytesRead`. The tail open uses `FILE_SHARE_READ` only (denying concurrent write/delete sharing while the handle is held) and verifies the file size remains stable across the read. Child HANDLE ownership is consumed immediately after the read and before N-API result construction.

Added isolated worker/harness `.native-probe\handle-relative-read-family-worker.cjs` / `handle-relative-read-family-test.cjs`. It covers:
- A file inserted only after the worker acquired its parent HANDLE; bounded tail read returns exact final five bytes, total file size, offset, and byte count.
- An outside-root junction inserted at the tail leaf after parent-HANDLE acquisition; tail read rejects it and does not expose outside bytes.
- A multi-read wrapper that invokes the native handle-relative child-read primitive sequentially for two files plus one missing file; per-file success/error results remain isolated.
- A multi-read wrapper with an outside-root junction in the middle item; only that item errors and no outside bytes are returned.

The native multi-read behavior is currently an isolated JavaScript wrapper over the single-child native primitive, not a native bulk API and not production `file_read_multiple` integration. The test explicitly re-baselines HANDLE accounting after the worker's stdin synchronization because Node lazily initializes a pipe HANDLE on that first synchronous read; operation HANDLE count is checked against that synchronized baseline, then the held parent HANDLE is checked separately. This avoids a false-positive one-HANDLE delta while still asserting child and parent ownership cleanup.

Unique MSVC x64 build exited 0. Standalone `npm run test:native-read-family-race` exited 0 with four outcome groups. Fresh `npm_lint` and `npm_test` both exited 0: 42 core checks, 34 format/config checks, authenticated HTTP initialize 200 / unauthorized blocked, and 36 URL-security checks. The standalone native test is separate from default `npm_test`; its test/worker syntax is included in `npm_lint`. No production C source, handler, dependency, runtime setting, service, tunnel, or credential was changed.

Limitations: tail stability is protected against ordinary concurrent writers/deleters by the restrictive share mode and checked by stable size; this does not claim a fully transactional snapshot against kernel/filesystem-level mutation. Multi-read is sequential and single-component only. Neither operation is wired into production. Phase 1 remains IN PROGRESS / NOT ACCEPTED as race-free.

Exact next action: implement and test safe handle-relative directory open/descendant traversal and enumeration primitives, then bind production-equivalent list/search and metadata behavior to held handles. Continue write/edit and move/rename only after their own same-handle and failure-cleanup designs are independently tested. Keep production handlers unchanged.

## Handle-relative directory traversal and bounded enumeration prototype — 2026-10-30

A new isolated native source/build target was added:
- `.native-probe/handle-relative-probe-phase1-dirwalk.c`
- `.native-probe/build-raya-phase1-dirwalk.cmd`
- output: `.native-probe/build/Release/napi_probe_raya_phase1_dirwalk.node`

New experimental exports:
- `openDirectoryChild(parentHandle, childName)`: opens exactly one validated leaf component relative to a held directory HANDLE using `NtCreateFile`, `RootDirectory`, `OBJ_DONT_REPARSE`, `FILE_OPEN_REPARSE_POINT`, and `FILE_DIRECTORY_FILE`. The returned JS external owns the new HANDLE and uses the existing explicit-close/finalizer lifecycle.
- `enumerateDirectoryPage(directoryHandle, maxEntries)`: requests a bounded 64-KiB `FileIdBothDirectoryInfo` page from the held directory HANDLE, validates record offsets and UTF-16 filename lengths, and returns names, file IDs, attributes, directory flags, and reparse flags. It does not open or follow entries. The API currently exposes only one page per invocation; continuation, restart, complete-directory semantics, and behavior on directories exceeding the page buffer are not implemented/accepted.

Build evidence: direct MSVC x64 build with the matching Node 22 headers completed with exit code 0 and no compiler warnings after correcting a signedness warning. The addon is isolated and is not imported by production code.

Verification limitation: attempts to create the standalone runtime test harness were blocked by the local safety layer. Therefore no runtime test, synchronized intermediate-component replacement test, enumeration-content assertion, reparse-entry assertion, or HANDLE-count test has been performed for these two new exports. Treat them as COMPILED / NOT TESTED. Do not infer runtime correctness from compilation alone. Existing read-family and directory-create test results remain separate and do not cover these exports.

Next required gate: add and run an approved standalone harness for ordinary nested directory opens, intermediate-component junction replacement, bounded enumeration, reparse-entry reporting without traversal, invalid/closed handle behavior, and HANDLE cleanup; then test pagination/continuation semantics before using enumeration for production list/search. Keep production handlers and dependencies unchanged. Phase 1 remains IN PROGRESS / NOT ACCEPTED.

## Directory traversal/enumeration runtime verification — 2026-10-30

The initial directory-walk prototype was reviewed and corrected before runtime acceptance. The original page loop could stop after `maxEntries` even though the OS had already advanced the directory enumeration cursor over the returned page. It now validates the entire returned record chain and fails explicitly if the page exceeds `maxEntries`, with an error stating that the current enumeration position was consumed and the caller must reopen the directory handle to restart. Record filename length is also checked against the next-record boundary.

Added persistent isolated harness `.native-probe/handle-relative-dirwalk-test.cjs` and root script `npm run test:native-dirwalk-race`; added its syntax check to `npm_lint`. Verified outcomes:
- Nested two-level child directory opens relative to held parent handles and enumerates the expected files.
- A junction inserted at the intermediate component after acquiring the parent handle is rejected; the outside fixture remains unchanged.
- Enumeration reports a junction entry with `isReparsePoint=true` and does not traverse it.
- A page exceeding `maxEntries` fails explicitly; reopening the directory handle starts a fresh enumeration and returns the complete expected set.
- Process HANDLE count returns to the synchronized baseline after success and failure paths.

Latest native build completed with MSVC x64 exit code 0 and no warnings. `npm run test:native-dirwalk-race` exited 0 with all seven checks. Fresh `npm_lint` exited 0. Fresh `npm_test` exited 0: 42 core checks, 34 format/config checks, authenticated HTTP initialize 200 / unauthorized blocked, and 36 URL-security checks.

Important limits remain: enumeration currently returns one OS-provided 64-KiB page only; no explicit continuation token or restart API exists. When a page exceeds the requested entry cap, it errors after the OS cursor has advanced; caller must close/reopen the directory handle to restart, as tested. No Production handlers were changed. Treat these as verified isolated primitives, not production integration or proof of complete directory listing/search correctness. Phase 1 remains IN PROGRESS / NOT ACCEPTED.

## Directory enumeration pagination hardening — 2026-10-30

Extended `enumerateDirectoryPage(directoryHandle, maxEntries, restart?)` with explicit same-handle restart via `FileIdBothDirectoryRestartInfo`, normal continuation via repeated `FileIdBothDirectoryInfo` calls, and explicit end-of-directory response (`done=true`, empty entries) on `ERROR_NO_MORE_FILES`. The per-page entry cap is now 1..1024; overflow still fails explicitly rather than silently dropping records. The persistent harness now creates 1,000 additional files and verifies multi-page enumeration returns all 1,002 entries exactly once, discovers a junction entry across pages without traversing it, blocks intermediate junction replacement, verifies over-cap error and same-handle restart, verifies explicit end-of-directory, and checks HANDLE count returns to baseline. Native build exit 0/no warnings; native harness exit 0 with eight checks; latest npm_lint and npm_test exit 0 (42 core, 34 format/config, HTTP 401/200, 36 URL-security). This remains isolated prototype code; no Production integration. Phase 1 remains IN PROGRESS / NOT ACCEPTED.

## 2026-10-30 final directory-enumeration pagination verification

The API now exposes `enumerateDirectoryPage(handle, maxEntries, restart?)`. A normal call continues from the directory HANDLE's current cursor; `restart=true` uses `FileIdBothDirectoryRestartInfo` to restart on that same handle; `ERROR_NO_MORE_FILES` becomes `{entries: [], pageRead: false, done: true}`. The maximum requested entry cap is 1..1024. If the OS page contains more than the requested cap, the function validates the full returned record chain and fails explicitly rather than dropping entries; the caller can restart on the same handle with a larger cap.

The persistent harness creates 1,000 additional files and verifies 1,002 entries across multiple 64-KiB pages with no omissions or duplicates. It also verifies junction metadata without traversal, a synchronized intermediate junction replacement is blocked, over-cap failure, same-handle restart, explicit end-of-directory, and HANDLE count returns to baseline. MSVC x64 build exit 0/no warnings; all four standalone native suites pass in one chained run (file create/read, directory create, tail/multi-read, directory walk); native directory-walk suite passes eight checks. Latest npm_lint and npm_test both pass (42 core, 34 format/config, HTTP 401/200, 36 URL-security).

These remain isolated native prototypes; no production handler was modified. Directory traversal/enumeration is not yet integrated with production list/search/metadata behavior. Phase 1 remains IN PROGRESS / NOT ACCEPTED.

## Handle-relative metadata primitive — 2026-10-30

Added isolated native export `inspectEntryMetadata(parentHandle, childName)`. It opens one validated leaf relative to the held parent HANDLE using `FILE_OPEN_REPARSE_POINT` and `OBJ_DONT_REPARSE`, reads `FILE_ID_INFO`, `FILE_STANDARD_INFO`, `FILE_BASIC_INFO`, and `FILE_ATTRIBUTE_TAG_INFO`, then closes the child HANDLE before constructing the JS result. It returns file ID/volume serial, size and allocation size as decimal strings, link count, directory/delete-pending flags, attributes, reparse flag, and NT 100-ns timestamp values as strings. A leaf junction is opened as the reparse object itself and reported as reparse metadata; it is not followed.

Persistent harness now additionally verifies regular-file metadata, directory metadata, and junction metadata. Latest MSVC x64 build exit 0/no warnings. Latest directory-walk suite passes ten checks; all four isolated native suites pass in one chained run. Latest npm_lint and npm_test both exit 0 (42 core, 34 format/config, HTTP 401/200, 36 URL-security). This primitive is isolated and not integrated into production metadata/list/search handlers. Phase 1 remains IN PROGRESS / NOT ACCEPTED.

## Recursive handle-relative list/search semantics — 2026-10-30

Extended the isolated harness with a recursive traversal/search wrapper built only from the new handle-relative primitives. It enumerates pages from each held directory HANDLE, obtains metadata relative to that HANDLE, opens only entries marked as directories and not reparse points, and reads bounded file contents relative to the same held parent. The wrapper uses relative names only for returned display paths; it does not use constructed paths for filesystem access. The fixture includes a nested in-root match and an outside-root match reachable only through a junction. Verified search returns only `sub/inner/nested/needle.txt`, reports the junction as metadata, and never traverses it or returns the outside-root match. It enforces test caps of depth 8, 5,000 entries, and 1 MiB per file.

The directory-walk suite now passes 11 checks. All four native suites pass in one chained run. Latest npm_lint and npm_test both pass (42 core, 34 format/config, HTTP 401/200, 36 URL-security). This is a test-only wrapper, not production `list_directory` or `search` integration. Phase 1 remains IN PROGRESS / NOT ACCEPTED.

## Resilient list/search harness failure isolation — 2026-10-30

Extended `.native-probe\\handle-relative-dirwalk-test.cjs` with a second, explicitly test-only traversal wrapper that separates per-entry failures from traversal-wide failures. It records metadata/open/read errors against the affected relative display path and continues with other entries; it does not use display paths for filesystem access. A synchronized test renames an enumerated in-root file immediately before metadata inspection: that entry produces an isolated error while the nested in-root search hit is still returned and the file is restored in cleanup. Added empty-directory coverage and explicit depth/total-entry budget assertions. The wrapper also has a per-file byte cap and a pattern-based match list; it remains a test harness, not production code.

The directory-walk harness exits 0 and now reports 15 checks, including synchronized per-entry metadata failure isolation, empty-directory traversal, depth/entry limits, rejection of enumeration on a closed directory HANDLE, and conversion of a closed-handle page failure into a directory-scoped error without leaking handles. Re-ran all four isolated native suites in one chain; each passed. Re-ran `npm test`; exit 0 with 42 core checks, 34 format/config checks, HTTP unauthorized/authorized initialize 401/200, and 36 URL-security checks. `npm_lint` also exits 0. No production handler, native production source, dependency, runtime setting, service, tunnel, or credential was changed; fixture cleanup and HANDLE baseline assertions passed.

## Search-session lifecycle and cooperative cancellation prototype — 2026-10-30

Extended the isolated handle-relative traversal harness with cooperative cancellation checks at entry boundaries. Cancellation propagates through recursive descent and returns through each directory's `finally` close path; the test asserts traversal stops at the configured check boundary and the process HANDLE count returns to baseline. Added a small in-memory search-session model in the test harness to exercise page cursor advancement, stable next-page behavior, session metadata, bounded session capacity with oldest-session eviction, explicit stop, and TTL expiry using an injected clock. These tests validate lifecycle semantics only; they do not implement or replace the production search session store.

The directory-walk suite now passes 22 checks. Added direct native API argument-validation coverage for zero and over-limit page sizes, a non-boolean restart argument, and unsafe child names; each is rejected before filesystem traversal. The previous mid-pagination held-HANDLE closure and independent `failNextDirectoryPageForTest()` schedules remain passing, each recording a directory-scoped error and returning process HANDLE count to baseline. These hooks and checks exist only in the isolated native-probe harness.

Re-ran the complete chain on 2026-10-30: all four isolated native suites passed; `npm test` exited 0 with 42 core checks, 34 format/config checks, HTTP unauthorized/authorized initialize 401/200, and 36 URL-security checks; `npm_lint` exited 0. The 22-check directory-walk harness and all new recursive behavior remain test-only. Remaining gaps: Windows ACL/permission-denied schedules and production-equivalent integration tests. No production handler or native production source was changed. Phase 1 remains IN PROGRESS / NOT ACCEPTED.

## Production filesystem access inventory — 2026-10-31

Completed a source-level inventory of filesystem operations across `server.mjs`, `advanced-tools.mjs`, and `format-tools.mjs`, with URL tools and process execution explicitly separated. The inventory maps each file operation to its current path-based access sequence, candidate native primitive, migration priority, and unresolved design requirements. Key findings: `file_write` and `move_file` have non-atomic preflight checks; `edit_block` and document writers use path-based temp-file/rename transactions; read handlers commonly separate metadata checks from reads; and process execution can independently access the host filesystem beyond bridge path confinement. The detailed matrix and recommended migration order are in `docs/PRODUCTION-FILESYSTEM-ACCESS-INVENTORY-20261031.md`. This is an inventory only: no production code or permissions changed, and no native prototype was connected. Phase 1 remains IN PROGRESS / NOT ACCEPTED.

## Production filesystem adapter contract — 2026-10-31

Created `docs/PRODUCTION-FILESYSTEM-ADAPTER-CONTRACT-20261031.md` as a design-only gate for future integration. It defines root identity, handle-relative access, reparse rejection, opaque HANDLE ownership, fail-closed behavior, bounded reads/enumeration, explicit error classes, and separate requirements for create/write/replace/move. It specifies a read-only-first migration order and production build/deployment gates. Source/prototype evidence is not treated as Production implementation. In particular, exclusive zero-byte create is not a complete write transaction, and no safe replace or handle-relative rename primitive has been accepted. No production code, package dependency, runtime configuration, or permissions changed. Phase 1 remains IN PROGRESS / NOT ACCEPTED.


## Native root HANDLE and read-only adapter harness — 2026-10-31

Added a separate test-only native source/build target:
- `.native-probe/handle-relative-probe-phase1-rootapi.c`
- `.native-probe/build-raya-phase1-rootapi.cmd`
- output: `.native-probe/build/Release/napi_probe_raya_phase1_rootapi.node`

The new `openWorkspaceRoot(rootPath)` export opens the configured root chain itself and returns the same opaque, single-owner N-API HANDLE wrapper used by the parent API. This closes the earlier primitive-level gap for files directly under a workspace root. It still accepts a path string rather than a root ID; a future JavaScript adapter must select that path exclusively from the server's configured `ROOTS` array. No caller-supplied path may become root authority.

Root-chain and relative-parent component validation in this isolated source now uses the strict Windows leaf-name validator, rejecting trailing-dot/space aliases, reserved device names (including COM/LPT variants), forbidden punctuation, and traversal components. `inspectParentHandle` now also returns size, allocation size, attributes, read-only attribute, reparse tag, and FILETIME metadata for the held directory itself.

The dedicated root API harness passed 10 checks, including root-level file read and metadata, nested handle-relative read, unsafe root/parent component rejection, root-directory rename followed by outside-junction replacement (the held root HANDLE still returned the original in-root bytes and left the outside canary unchanged), and HANDLE count returning to baseline. MSVC x64 build exited 0.

Added a separate JS-only test harness `.native-probe/handle-relative-readonly-adapter-test.cjs`. It selects a root only from a supplied configured-roots array, opens the root HANDLE, derives parent components, and performs native read/metadata operations without path-based I/O in the adapter path. It passed 9 checks: root-level/nested UTF-8 reads match Node output (including invalid UTF-8 replacement), the 256-KiB cap is enforced, directories are not read as files, root/file/directory metadata maps to the expected basic shape, ordinary-file size/creation/modification timestamps (within 1 ms) and read-only status match Node on the fixture, outside-root paths are rejected, reparse-point reads/metadata fail closed without following a junction, and all HANDLEs return to baseline.

Important compatibility limit: reparse-point metadata is deliberately rejected by this first adapter harness rather than incorrectly mapping every Windows reparse point to Node's `isSymbolicLink()`. Symlink/junction tag-to-public-output parity remains open. This is a test harness only; no MCP handler imports the addon, and no Production code, dependencies, permissions, runtime settings, or configured roots changed.

All six isolated native suites (the four existing race suites, root API, and read-only adapter comparison) were chained after the final native metadata build and exited 0. Fresh full `npm test` passed (42 core, 34 format/config, HTTP 401/200, 36 URL-security); `npm lint` passed with the new harness syntax check. Phase 1 remains IN PROGRESS / NOT ACCEPTED.

## Initial read-only adapter feasibility review — 2026-10-31 (superseded)

The initial review identified the missing root HANDLE API, incomplete root/parent component validation, and untested byte/metadata conversion. These findings drove the isolated `openWorkspaceRoot` implementation and JS-only comparison harness documented above. The primitive-level root and conversion gaps are now covered by test-only evidence; rootId-to-allowlist integration, reparse-point public-output parity, Production error mapping, ACL tests, and Production-equivalent integration remain open. No Production handler or fallback was authorized. Phase 1 remains IN PROGRESS / NOT ACCEPTED.

## Reparse-tag parity and adapter error probes — 2026-10-31

Added `.native-probe/reparse-parity-probe.cjs` and the `test:native-reparse-parity` package script. Runtime fixtures successfully created a directory junction, directory symlink, and file symlink on this host. The native metadata tags matched the Windows definitions exactly: junction `0xA0000003` (`IO_REPARSE_TAG_MOUNT_POINT`); both symlinks `0xA000000C` (`IO_REPARSE_TAG_SYMLINK`). Node `lstat().isSymbolicLink()` returned true for all three and `lstat().isDirectory()` returned false. Native metadata correctly marked all three as reparse points, but its directory attribute was true for directory junction/symlink objects.

A material parity gap was measured: native `FILE_STANDARD_INFO` reported size `0` for all three reparse objects, while Node `lstat().size` reported 66 bytes for the directory links and 77 bytes for the file symlink in this fixture. Therefore tag-based type mapping alone is insufficient to reproduce `get_file_info`; reparse metadata remains fail-closed in the adapter until size/timestamp/mode semantics are resolved without following the target. The adapter must not copy the native `isDirectory` bit into Node's public type for reparse entries.

Extended the read-only adapter harness with missing-leaf, missing-parent, and unavailable configured-root failure checks; each fails closed and returns the process HANDLE count to baseline. Permission-denied behavior is still untested because no safe ACL fixture or approved injected access-denied path is available. No ACL changes were made.

Verification on 2026-10-31: `npm lint` passed; `npm test` passed (42 core, 34 format/config, HTTP unauthorized/authorized 401/200, 36 URL-security); all six prior native suites passed individually, and the new reparse parity suite passed. The read-only adapter comparison now includes the additional missing-path/root checks. A direct `npm` executable launch through the managed-process tool returned `spawn npm ENOENT`; the predefined local lint/test checks succeeded and are the authoritative full-suite evidence. No Production handler, native production source, configured root, dependency, runtime setting, service, tunnel, or permission changed. Phase 1 remains IN PROGRESS / NOT ACCEPTED.

## Reparse metadata mapping and adapter race verification — 2026-10-31 (supersedes earlier reparse-size blocker)

Extended the isolated root API native metadata probe to read reparse data from the held reparse HANDLE with `FSCTL_GET_REPARSE_POINT`, validate the reparse header/name bounds, convert the print name to UTF-8, and derive Node-compatible logical size. `FILE_STANDARD_INFO.EndOfFile` and `GetFileInformationByHandle` both return zero for these reparse objects; the Node-compatible size is instead the UTF-8 byte length of the stored print name after removing the Windows extended-path `\\?\` prefix where present. This parsing is limited to the known mount-point and symlink tags; other reparse tags remain unsupported/fail-closed.

Added a durable parity harness for five fixture types: directory junction (`0xA0000003`), absolute directory symlink, absolute file symlink, relative file symlink, and Unicode directory symlink. All five matched Node `lstat` for `isSymbolicLink`, public type semantics (symlink, not directory), size, creation/modification/access timestamps (within 1 ms), and readonly mapping. Native directory attributes remain true for directory reparse objects; the JS adapter correctly maps recognized reparse tags to public `type: symlink` and does not reuse the native directory bit for public type.

Updated the JS-only read-only adapter harness to map only `IO_REPARSE_TAG_MOUNT_POINT` and `IO_REPARSE_TAG_SYMLINK` to Node's symlink metadata contract, using the validated native reparse logical size. Unsupported reparse tags fail closed. File reads through a reparse point still fail closed; metadata reads inspect the link object itself and do not follow its target. Junction and symlink canaries outside the root remain unchanged.

Extended adapter fault/race schedules: injected exceptions immediately after root HANDLE acquisition and parent HANDLE acquisition both release all acquired HANDLEs; a root directory rename followed by outside-junction replacement still reads the original in-root file through the held root HANDLE; a parent directory rename followed by outside-junction replacement still reads the original in-root file through the held parent HANDLE. Process HANDLE count returns to baseline after all success and failure cases.

Build and verification on 2026-10-31: root API MSVC x64 build exited 0 with no warnings. All seven isolated native harnesses passed individually (create race, directory-create race, read-family race, directory-walk race, root API, read-only adapter, reparse parity). `npm lint` passed; `npm test` passed (42 core, 34 format/config, HTTP unauthorized/authorized 401/200, 36 URL-security).

Remaining blockers: Production error-category parity is not implemented; missing-path tests prove fail-closed cleanup but do not establish exact not-found/permission distinctions. Permission-denied/ACL behavior remains untested and no ACL changes were made. Unknown reparse tags remain unsupported. The adapter still uses a test-only configured-root array and is not wired to MCP registration or Production `ROOTS` root identity. No Production handler, native production source, configured root, dependency, runtime setting, service, tunnel, or permission changed. Phase 1 remains IN PROGRESS / NOT ACCEPTED.

## Native error categories — 2026-10-31

Added a test-only NTSTATUS classification layer to `.native-probe/handle-relative-probe-phase1-rootapi.c` and exposed it as `classifyNtStatusForTest`. The classifier distinguishes `not_found`, `access_denied`, `already_exists`, `invalid_name`, `is_directory`, `not_directory`, `reparse_blocked`, `sharing_violation`, `storage_full`, and `resource_exhausted`; unknown statuses remain `io_error`. The ten native NTSTATUS open-failure formatting sites now include the stable category prefix alongside the raw NTSTATUS for diagnostics. This is still an isolated probe API, not the Production MCP error contract.

Added `.native-probe/native-error-map-test.cjs` and `test:native-error-map`; 17 status/argument cases passed. The read-only adapter harness now asserts real `not_found` categories for missing leaf, missing parent and missing configured root, and `is_directory` when a directory is passed to the file reader. These real operations also preserve HANDLE cleanup. `access_denied` now also has a one-shot native child-open fault-injection test; collision now has a live FILE_CREATE test, invalid-name has an actual unsafe-leaf test, and reparse-blocked remains a classifier-vector test. This does not prove live ACL behavior. No ACL was modified; permission-denied ACL acceptance remains open.

Latest verification after the native error classifier and parser-bound checks: isolated MSVC x64 root API build exited 0; all eight native harnesses passed (create race, directory-create race, read-family race, directory-walk race, root API, read-only adapter, reparse parity, native error map); `npm lint` passed including the new harness syntax check; `npm test` passed (42 core, 34 format/config, HTTP unauthorized/authorized initialize 401/200, 36 URL-security).

Remaining gates: real access-denied/ACL test without bypassing safety controls; live collision and other status-path mapping assertions where practical; rootId-to-configured-`ROOTS` binding; request-scoped HANDLE lifecycle; production-equivalent integration for each filesystem operation family; write/replace/move transaction semantics; and acceptance review. No Production handler, native production source, configured root, dependency, runtime setting, service, tunnel or permission changed. Phase 1 remains IN PROGRESS / NOT ACCEPTED.

## Access-denied fault injection — 2026-10-31

Extended the isolated native probe with a one-shot `failNextChildOpenStatusForTest` injector scoped to the handle-relative child-read open. The injected `STATUS_ACCESS_DENIED` follows the same native NTSTATUS error path and is emitted as `access_denied`; the read-only adapter harness confirms the error is distinct and all root/parent HANDLEs are closed afterward. This exercises the category path without changing Windows ACLs or creating a permission-denied ACE. It is deterministic fault injection, not a live ACL/authorization test.

The same adapter harness now verifies real `not_found` for missing leaf, missing parent and unavailable configured root, and real `is_directory` for a directory passed to the file reader. The native status classifier vectors cover known status families and generic fallback; the native error harness now has 18 total checks including a real collision. The access-denied case is no longer classifier-only, but real ACL behavior remains unverified.

Rebuilt the isolated root API addon with MSVC x64 (exit 0). After this change all eight native suites passed again; `npm lint` and `npm test` both passed. No Production handler, native production source, dependency, runtime setting, root allowlist, service, tunnel or ACL changed. Phase 1 remains IN PROGRESS / NOT ACCEPTED.

## Live collision, invalid-name and close-failure categories — 2026-10-31

Extended the native error harness to exercise a real `FILE_CREATE` collision against an existing file. The OS-returned NTSTATUS is surfaced as `already_exists`; the original file bytes remain unchanged and process HANDLE accounting returns to baseline. The adapter harness now exercises an actual unsafe leaf (`bad:name`) and receives `invalid_name`; missing leaf/parent/root still produce `not_found`; a directory passed to the file reader produces `is_directory`.

Normalized close failures with the `close_failure` category prefix. A deterministic `failNextCloseForTest` is armed only after the root HANDLE has been acquired, so the injected failure is consumed by the child file HANDLE close rather than a root-chain transition. The adapter observes `close_failure` and still closes the remaining parent/root HANDLEs. This models a post-close failure status; it does not model every possible kernel close failure.

The native error harness now passes 18 checks, including the real collision case. The adapter harness now covers real not-found, invalid-name, is-directory, live injected access-denied, and injected close-failure categories with HANDLE cleanup. Access-denied remains a native fault injection, not a live ACL test; no ACL changes were made.

Rebuilt the isolated root API addon with MSVC x64 (exit 0). All eight native suites passed again; `npm lint` passed; `npm test` passed (42 core, 34 format/config, HTTP 401/200, 36 URL-security). No Production handler, native production source, configured root, dependency, runtime setting, service, tunnel or permission changed. Phase 1 remains IN PROGRESS / NOT ACCEPTED.

## RootId contract prototype — 2026-10-31

Added a test-only server-owned root registry model in `.native-probe/handle-relative-readonly-adapter-test.cjs` and a standalone deterministic contract test in `.native-probe/rootid-contract-test.cjs`. The model assigns opaque IDs to configured roots; a request selects a root by ID and supplies only a relative path. It rejects unknown IDs, caller-supplied absolute/drive/UNC paths, traversal components, duplicate configured roots, and nested/overlapping roots that would make root selection ambiguous. The standalone contract test passed 12 assertions. The native read-only adapter harness was rerun after these changes and passed, including its existing HANDLE cleanup and root/parent replacement schedules. `npm lint` passed after correcting the test file to CommonJS; `npm test` passed (42 core, 34 format/config, HTTP 401/200, 36 URL-security).

This is strictly a test model: no production MCP tool schema or handler now accepts rootId, and no Production `ROOTS` mapping was changed. A material unresolved issue is configured-root identity across rename/replacement: the model retains the configured path string, but it does not pin or verify the underlying directory identity across requests. The native root opener still receives a path at request time. Root replacement acceptance therefore remains open. Request-scoped HANDLE lifecycle/concurrency isolation also remains open; the existing adapter uses synchronous try/finally and does not yet establish async request lifecycle semantics. No Production handler, native production source, dependency, configured root, runtime setting, permission, service, or tunnel changed. Phase 1 remains IN PROGRESS / NOT ACCEPTED; Production integration remains NO-GO.

Exact next action: design and test an async-safe request scope that owns one root HANDLE and its parent HANDLE chain per request, closes in reverse order on success/error/cancellation, proves concurrent requests never share handles, and rejects root identity replacement. Keep it isolated from Production until all gates pass.

## Async request-scoped HANDLE lifecycle prototype — 2026-10-31

Added isolated .native-probe/request-scope-lifecycle-test.cjs and the test:native-request-scope package script; the file is included in npm run check. Its test-only withRequestScope opens a fresh root HANDLE and any required parent HANDLEs for each invocation, holds them across an awaited callback, and closes owned handles in reverse order in finally. The callback receives a request-local frozen scope; no HANDLE or scope is stored in a shared registry.

The 12-check lifecycle harness passed twice in its final form. It verifies two overlapping requests receive distinct root and parent owner objects; completing request B closes B's owners while A's remain usable; successful completion closes A's owners; callback rejection closes the owners; cooperative cancellation observed after the callback resumes rejects with AbortError and closes owners; cancellation before acquisition opens none; and a root renamed/replaced with an outside junction during an awaited request remains bound to the already-open root HANDLE and reads the original content, not the replacement target. It checks closed owners by attempting native inspection after close. The existing read-only adapter, rootId contract harness, npm lint, and npm test also passed after this addition (42 core, 34 format/config, HTTP auth 401/200, 36 URL-security).

Important limits: cancellation is cooperative and checked at acquisition boundaries and callback completion; it cannot forcibly interrupt a callback that never settles. The prototype uses synchronous native filesystem operations inside an async JS request wrapper; it does not prove cancellation of kernel I/O or production MCP request-disconnect propagation. Root identity is pinned only after the request opens its root HANDLE; the rootId registry still does not pin/validate directory identity between requests. The harness also does not establish a clean global process HANDLE-count baseline under asynchronous runtime noise; per-owner closed/open state is directly verified instead. No Production handler/schema, native production source, configured ROOTS, dependency, service, tunnel, runtime setting, or permission changed. Phase 1 remains IN PROGRESS / NOT ACCEPTED; Production integration remains NO-GO.

Next: validate root identity across requests using a server-owned root registry that stores stable directory identity (not only a path string), then add production-equivalent request cancellation/disconnect tests and expand the same request-scope ownership discipline across operation-family adapters. Keep all work isolated; do not migrate Production handlers yet.

## Root identity across requests — 2026-10-31

Inspected the isolated native root API. The existing test-only addon already exposes stable directory identity from a held HANDLE using Windows `GetFileInformationByHandleEx(FileIdInfo)`: the pair `VolumeSerialNumber + 128-bit FILE_ID_INFO.FileId.Identifier`. The identity is read only after opening the root as a plain non-reparse directory. No native production source or Production handler was changed.

Added `.native-probe/root-identity-registry-test.cjs` and `test:native-root-identity`; included the new test in `npm run check`. Its server-owned test registry maps opaque rootIds to configured paths plus the stable identity captured from a native root HANDLE. Per request, it reopens the configured path, rejects reparse roots, compares the fresh HANDLE identity before allowing the operation, and closes the HANDLE on mismatch. It rejects unknown/stale rootIds, overlapping/duplicate configured roots, and duplicate directory identities.

The 12-check harness passed. It verifies initial registration and handle-relative read, unknown rootId rejection, overlapping/duplicate configuration rejection, detection when the configured directory is renamed and the original path is recreated with a different directory, rejection of a junction replacement, and successful access to the same original directory after it is moved and explicitly re-registered at its new path. This establishes the movement policy for the prototype: a configured root is path-bound; a legitimate move requires explicit server-side re-registration. A stale rootId never silently follows a replacement or a moved path.

Verification after the addition: `npm run lint` exit 0; `npm test` exit 0 (42 core checks, 34 format/config checks, HTTP auth 401/200, 36 URL-security checks); native root HANDLE API, read-only adapter, rootId contract, request-scope lifecycle, and root-identity registry suites all exit 0. The new root-identity harness reports 12 checks passed. No Production schema/handler, native production source, configured ROOTS, dependency, service, tunnel, runtime setting, or permissions changed.

Limits: this is a test-only registry model, not wired into live MCP tools or the configured Production ROOTS. It validates identity by reopening the registered path on each request; it does not persist HANDLEs across requests. The Windows identity is volume serial plus file ID, not a cryptographic identity and should be treated as filesystem-provided identity. Network/remote filesystem behavior and file-ID stability guarantees across all supported filesystems are not established. ACL permission-denied live acceptance remains open; injected access-denied is not a substitute. Phase 1 remains IN PROGRESS / NOT ACCEPTED; Production integration remains NO-GO.

Next: exercise production-equivalent disconnect/cancellation behavior and apply request-scoped HANDLE ownership to each operation family (write/atomic replace, move, directory create/list/search, tail/multi-read, edit and format operations), still test-only. Then repeat full acceptance including live ACL denial before considering any Production migration.

## MCP cancellation and HTTP disconnect lifecycle — 2026-10-31

Inspected the installed MCP SDK request lifecycle. Tool handlers receive `extra.signal`; the SDK aborts it on a `notifications/cancelled` notification and when the connected transport closes. The Streamable HTTP transport's response-stream `cancel` callback removes its response-stream mapping, but it does not itself abort the Protocol request-handler controller.

Added three isolated test harnesses and package scripts:
- `.native-probe/mcp-cancellation-lifecycle-test.mjs` / `test:native-mcp-cancellation`: exercises the real MCP SDK tool dispatch and proves both protocol cancellation notification and transport close abort `extra.signal`; request-scoped native HANDLEs close after the awaiting callback resumes (6 checks).
- `.native-probe/http-disconnect-cancellation-test.mjs` / `test:native-http-disconnect`: runs a real local Node HTTP + StreamableHTTPServerTransport request, cancels the client response stream while the tool handler is pending, and confirms `extra.signal.aborted` remains false. The handler's HANDLEs close only after the callback eventually settles (6 checks). This is a confirmed current behavior/gap, not a hypothetical concern.
- `.native-probe/http-disconnect-bridge-prototype-test.mjs` / `test:native-http-disconnect-bridge`: test-only proof of a possible adapter. It buffers/parses the incoming JSON-RPC body and forwards it as parsedBody, creates per-request disconnect controllers keyed by session ID plus JSON-encoded request ID, aborts on premature HTTP response close, combines that signal with the SDK `extra.signal` using `AbortSignal.any`, and cleans the controller registry after request handling. Real HTTP disconnect aborts the combined signal; the request HANDLE is closed and the registry entry is removed (8 checks).

Verification: all three new tests passed; the existing root identity and request-scope suites passed; `npm run lint` exit 0; `npm test` exit 0 (42 core, 34 format/config, HTTP auth 401/200, 36 URL-security). New harness files are included in the syntax-check script. No Production handler, Production HTTP wrapper, Native production source, configured ROOTS, dependency, service, tunnel, runtime setting, or permission changed.

Important limits: the disconnect bridge is only a test prototype and is not wired into `server.mjs` or live MCP tools. Its request-body buffering, body-size/error handling, batch behavior, session/request-ID correlation, races between normal response completion and close, and compatibility with the exact production HTTP wrapper still need hardening and broader tests. MCP cancellation is cooperative: it cannot interrupt a synchronous native filesystem call already executing; request scopes must check cancellation at safe acquisition/operation boundaries and always close in finally. Production integration remains NO-GO; Phase 1 remains IN PROGRESS / NOT ACCEPTED. Live ACL permission-denied acceptance is still open.

Next: harden and adversarially test the disconnect bridge in isolation (body limits and malformed JSON, batches, concurrent sessions with colliding IDs, premature request abort, normal completion vs close races, controller cleanup on all error paths, transport shutdown), then apply the accepted request-scope pattern to operation-family adapters. Do not modify Production until acceptance gates, including live ACL denial, are satisfied.


## HTTP disconnect bridge hardening — 2026-10-31

Added isolated .native-probe/http-disconnect-bridge-hardening-test.mjs and package script test:native-http-disconnect-hardening; added the file to the npm run check syntax chain. The harness passes 13 checks covering streamed JSON body parsing, malformed JSON rejection, a strict body-size cap (1 KiB in the harness), explicit rejection of JSON-RPC batches, invalid JSON-RPC shape, request-key isolation across sessions, numeric-vs-string request IDs, duplicate active ID rejection, normal response close not aborting, premature response close aborting, identity-checked cleanup, interrupted body reads, and final registry cleanup.

Verification on 2026-10-31: dedicated hardening test exited 0 with 13 checks. npm run lint exited 0 and syntax-checked the new harness. npm test exited 0 (42 core checks, 34 format/config checks, HTTP auth 401/200, 36 URL-security checks). Re-ran rootId contract (12), request-scope lifecycle (12), root identity (12), MCP cancellation lifecycle (6), HTTP disconnect baseline (6), and HTTP disconnect bridge prototype (8); all exited 0.

Scope and limits: the new hardening harness tests policy/helper semantics in isolation; it does not yet prove all those cases against a fully integrated real HTTP server/SDK bridge. Batch requests are explicitly rejected by the prototype policy rather than supported. Premature abort during body receipt is covered as a stream-read rejection, but production server-level socket/timeout behavior still needs an end-to-end test. Transport shutdown with multiple pending requests, handler rejection races, parsing/transport exceptions under real HTTP, and close-vs-response completion races still need dedicated integration coverage. No Production HTTP wrapper, MCP handler, native production source, configured ROOTS, dependencies, services, tunnels, runtime settings, or permissions were changed. No ACL changes were made. Phase 1 remains IN PROGRESS / NOT ACCEPTED; Production integration remains NO-GO. Live ACL permission-denied acceptance remains open.

Next: extend the real local HTTP bridge harness to cover error-path cleanup, multiple pending requests during transport shutdown, and response-completion/disconnect races; then map request-scoped HANDLE ownership across operation-family adapters, still test-only.


## Real HTTP bridge shutdown/error-path acceptance probe — 2026-10-31

Added `.native-probe/http-disconnect-bridge-shutdown-test.mjs` and `test:native-http-disconnect-shutdown`; included the harness in `npm run check`. This is a local real-HTTP + MCP SDK + native HANDLE integration harness, still isolated from Production. It verifies malformed JSON and JSON-RPC batch requests are rejected before controller registration; a tool-handler rejection does not strand a registry entry; normal successful response completion does not falsely abort its request signal; and shutting down the Streamable HTTP transport while two tool requests are pending aborts both handler signals. After the pending handlers are allowed to settle, both native root HANDLEs are closed and the request controller registry is empty. Dedicated test exited 0 and reported 12 checks.

Verification: `npm run lint` exited 0, including syntax check of the new harness. `npm test` exited 0 (42 core checks, 34 format/config checks, HTTP auth 401/200, 36 URL-security checks). The prior test-only suites remain separately available; the dedicated new shutdown/error harness passed on its latest run.

Limits: this harness closes the MCP Streamable HTTP transport explicitly; it does not yet simulate an operating-system listener shutdown with active sockets, kernel-level cancellation of a blocked native I/O call, every possible race between response `finish` and socket `close`, or real Production HTTP middleware behavior. The bridge remains a prototype. No Production HTTP wrapper, MCP handlers, native production source, configured ROOTS, dependencies, services, tunnels, runtime settings, or permissions were changed. No ACL changes were made. Phase 1 remains IN PROGRESS / NOT ACCEPTED; Production integration remains NO-GO. Live ACL permission-denied acceptance remains open.

Next: test listener/socket shutdown and response-finish/close ordering under repeated concurrency, then review cancellation propagation for each filesystem operation family. Keep all work in `.native-probe` until all acceptance gates, including live ACL denial, pass.


## Follow-up: real HTTP socket-close case — 2026-10-31

Extended `.native-probe/http-disconnect-bridge-shutdown-test.mjs` with a third in-flight tool request and a real server-side socket close via `http.closeAllConnections()`. The test now proves the client fetch is terminated, the request-scoped combined signal aborts on the response `close` event, and the native HANDLE closes after the test handler settles. It separately retains the two-request `transport.close()` scenario. It also verifies that a normal fast response does not abort its signal after response completion. Latest dedicated result: exit 0, 15 checks; reported `httpSocketDisconnectAborts:true`, `normalResponseCloseSafe:true`, `pendingRequestsAbortedOnTransportClose:true`, `handlesClosed:true`, and `registryClean:true`.

After this extension, `npm run lint` and `npm test` were rerun and both exited 0. The 42 core, 34 format/config, HTTP auth, and 36 URL-security checks all passed. This remains a test-only harness; no Production integration or ACL changes. Remaining limits: OS listener shutdown semantics across all deployment topologies, exhaustive `finish`/`close` race permutations, kernel-level cancellation of blocked native I/O, and Production middleware behavior remain unproven. Phase 1 is still IN PROGRESS / NOT ACCEPTED; Production remains NO-GO pending all gates, especially live ACL permission-denied acceptance.


## Filesystem operation-family review — 2026-10-31

Re-ran native test-only write/create and directory-create race suites. `test:native-create-race` exited 0 and covered six outcomes: parent rename plus outside junction replacement stays bound to held parent HANDLE; injected post-open failure closes parent HANDLE; regular leaf collision preserves bytes; leaf junction collision fails closed; in-root read after synchronized insertion returns exact bytes; outside leaf junction read is blocked. `test:native-dircreate-race` exited 0 and covered four outcomes: parent replacement containment, existing directory collision preservation, junction collision fail-closed, and injected post-create close-status failure with committed-result reporting and parent HANDLE release. Also re-ran `test:native-read-family-race` (4 outcomes), `test:native-dirwalk-race` (22 checks), and `test:native-readonly-adapter` (10 checks); all exited 0. `npm run lint` and `npm test` both exited 0 (42 core checks, 34 format/config checks, HTTP auth 401/200, 36 URL-security checks).

Important gap found by source review of `advanced-tools.mjs`: current `move_file` still performs path-based `securePath` + `noSymlinkAncestors` checks followed by `fs.rename`; `edit_block` performs path-based checks and uses temp-file `fs.writeFile` + `fs.rename`; `create_directory` uses path-based resolution followed by `fs.mkdir`. The native test-only addon coverage currently demonstrates exclusive file creation and exclusive directory creation relative to held parent HANDLEs, but does not provide equivalent HANDLE-relative rename/move, replace/edit, or general write adapters. Therefore these mutating operation families are NOT accepted against TOCTOU races. Existing ordinary smoke tests prove functional behavior and basic junction checks, not adversarial race safety. `read_multiple_files` and `list_directory` production paths also remain path-based despite the separate native read-family/dirwalk prototypes.

No Production handler was modified. Do not infer that file move/edit/write safety is accepted from the current native create tests. Next work should remain test-only: define required native contracts for safe replace/edit and rename/move (including collision, same-volume, reparse, and failure-after-commit semantics), then implement and race-test one operation family at a time. `create_directory` production handler remains path-based and also requires a future migration plan despite the test-only native exclusive directory primitive. No configured ROOTS, runtime settings, dependencies, services, tunnels, or permissions were changed; no ACL changes. Phase 1 remains IN PROGRESS / NOT ACCEPTED; Production integration remains NO-GO. Live ACL permission-denied acceptance remains open.


## Next mutation-family design gate — 2026-10-31

Added `.native-probe/mutation-adapter-contract.md` as a test-only design contract for HANDLE-relative rename/move and atomic replace/edit. It defines request-scoped parent HANDLE ownership, single-leaf validation, same-volume-only rename, native RootDirectory-relative rename, atomic no-overwrite/replacement policy, explicit pre-commit vs post-commit error reporting, temporary sibling lifecycle, metadata questions, synchronized race cases, and HANDLE accounting. It also explicitly states that the existing exclusive directory primitive does not make Production recursive `create_directory` safe.

This is a design artifact, not an implementation or acceptance result. No native C source, Production handler, runtime configuration, ROOTS, service, tunnel, dependency, or permission was changed. The immediate implementation target should be the smaller rename/move primitive, isolated in a separate native probe/addon, with no-overwrite semantics and deterministic worker-process race tests. Replace/edit follows only after rename semantics and post-commit reporting are proven. Phase 1 remains IN PROGRESS / NOT ACCEPTED; Production remains NO-GO; live ACL permission-denied acceptance remains open.

## Test-only HANDLE-relative rename prototype — 2026-10-31

Implemented a separate native addon source at .native-probe/handle-relative-rename-probe.c, build script at .native-probe/build-raya-phase1-rename.cmd, and JS harness at .native-probe/handle-relative-rename-test.cjs; registered test:native-rename-relative and its syntax check in package.json. The addon consumes the existing opaque parent HANDLE owners (it does not reopen either parent by path), validates both leaf names, verifies both parents are plain directories on the same volume, opens the source leaf relative to the held source parent using NtCreateFile with DELETE, FILE_OPEN_REPARSE_POINT, and OBJ_DONT_REPARSE, rejects reparse points/directories, and performs NtSetInformationFile(FileRenameInformation) with RootDirectory=destinationParent and ReplaceIfExists=FALSE. Cross-volume moves fail closed; no copy/delete fallback exists. The result distinguishes successful commit from committed_but_cleanup_failed after a source-HANDLE close-status failure.

The independent addon compiled successfully with Visual Studio 2026 Build Tools. test:native-rename-relative exited 0 with 15 checks: same-parent rename; distinct-parent same-volume rename; existing file and directory collisions preserve the destination; existing destination reparse leaf blocks without following or replacing it; directory source rejected by the regular-file-only v1 prototype; missing source rejection; unsafe source/destination leaf rejection; source file-symlink rejection without following its target; injected pre-commit failure preserves source; post-commit cleanup failure is reported distinctly; source-parent and destination-parent rename plus junction replacement cannot redirect their already-held HANDLEs; and explicit parent HANDLE owners return process HANDLE count to baseline. Cross-volume behavior was not exercised because the fixture used one volume. Added a separate worker-process race harness at .native-probe/handle-relative-rename-worker.cjs and .native-probe/handle-relative-rename-worker-race-test.cjs. Four independent Node workers each opened source and destination parent HANDLEs, signaled readiness, then the parent process renamed both directories and installed outside junctions before releasing all workers. All four renames committed into the original held destination directories; outside source/destination sentinels remained unchanged; all worker-owned parent HANDLEs were explicitly closed. The worker harness was then repeated for five full rounds: 20 worker processes total and 120 assertions, all passing. The repeat runner is .native-probe/handle-relative-rename-worker-stress-test.cjs and is exposed as test:native-rename-worker-stress.

Verification after adding the new test script: npm run test:native-rename-relative, npm run test:native-rename-workers, npm run test:native-rename-worker-stress, npm run lint, and npm test all exited 0. The repeated worker race test passed five rounds (20 worker processes, 120 assertions). npm test reported 42 core checks, 34 format/config checks, HTTP authentication (unauthorized blocked; authenticated initialize 200), and 36 URL-security checks.

This is a test-only prototype, not Production integration or acceptance of the move_file handler. Production move_file remains path-based; edit_block, create_directory, and the read/list operation paths remain as previously assessed. No Production handler/native source, configured ROOTS, runtime settings, dependencies, services, tunnels, or permissions were changed; no ACL changes were made. Phase 1 remains IN PROGRESS / NOT ACCEPTED; Production integration remains NO-GO. Live ACL permission-denied acceptance remains open. Cross-volume rejection was tested successfully between C: and D:; remote/network volume semantics remain unverified. Decision update (2026-10-31): the documented Windows APIs reviewed do not expose an expected destination volume+FileId compare-and-swap condition for atomic replacement. FileRenameInformation/Ex selects the destination by parent-relative name; ReplaceFileW selects it by pathname and opens the target with FILE_SHARE_DELETE. A restrictive-share target HANDLE can pin identity but prevents the sibling replacement commit while held; writing through it is in-place, non-atomic mutation with partial-write risk. Therefore existing-file atomic edit has no accepted implementation and must fail closed. Keep atomic create-if-absent publish as the only accepted write-publication subset. A weaker in-place edit would require a separately approved product contract that explicitly accepts partial visibility and durability risk; it must not be called atomic replace. No Production changes. Phase 1 remains IN PROGRESS / NOT ACCEPTED; Production remains NO-GO; live ACL denial remains open.

## Atomic publish contention stress — 2026-10-31

Added `.native-probe/handle-relative-publish-worker.cjs` and `.native-probe/handle-relative-publish-worker-stress-test.cjs`; registered `test:native-publish-workers` and included both scripts in the `npm run check` syntax chain. Five synchronized rounds each launched four independent worker processes. In all 5 rounds exactly one worker published the shared destination using the native parent-HANDLE-relative create-if-absent primitive; the other three received no-overwrite collisions. Winner bytes were intact, all losing temporary siblings were removed, and each worker's process HANDLE count returned to its own baseline. Result: 20 worker processes, 20 round-level assertions, exit 0.

Fresh verification after adding the stress harness: `npm run test:native-publish-workers` exit 0; `npm run test:native-publish-exclusive` exit 0 (9 checks); `npm run test:native-rename-relative` exit 0 (15 checks); `npm run test:native-rename-worker-stress` exit 0 (5 rounds / 20 workers / 120 assertions); `npm run lint` exit 0; `npm test` exit 0 (42 core, 34 format/config, HTTP auth 401/200, 36 URL-security checks). These are test-only prototypes; no Production handler or native production source changed.

The identity-conditional replacement decision above remains unchanged: existing-file atomic edit is unsupported / fail closed. The publish stress proves atomic create-if-absent contention only, not replacement. Phase 1 remains IN PROGRESS / NOT ACCEPTED; Production remains NO-GO; live ACL permission-denied acceptance remains open.

Next: continue the remaining operation-family acceptance work with the same test-only discipline. Keep existing-file atomic edit unsupported unless a filesystem/provider-specific identity-conditional transaction or a separately approved weaker cooperative/in-place contract is proven; do not weaken the threat model. The Windows rename API exposes replace/no-replace and POSIX replacement semantics, but no expected-target-FileId compare-and-swap contract; do not implement existing-file atomic edit until that gap is resolved. A separate test-only atomic create-if-absent publish primitive is now implemented and verified (9 checks). The publish test, existing rename stress (5 rounds / 20 workers / 120 assertions), npm run lint, and npm test all exited 0. See .native-probe/mutation-adapter-contract.md. This primitive does not replace existing files; no Production handlers, native source, ROOTS, runtime settings, services, tunnels, or permissions were changed.


## Broker v2 read-path research update — 2026-11-27

This section supersedes the older October 30 "Immediate next action" entries below for the current PR #19/#20 research branch. It does not change the Phase 1 acceptance gate.

### PR #19 — pinned-root Broker v2 read-only slice
- Remains an independent Draft PR; not merged or connected to production handlers.
- The one-host elevated acceptance run created a uniquely named disposable virtual-service-account probe, validated the protected root DACL, rejected an injected Everyone Modify ACE, exercised restricted-token write/DACL/owner/attribute/directory-create/image-write/hard-link denials, and verified cleanup removed the test service, process, fixture and image.
- This is a test-only fixed-root read slice, not general filesystem authorization or operation-family parity.

### PR #20 — read-path foundations
- Latest branch head at this update: `31d45eec805f7681b8f4e261f1b44d613c802176`; PR remains Draft and not merged.
- Added UTF-16 relative-path lexical validation; HANDLE-relative nested directory opens; same-HANDLE final regular-file open; bounded HANDLE-relative directory enumeration; a synthetic I/O lifecycle model; real disposable overlapped named-pipe cancellation; a heap-owned four-operation reaper experiment; a bounded RYA2 frame codec/response state machine; and a message-mode named-pipe frame transport experiment.
- Local MSVC x64 evidence: lexical 25 assertions; directory enumeration 30 base assertions; lifecycle model 34; overlapped cancellation 17; reaper 42; frame codec/state 57; message-mode transport 12. HANDLE-chain tests passed with the file-symbolic-link case explicitly skipped because this host disallowed it. Directory symbolic-link creation was also skipped locally with ERROR_PRIVILEGE_NOT_HELD (1314).
- GitHub Actions run #55 failed only in the directory-enumeration test: the test inspected only the first OS page even though the created reparse entry was on a later page. The harness now iterates every page before checking the reparse entry. Run #56 (commit `d559e1d`) is in progress; its result must be observed before claiming CI acceptance for the latest PR head.
- A directory HANDLE opened with read-only sharing blocked directory rename on this host, but did not block creation of a new child. Enumeration is therefore not a snapshot. Calls on one HANDLE must be serialized; returned names are not authorization and must be reopened relative to the pinned parent HANDLE and revalidated.
- The message-mode transport is still a synchronous test harness. Bounded per-frame ACK deadlines, an overall request deadline, REQUEST schema, ERROR semantics and disconnect coverage at every protocol phase are not implemented.
- The reaper prototype demonstrates one safe ownership pattern at a bounded four-operation test scale. It is not yet a final production concurrency cap or an IOCP-vs-event architecture decision.

### Phase 1 acceptance status

**IN PROGRESS / NOT ACCEPTED. Production filesystem confinement remains NO-GO.**

The source-to-contract audit still identifies all 15 registered filesystem operations/modes as not accepted: `workspace_roots`, `workspace_list`, `file_read`, `file_read_chunk`, `file_write`, `file_write_chunk`, `create_directory`, `get_file_info`, `read_multiple_files`, `list_directory`, `move_file`, `edit_block`, `start_search` filename mode, `start_search` content mode, and `read_file_tail`.

The following blockers remain:
1. The NumberOfLinks == 1 observation can become stale: Windows permits a new hard link after the check while the read HANDLE is still open. The protected-root/restricted-token denial tests are useful evidence, but the final threat-model decision and enforcement proof remain open.
2. Directory enumeration is bounded and HANDLE-relative but not snapshot-consistent under concurrent child creation/deletion; reparse-entry runtime coverage was skipped locally due privilege restrictions (CI rerun pending).
3. The frame transport lacks bounded ACK and request-wide deadlines, complete REQUEST/ERROR schemas, and full disconnect-phase coverage.
4. RootId-to-configured-ROOTS binding and request-scoped HANDLE lifecycle must be accepted end-to-end.
5. The operation-family race, transaction, permission-denied, failure/cleanup and provider-compatibility matrices remain incomplete.
6. The native probes are not integrated into production handlers. No production service, ACL migration, workspace deployment or runtime fallback was performed.

Exact next action: obtain a green latest-head CI run; then add bounded transport deadlines and REQUEST/ERROR semantics, expand the native transport disconnect tests, and continue the operation-family acceptance matrix. Do not integrate any candidate into Production until every Phase 1 gate is satisfied.


### PR #20 follow-up — request/error contract and deadlines (2026-11-27)
- Latest PR #20 head: `e9e1788a954a572a221160ed7e83a4a0c46bfc9d`; remains Draft, not merged.
- Added REQUEST payload schema: operation code, syntactically constrained rootId, UTF-16LE relative path, zero reserved field; READ_FILE requires a nonempty path and LIST_DIRECTORY permits the configured root. This validates syntax only; it does not bind rootId to configured ROOTS or authorize the caller.
- Added terminal ERROR payload with bounded numeric codes, retryable flag and reserved-zero checks; ACK is required for the terminal error.
- Added overflow-safe monotonic deadline arithmetic: one overall request deadline and each frame budget is the minimum of its per-frame cap and remaining request time.
- Extended actual overlapped named-pipe tests to 23 assertions. Two pending reads were canceled and safely reaped under one 500 ms request deadline with 200 ms per-frame caps; the second frame received only the remaining budget.
- Added request/error contract tests (44) and deterministic deadline tests (20). Message-mode pipe transport now has 28 assertions, including REQUEST and terminal ERROR round-trip.
- All ten native commands passed locally on this host. The preceding GitHub Actions run #56 completed green end-to-end for commit `d559e1d` (all then-configured native tests, npm audit, syntax, modular verification and MCP tests). The new commit `e9e1788` adds two more CI test commands; its own CI run must complete before claiming latest-head CI acceptance.
- The framed message-mode transport remains a synchronous test harness. The deadline arithmetic is tested against real overlapped reads, but it is not yet wired into the message-frame transport/reaper.

## 2026-11-27 follow-up — configured root registry and request contexts

- PR #20 latest head: ae0ea46126b4daee5d62e8ea2d5fafb94acabae5; Draft, not merged.
- Added test-only root-registry.c/.h: trusted config maps 1..8 unique rootIds to absolute paths; roots are opened once and retained; requests resolve rootId and use relative paths only.
- READ_FILE pins parent components relative to the configured root and opens the final regular file; LIST_DIRECTORY pins the requested directory. Active request contexts hold a registry reference; registry close refuses with ERROR_BUSY until contexts release their handles.
- The module validates configured root path type/reparse/delete-pending state, but it does NOT validate owner/DACL, authenticate the caller, load the application's actual ROOTS configuration, or integrate with Node handlers.
- Added 44 root-registry/request-context assertions: two-root selection, root and nested reads/listings, invalid root/path rejection, size caps, directory-vs-file rejection, request cleanup, shutdown while active, root namespace pinning and atomic invalid-config handling. Configured-root symlink test is skipped locally due ERROR_PRIVILEGE_NOT_HELD (1314); CI is expected to exercise it.
- All eleven native test commands passed locally on Windows x64 / MSVC. The previous CI run #58 was fully green for commit e9e1788; latest run #59 for ae0ea46 is in progress and includes the new root-registry test.
- Phase 1 remains IN PROGRESS / NOT ACCEPTED. All 15 production filesystem operations/modes remain unaccepted. Production confinement remains NO-GO.

## 2026-11-27 follow-up — end-to-end read-only vertical slice

- PR #20 latest head: d6a896675f1500a3834879bbed64a68efd43228d; Draft, not merged.
- Extended message-mode named-pipe tests to 86 assertions. The new end-to-end test sends a REQUEST, decodes its schema, resolves the configured rootId, acquires a request-scoped root/file HANDLE, reads the file, sends RESPONSE_HEADER/DATA/END as separate pipe messages, and validates the ACK for every frame.
- The vertical slice is still test-only and synchronous at the message-frame layer. It is not connected to the Broker service or Node handlers. The async deadline budget is tested separately against real overlapped pipe reads, not yet wired into this framed transport.
- All eleven native commands passed locally. GitHub Actions run #59 is fully green for the preceding commit ae0ea46, including all native commands, npm audit, lint, modular verification and MCP tests. Run #60 (ID 45971616128) for latest head d6a8966 is queued and must be observed.
- Remaining blockers: production root owner/DACL and client identity validation, production ROOTS integration, async framed ACK deadlines and disconnect coverage at each protocol phase, all write/edit/move/rename/delete operation families, operation race/transaction matrices, and hard-link mutation policy.
- Phase 1 remains IN PROGRESS / NOT ACCEPTED. All 15 production filesystem operations/modes remain unaccepted; production confinement remains NO-GO.

\n

### 2026-11-27 — current Phase 1 research status

- PR #20 remains Draft. Last pushed head: d74c23c4213c518db768a84771a1d48f290c74da. Run #63 (45973916065) is fully green for that head.
- Test-only REQUEST schema now has five read-only operation codes: READ_FILE, LIST_DIRECTORY, GET_FILE_INFO, READ_FILE_CHUNK and READ_FILE_TAIL. Its fixed header is 20 bytes and carries a checked 64-bit offset and bounded 32-bit length.
- File reads and metadata use the same validated final-file HANDLE. Chunk reads require an exact in-file range; tail reads return up to the requested suffix. Request contexts pin parent directory chains and hold registry references.
- A new overlapped message-mode ACK-read experiment applies a shared monotonic request deadline and per-frame cap. Timeout requests CancelIoEx and drains terminal completion before releasing the stack OVERLAPPED/event/buffer. It has 27 assertions and passed four consecutive local runs. This is not a persistent reaper and not integrated into the full framed transport.
- Twelve native test commands pass locally on Windows x64/MSVC. Key counts: root registry/request contexts 63; REQUEST/ERROR 62; message-mode transport 109; overlapped message-mode ACK reads 27; I/O lifecycle 34; deadline arithmetic 20; overlapped cancellation 23; reaper 42; frame codec 57. Some symbolic-link fixtures are explicit SKIPs due ERROR_PRIVILEGE_NOT_HELD (1314).
- Next increment must be committed/pushed and its own CI observed. Then implement heap-owned persistent reaper ownership for asynchronous message-mode frame/ACK operations.
- Remaining gates: production root owner/DACL and authenticated client identity; production ROOTS integration; full asynchronous framed deadlines/cancellation/disconnect matrix; workspace_roots/workspace_list/read_multiple_files/search; write/edit/create/move/rename/delete; directory enumeration consistency; hard-link mutation policy.
- Phase 1 remains IN PROGRESS / NOT ACCEPTED. All 15 production filesystem operations/modes remain unaccepted. Production confinement remains NO-GO.
- Original dirty checkout C:\Users\Ali\local-mcp-bridge remains untouched; no Production service, ACL migration or real workspace deployment.

# Current Project Status — Raya Local MCP Bridge

Last updated: 2026-11-20

## Broker prototype v2 validation subtrack - 2026-11-20
- Broker-prototype-v2 Stage 5.16 strict dual-post-terminal-ACK criterion is COMPLETE in the isolated prototype branch. Two independently OS-issued Adapter operations each reached their own post-terminal ACK-notification failure on one shared Worker; typed Submitter ACK outcomes and Adapter-scoped retained publication preserve fail-closed behavior for every other failure class.
- Stage 5.19 is COMPLETE within the frozen independent per-plane v1 inventory contract: two Worker records and one Adapter record per Adapter were copied and their identity pairs matched. No atomic cross-plane snapshot is claimed.
- Stage 5.18 remains PARTIAL: DIRECT discard_unissued, Adapter reserve-rejection cleanup, and combined Adapter+Submitter NOT_ISSUED notification failure are runtime-covered. Existing Worker wake-failure test confirms a transferred-but-unprocessed operation remains Adapter-retained and Worker-queued/reserved but absent from Worker retained inventory (confirmed gap). Managed Worker-level native WAIT_FAILED runtime evidence is now covered by the owner-approved RAYA_ADAPTER_TESTING-only wait-boundary seam and a real Worker/Adapter/Submitter process-isolated test (10 × 31 assertions PASS). The separate transferred-but-unprocessed wake-failure Worker-inventory gap remains confirmed and unresolved. Invariant-guard runtime fault injection is intentionally excluded as manufactured internal corruption. Stage 5.20 remains INCOMPLETE (no approved positive recovery/reconciliation contract or action).
- Broker Stage 5 disposition: Gate D complete/no action approved; Gate E blocked; Gate F pending full Stage 5/Phase 1 owner acceptance and PR review. Stage 5/Phase 1 remain NOT ACCEPTED; Production remains NO-GO. This prototype-only result does not change the top-level filesystem Phase 1 acceptance status below.
- Authoritative evidence: `broker-prototype-v2-20261106/validation/STAGE-5-16-TO-5-20-EXECUTION-RESULTS-20261118.md`; architecture/test details: `broker-prototype-v2-20261106/validation/STAGE-5-16-STRICT-DUAL-ACK-ARCHITECTURE-DESIGN-GATE-20261120.md`.

## 2026-11-06 — isolated broker identity gate and v2 specification
- The disposable dedicated-account broker's LocalSystem negative-client test passed: SYSTEM could open the named pipe but the broker rejected the mismatched TokenUser SID/session with the expected unauthorized frame. Task Scheduler returned LastTaskResult=0.
- The temporary task, output file, broker service, broker process, fixture and image directory were removed; cleanup verified service absent, zero broker processes, fixture absent and image absent.
- Recorded the test-only protected-filesystem broker v2 specification in docs/PROTECTED-FILESYSTEM-BROKER-V2-SPEC-20261106.md. It defines protected service-owned roots, authenticated root IDs, strict single-link policy and staged per-operation acceptance.
- No v2 operation implementation or Production integration has been made in this step. Production handlers remain path-based; Phase 1 remains NOT ACCEPTED and Production NO-GO.
- Latest repository validation after the documentation update: npm_lint exit 0; npm_test exit 0 (46 core, 34 format/config, HTTP auth/initialize, 36 URL-security); git_diff_check exit 0 with line-ending warnings only.
- No workspace ACL/owner, configured roots, Production handlers, dependencies, runtime settings, unrelated services or tunnels were changed. IranPriceScout and port 8765 were not touched.

## Mission
Build a secure, reliable Windows local MCP bridge with practical Desktop Commander-like capabilities.

## Repository and security status — 2026-11-01
- Private GitHub repository: https://github.com/ijoolaie/Raya-MCP; default branch `main`; initial source push verified. See HANDOFF.md for the import manifest and exclusions.
- Local baseline checks passed: `npm run lint`; `npm test` (42 core, 34 format/config, HTTP auth/initialize, 36 URL-security checks). The catalog/test inventory reports 45 tools; this does not mean every tool has independent acceptance coverage.
- Dependabot alert #1 for GHSA-g3hv-75wg-m249 / CVE-2026-118764 has a remediation candidate on `security/minimatch-remediation`: Archiver 8.0.0, Unzipper 0.12.5, minimatch 11.0.1. Local and clean-clone `npm audit` report 0 vulnerabilities; full lint/test and XLSX round-trip pass. [PR #1](https://github.com/ijoolaie/Raya-MCP/pull/1) is open; Windows/Node 22 GitHub Actions passed on the current code head. The default-branch Dependabot alert remains open until merge and re-verification; see `docs/DEPENDENCY-SECURITY-20261101.md`.
- A local change in `.native-probe/hardlink-sharing-experiment.c` remains uncommitted and preserved; `to-iman-hand-off.md` is ignored and unpushed. A separate hard-link guard prototype/test is also local and test-only; it does not modify the pinned prototype or Production handlers. It rejects multi-link reads and blocks rename/unlink while the read HANDLE is held on the tested NTFS host, but is not accepted as race-free or portable. See `.native-probe/hardlink-alias-policy.md`.
- Live DACL denial was verified on the current NTFS host for `createChildExclusive`, `createDirectoryChildExclusive`, and `readChildFromParent`; each returned `STATUS_ACCESS_DENIED`, then succeeded after only the temporary fixture's deny ACEs were removed. No global/user-profile ACLs were changed. See `.native-probe/acl-denial-acceptance.md`.

## Raya Agent bootstrap inventory — 2026-11-05
A read-only source inventory and import policy has been recorded in `docs/RAYA-AGENT-IMPORT-MANIFEST-20261105.md`. Observed source: 109 tracked files at HEAD `ef99ccec39f3eb318573421322df5e10bdc4335e` (8 commits ahead of origin/main), 15 modified tracked files, and 116 untracked non-ignored files. 79 tracked files are in `.native-probe/`; these contain security research/tests and are to be segregated from production runtime. The broker prototype and other untracked probes require selective review; binaries, logs, tunnel YAML backups, secrets and machine-specific artifacts are excluded. No Raya Agent repository has been created yet. The next step is path-level review of working-tree diffs and prototype source before preparing a clean staging export.

## Product architecture decision — 2026-11-05
The user approved the direction to keep Raya MCP as an independent project and create Raya Agent as a separate project initially reusing the full applicable Raya MCP codebase. A formal decision record is in `docs/ARCHITECTURE-DECISION-RAYA-MCP-AND-AGENT-20261105.md`. Implementation has not started: the immediate next step is a read-only source inventory and import/exclusion manifest, not a blind copy of the dirty working tree. Raya Agent will own planning/orchestration/approvals/context/UX and connect to Raya MCP through a versioned public MCP interface. Both products must remain independently testable and releasable. This decision does not change Raya MCP's security status or authorize production service/ACL migration.

## Overall status
<badge>Active development</badge> Phase 1 — filesystem confinement is the active phase. Production filesystem confinement is **NOT ACCEPTED as race-free**.

## Phase status
| Phase | Status | Notes |
|---|---|---|
| 0 — Baseline and architecture | Implemented; ongoing | Workspace, tool families, threat/acceptance gaps and phase policy documented |
| 1 — Filesystem confinement | In progress / Not accepted | Held-parent read, exclusive zero-byte create, identity readback, finalizer, close-fault and selected cleanup tests pass in isolated prototype; live DACL denial now passes for read/file-create/directory-create on current NTFS; hard-link alias policy and broader operation-family/provider gates remain |
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

## Read/metadata and ACL follow-up — 2026-11-01

Re-ran `handle-relative-read-family-test.cjs` (exit 0; 4 outcomes) and `handle-relative-readonly-adapter-test.cjs` (exit 0; 10 checks), covering synchronized in-root tail/multi-read insertion, outside junction rejection, root/file/directory metadata mapping, UTF-8/size limits, and HANDLE cleanup. `npm run lint` and `npm test` both exited 0 (42 core, 34 format/config, HTTP auth 401/200, 36 URL-security checks).

A separate disposable-file live ACL probe applied an explicit current-SID `icacls` `(DENY)(RA)` ACE; `icacls` showed the deny entry, but `inspectChildFromParent` still succeeded while requesting `FILE_READ_ATTRIBUTES`. The temporary ACE was removed and fixture deleted. This metadata ACL case is UNRESOLVED / NOT ACCEPTED; the exploratory test was not retained in the harness. The existing live DACL results remain limited to file create, directory create and content read. See `.native-probe/acl-denial-acceptance.md`.

No Production handlers, native Production source, configured ROOTS, dependencies, runtime settings, services, tunnels or unrelated workspaces were changed. The pre-existing user modification `.native-probe/hardlink-sharing-experiment.c` remains untouched. Phase 1 remains IN PROGRESS / NOT ACCEPTED; Production remains NO-GO. Next: determine why the RA deny does not block metadata open, build an independently observable metadata ACL test, then continue operation-family/provider acceptance. Hard-link policy remains awaiting user decision.

## Direct Win32 metadata-open cross-check — 2026-11-01 (unresolved)

To determine whether the prior RA result was specific to the pinned NtCreateFile adapter, a temporary standalone N-API probe called Win32 `CreateFileW` directly with `dwDesiredAccess = FILE_READ_ATTRIBUTES` only (0x80), all share flags, `OPEN_EXISTING`, and `FILE_FLAG_OPEN_REPARSE_POINT`. Before applying a deny ACE, the open succeeded as expected. After applying `icacls /deny` first to the exact current TokenUser SID and then in a separate fixture to `Everyone` (`S-1-1-0`), the open still succeeded. The same ACLs were independently enumerated from the file DACL: the explicit deny ACE was present with mask `0x00000080`, and its SID matched the process TokenUser SID in the user-SID case. `inspectChildFromParent` also continued to succeed. `SeBackupPrivilege` was reported disabled; the process token had the Administrators group enabled. Both deny ACEs were removed from their disposable fixtures, which were then deleted.

This shows the observation is not limited to the prototype's `NtCreateFile` path: on this host, the tested Win32 `CreateFileW(FILE_READ_ATTRIBUTES)` open also succeeded despite the observed explicit RA deny ACE. It does NOT establish that Windows universally ignores RA denies. The current token is elevated and includes Administrators; the cause remains unresolved. A separate `AccessCheck` diagnostic was inconclusive because the helper returned error 1338 (invalid security descriptor), so it is not evidence either way. Do not mark metadata ACL denial as passed or generalize this result to other tokens/providers. Next: reproduce under a non-elevated standard-user token (without changing account or global permissions unless explicitly approved), and validate the security-descriptor/access-check helper independently before drawing a Windows ACL conclusion. Temporary probe sources/build outputs were removed; no new test harness or Production source was retained.


## 2026-11-02 — root namespace pinning regression fixed in isolated candidate

A prior synchronized characterization showed the isolated hard-link-guard candidate's `openRelative` called `open_root_chain(..., pinNamespace=0, ...)`. A test callback successfully renamed the configured root and installed an outside junction at its former path while the operation was walking the root. This was a real namespace-pinning defect in that candidate.

Changed only the isolated `.native-probe/handle-relative-hardlink-guard.c` call to request `pinNamespace=1`; no Production handler or pinned production addon was changed. Built a separately named addon (`napi_probe_raya_phase1_hardlink_guard_rootpin.node`) to avoid overwriting the previous artifact. Added a focused regression harness and a copy of the cross-process hard-link guard harness targeting the new addon.

Evidence on the current local NTFS host:
- Native MSVC x64 build exited 0.
- Root namespace regression exited 0: configured-root rename was blocked with `EBUSY`; `openRelative` returned the original marker content; no junction replaced the configured root.
- Cross-process hard-link guard suite exited 0: single-link read/identity allowed; concurrent hard-link creation was detected before bytes were returned; rename/unlink remained blocked while the read HANDLE was held; pre-existing multi-link and outside-backed aliases were rejected; HANDLE count returned to baseline.
- New harness syntax checks passed.
- `npm run lint` exited 0.
- `npm test` exited 0: 42 core, 34 format/config, HTTP auth/initialize and 36 URL-security checks.

Scope: this is still an isolated candidate, not Production integration or acceptance of the hard-link policy. The strict `NumberOfLinks == 1` policy still rejects legitimate hard links and remains awaiting the user's policy decision. The metadata `FILE_READ_ATTRIBUTES` deny behavior remains unresolved; the 2026-11-02 diagnostics narrowed the observed behavior but did not establish a portable Windows/NTFS rule. Do not continue broad OS/filter-driver exploration without a concrete decision-changing hypothesis. Phase 1 remains IN PROGRESS / NOT ACCEPTED; Production integration remains NO-GO.


## 2026-11-02 — HANDLE-relative rename ACL acceptance

Added a disposable-fixture ACL test for the isolated native rename prototype and registered it as npm run test:native-rename-acl. Exact current-user SID deny ACEs were independently visible:
- Destination directory FILE_ADD_FILE (WD) deny caused parent-relative rename to fail with STATUS_ACCESS_DENIED (0xC0000022); source remained and destination was not created.
- Source file DELETE (D) deny caused source open to fail with STATUS_ACCESS_DENIED; source remained and destination was not created.
- Removing only the test ACEs restored both rename operations; resulting destination contents were verified.
- Explicit parent HANDLE count returned to baseline and the disposable fixture was removed.

Fresh verification: npm run test:native-rename-acl exit 0; npm run test:native-rename-relative exit 0 (15 checks); npm run test:native-rename-worker-stress exit 0 (5 rounds / 20 workers / 120 assertions); npm run lint exit 0; npm test exit 0 (42 core, 34 format/config, HTTP auth/initialize, 36 URL-security checks).

Scope is the isolated rename addon on this host and tested ACL entries. This is not a Production move_file acceptance, not provider-wide proof, and does not resolve metadata FILE_READ_ATTRIBUTES. Existing production filesystem handlers remain path-based and unchanged. Hard-link policy is still awaiting decision. Phase 1 remains IN PROGRESS / NOT ACCEPTED; Production remains NO-GO.


## 2026-11-02 — isolated directory-rename operation slice

Created a separate directory-capable clone of the native rename candidate and a dedicated test. The original file-only rename addon and Production handlers were not changed. The new candidate opens a child relative to the held parent with no FILE_NON_DIRECTORY_FILE restriction, then checks the opened object's reparse flag and actual directory type before applying the same no-overwrite parent-relative rename.

npm run test:native-rename-directories exited 0 with 12 checks: empty and non-empty directory moves; destination file/directory collision preservation; reparse source rejection; missing/unsafe leaf rejection; source DELETE and destination FILE_ADD_SUBDIRECTORY live DACL denial with successful retry after ACE restoration; source/destination parent path-to-junction replacement resistance; cross-volume rejection; and HANDLE baseline restoration. The build used MSVC x64 and produced a separately named addon.

After registering the new harness in package.json, npm run lint and npm test both exited 0 (42 core, 34 format/config, HTTP auth/initialize, 36 URL-security checks). This is local NTFS prototype evidence only; directory-rename multi-worker stress and provider portability remain untested. Production move_file remains path-based and unaccepted. Metadata FILE_READ_ATTRIBUTES remains unresolved; strict hard-link policy remains pending. Phase 1 remains IN PROGRESS / NOT ACCEPTED; Production remains NO-GO.


## 2026-11-02 — directory rename multi-process stress accepted for the candidate

Added synchronized independent-process directory rename workers and a five-round stress runner, registered as npm run test:native-rename-directory-workers and included in npm syntax checks. Each round uses four workers that acquire source/destination parent HANDLEs before the controller renames both directories and replaces the old paths with outside junctions. Workers must rename nested directory trees into the original held destination directories, preserve canary content, leave outside sentinels unchanged, and close their HANDLE owners.

Fresh results: 5 rounds, 20 workers, 180 assertions, exit 0. A single-round diagnostic also passed (4 workers / 36 assertions). The normal directory suite remains 12 checks. npm run lint and npm test passed after registering the worker scripts. This strengthens only the isolated directory candidate on local NTFS; Production move_file remains unchanged and unaccepted, provider portability is untested, metadata FILE_READ_ATTRIBUTES remains unresolved, and strict hard-link policy remains pending. Phase 1 remains IN PROGRESS / NOT ACCEPTED; Production remains NO-GO.


## 2026-11-02 — native rename cancellation boundary

Added and registered npm run test:native-rename-cancellation. Four checks passed: pre-aborted signal prevents mutation; cancellation queued with setImmediate cannot interrupt the synchronous native rename; the completed commit remains reported as committed; and a post-commit cleanup/status failure remains distinguishable as committed_but_cleanup_failed. Parent HANDLEs return to baseline.

This confirms only the JavaScript/event-loop boundary around the isolated synchronous native rename. The kernel operation is not force-cancellable while executing. Any future adapter must check cancellation before starting and must never rewrite a committed result as cancelled. MCP notifications/HTTP disconnect behavior remains governed by the separate transport lifecycle tests. No Production handler changed.


## 2026-11-02 — user-approved strict hard-link policy

The user explicitly approved the conservative single-link policy for Raya MCP: every regular file admitted to a confined file operation must have `FILE_STANDARD_INFO.NumberOfLinks == 1`; if the count is unavailable, inconsistent, or greater than one, the operation must fail closed. This intentionally rejects legitimate in-workspace hard links as well as outside-backed aliases. Do not silently fall back to path-based handling or weaken the rule for compatibility.

This is a policy decision, not Production acceptance. The current isolated hard-link-guard candidate enforces the check only in `readChildFromParent`, `inspectChildFromParent`, and `openRelative`; all other file-operation families (including rename/move, metadata/list/search adapters, write/publish and any future mutation path) still require explicit applicability review and adversarial tests. The check must be performed against the opened file HANDLE, not a separately resolved pathname. Revalidate at the operation's commit/read boundary where concurrent link creation is possible; sharing flags alone are not a hard-link defense.

The 2026-11-02 Medium-integrity RA/RD matrix independently confirms content READ_DATA denial for the tested read adapter, while metadata inspection still succeeds despite an explicit READ_ATTRIBUTES deny. Metadata authorization remains FAIL / UNRESOLVED and is not waived by this hard-link decision. Provider coverage remains local NTFS only. Phase 1 remains NOT ACCEPTED; Production remains NO-GO.


## 2026-11-02 — latest ACL matrix and validation status

The Medium-integrity RA/RD matrix (disposable local NTFS fixtures; HANDLE count unchanged at 189) passed the content-read denial cases: explicit RD and RA+RD denies returned STATUS_ACCESS_DENIED with no bytes; RA-only content read succeeded as expected. Metadata inspection nevertheless succeeded under RA deny. This is an explicit metadata authorization failure/unresolved condition, not a pass. See .native-probe/metadata-acl-ra-rd-matrix-results-20261102.md.

A fresh npm run lint completed with exit code 0. The subsequent npm test invocation was blocked by the OpenAI safety layer before a test result was produced; it is BLOCKED, not passed or failed. No Production code, runtime configuration, ACL, service, tunnel, or permission was changed in this continuation.


## 2026-11-02 — strict hard-link policy applied to isolated rename characterization

Created a separately named file-only rename candidate and harness: .native-probe/handle-relative-rename-hardlink-probe.c, build-raya-phase1-rename-hardlink.cmd, and handle-relative-rename-hardlink-test.cjs. The candidate opens with FILE_SHARE_READ, rejects pre-existing regular files whose NumberOfLinks is not 1, and rechecks the same HANDLE immediately before rename. MSVC x64 build exited 0. The 18-check local NTFS harness exited 0 and covered pre-existing multi-link rejection, insertion after initial validation detected by recheck, normal rename/no-overwrite behavior, reparse and cross-volume rejection, failure/commit reporting, and HANDLE baseline.

**The harness also deliberately reproduced a critical residual race:** a hard link inserted after the final NumberOfLinks query but before NtSetInformationFile caused the rename to commit a multi-link file. Thus FILE_SHARE_READ plus pre-commit recheck is insufficient; regular-file rename remains NOT ACCEPTED. This is a characterization failure, not an accepted security fix. The test-only API includes synchronization hooks solely to make both race windows deterministic.

Registered the new harness as test:native-rename-hardlink and added its syntax check to npm run check. No Production handler or native Production source was changed. See .native-probe/hardlink-operation-family-audit-20261102.md. The existing strict policy remains approved, but cannot be considered enforced for rename until link creation is excluded across the commit boundary. Metadata RA denial remains unresolved; provider portability remains untested; Phase 1 NOT ACCEPTED; Production NO-GO.

## 2026-11-02 — hard-link share-mode contradiction and ACL boundary

A fresh five-case cross-process native reproducer on the local NTFS volume confirmed that CreateHardLinkW succeeded while another process held the source with share mode 0 in all tested access modes (GENERIC_READ; FILE_READ_ATTRIBUTES|SYNCHRONIZE; FILE_READ_DATA|SYNCHRONIZE), and also with FILE_SHARE_READ and full sharing. The held handle's link count changed 1→2 in every case. This contradicts the CreateHardLinkW documentation statement about non-sharing handles; the empirical local NTFS result is repeatable. Do not rely on share modes to exclude hard-link creation.

A disposable-fixture DACL experiment found that DENY FILE_WRITE_ATTRIBUTES to the current user blocked CreateHardLinkW (Win32 error 5). However, a same-identity child could replace the fixture DACL with a NULL DACL and then create the link; the parent restored the original DACL and cleaned up. The same token could open WRITE_DAC. Therefore a simple per-file deny ACE is not a durable same-owner boundary. No workspace ACLs or ownership were changed.

A service identity with protected ownership/DACLs could be investigated as a separate architecture, but would change file ownership/access semantics and needs explicit approval. For the current same-user model, share-mode pinning and a simple DACL deny do not prove exclusion across the commit boundary. Regular-file rename remains NOT ACCEPTED; strict HANDLE-based NumberOfLinks admission checks remain required. Detailed commands/results and the Microsoft documentation conflict are in .native-probe/hardlink-operation-family-audit-20261102.md. Metadata READ_ATTRIBUTES denial remains unresolved; provider portability remains untested; Phase 1 NOT ACCEPTED; Production NO-GO.

## 2026-11-02 — publish/read-tail/metadata/enumeration hard-link gaps

Built isolated publish-hardlink addon and harness. MSVC x64 build exit 0; 10 checks exit 0. The test opens a staging file with ShareAccess=0, creates an outside hard link while the staging HANDLE remains open, and proves the candidate still publishes the file with NumberOfLinks=2. Atomic create-if-absent publication is therefore NOT ACCEPTED under the strict policy.

A separate rootapi characterization harness (exit 0) created an outside-backed alias inside a disposable workspace. The isolated rootapi candidate's readChildFromParent and readTailChildFromParent returned the outside-backed bytes; inspectEntryMetadata returned numberOfLinks=2 without rejecting; enumerateDirectoryPage returned the alias as a normal file entry without a link count. This rootapi candidate does not enforce the approved policy for these operations. The hardlink-guard candidate covers only its distinct readChildFromParent, inspectChildFromParent and openRelative APIs; read-tail, directory enumeration and publish remain uncovered or incomplete.

Added test-only package scripts and syntax checks for the new characterization harnesses. No Production handlers, workspace ACLs, file ownership, runtime configuration, ROOTS, services or tunnels were changed. See .native-probe/hardlink-operation-family-audit-20261102.md. Next: build separate guarded read-tail/metadata/enumeration/publish candidates and adversarial tests; decide separately whether a service identity architecture is acceptable. Phase 1 NOT ACCEPTED; Production NO-GO.

## 2026-11-03 — isolated dedicated-identity named-pipe prototype

Implemented a separate test-only Windows service candidate under `.native-probe/broker-prototype-20261103/`. It uses a dedicated virtual service account and protected disposable fixture; the local named pipe has a restricted DACL, rejects remote clients, and validates the actual client TokenUser SID and TokenSessionId. The protocol is versioned and exposes only `READ fixture`; no caller-supplied filesystem path is accepted. The service checks regular-file type, reparse status, single-link count, and a 4096-byte read cap.

Evidence: final MSVC x64 build exit 0 without warnings; service reached RUNNING; Win32 client passed 3/3 protocol checks (fixed-root read/content integrity, unknown-root denial, unsupported-operation denial). Service stop/delete and fixture/image cleanup succeeded; `sc query` returned 1060 after deletion.

Important gap: Node.js `net.createConnection` fails with EPIPE against the broker pipe and also against an independent minimal Win32 byte-mode echo pipe; the cause remains unresolved. An isolated native Win32 console adapter successfully bridged Node `child_process` stdio to a Win32 named pipe in both byte and message modes, but it has not yet been adapted to the broker's split header/payload protocol or tested against the broker service. No Raya MCP transport integration is claimed. See `.native-probe/broker-prototype-20261103/RESULTS-20261103.md`. Negative tests with another user/session, remote clients, pipe ACL denials, fuzzing, slow clients, concurrency, crash recovery, and full operation families are not complete.

The disposable service was removed. No Production service, workspace ACL/owner, configured root, runtime setting, Production handler, dependency, or tunnel changed. The separate IranPriceScout tunnel was not touched. This prototype does not close the Phase 1 hard-link operation-family gaps. Phase 1 remains NOT ACCEPTED; Production remains NO-GO.

Supplemental handoff: `.native-probe/broker-prototype-20261103/HANDOFF-20261103.md`. The main `HANDOFF.md` append was blocked because its existing 261,868-byte size exceeds the connector's 256 KiB edit limit; its pre-existing contents were preserved.

## 2026-11-04 — protocol framing and lifecycle follow-up

The broker's first end-to-end read test exposed a message-mode framing defect: the server wrote the success header and payload as two separate named-pipe messages, while the native client expected one complete response message. The broker now assembles the header and payload into one bounded message; the Win32 acceptance client checks the combined response, and the Node adapter reads and validates the complete framed message. MSVC x64 builds for both binaries exited 0.

Fresh disposable-service evidence (all performed against the fixed fixture only):
- Win32 protocol client: 3/3 passed (fixed-root content and 50-byte payload integrity, unknown-root rejection, unsupported-operation rejection).
- Node `child_process` → native adapter → named pipe → broker: passed; success framing and exact 50-byte content verified; unknown-root and unsupported-operation response framing verified; empty, oversized (256-byte), and actual-NUL stdin inputs rejected with the expected exit codes.
- Reconnect after a client connects and disconnects before sending a request: passed.
- Idle connected client receives `RAYA/1 ERR BAD_REQUEST` after the 2-second request deadline: passed.
- Service stop while a client is connected but idle: service reached STOPPED within the bounded wait: passed.
- Client that sends a valid read request but does not read the response: service remained RUNNING; a subsequent full Node protocol suite passed.
- After each service run, service deletion was verified (`sc query` error 1060), no `named-pipe-broker.exe` process remained, and both ProgramData fixture/image paths were absent.

A first lifecycle test attempt was invalid because the PowerShell .NET stream did not support the synchronous `ReadTimeout` property; a subsequent test used `ReadAsync` with a bounded task wait and passed. A separate early-disconnect/reconnect test passed when run independently. Do not count the invalid attempt as a test result.

Remaining NOT TESTED / NOT ACCEPTED: different-user/session authorization denial, remote-client rejection, explicit pipe-DACL negative tests, malformed/partial/slow request fuzzing beyond the stated cases, concurrent clients, forced process termination/crash recovery, service binary/config tamper resistance, other filesystem operation families, provider portability, and formal security review. The service still uses a synchronous single-instance prototype with `PIPE_NOWAIT` polling; this is not a production-grade overlapped-I/O server and does not establish a production security boundary. No Raya MCP runtime transport integration was made.

No Production handlers, workspace ACLs/owners, configured roots, runtime settings, dependencies, unrelated services, or tunnels were changed. The IranPriceScout tunnel on port 8765 was not touched. Production remains **NO-GO**; Phase 1 remains **NOT ACCEPTED**.

Additional protocol boundary check (2026-11-04): a syntactically incomplete request (`RAYA/1 READ` without the terminating newline) was sent through the Node native adapter to the live disposable service and returned the exact `RAYA/1 ERR BAD_REQUEST` response. The normal Node protocol suite passed immediately afterward, confirming the malformed request did not poison the next connection. A direct oversized pipe-message test was not accepted as evidence because the first PowerShell client attempt received an empty response; the adapter's own 256-byte stdin rejection remains verified, but broker-side oversized-message handling needs a dedicated native test harness.


2026-11-04 isolated broker follow-up: replaced the prototype's PIPE_NOWAIT polling with event-based overlapped ConnectNamedPipe/ReadFile/WriteFile and bounded cancellation. MSVC builds, Win32 and Node protocol suites, idle-client deadline, reconnect, and stop-while-read-pending passed. Disposable service and ProgramData artifacts were cleaned and absence verified. This lifecycle run used LocalSystem with service SID enabled after SCM rejected the virtual-account configuration (1057), so it does not validate dedicated virtual-account deployment. Forced write-pending, concurrency, identity/remote/DACL denial, crash/tamper, and security review remain open. Production remains NO-GO; Phase 1 NOT ACCEPTED.

Additional 2026-11-04 overlapped check: a client sent a valid request but did not read the response; the service stayed RUNNING after 2.5 seconds and the Node protocol suite passed afterward. Cleanup was repeated and verified. This does not prove forced partial/blocked WriteFile behavior.

2026-11-04 dedicated-account correction: the prior overlapped lifecycle run used LocalSystem after an initial account-configuration attempt failed. That account setup issue has now been resolved using a native ChangeServiceConfigW helper. A fresh full overlapped transport run under `NT SERVICE\RayaFsPipeProbe20261103` passed: service RUNNING, Win32 3/3, Node integration, idle timeout, reconnect, stop during pending read, restart, and non-reading-client recovery. Both fixture and image directory owners were confirmed as the virtual service account. Service, process, and ProgramData artifacts were removed and absence verified. Different-user/session and remote denial, pipe-DACL negative tests, forced blocked writes, concurrency, crash/tamper resistance, and formal review remain open. Production remains NO-GO; Phase 1 NOT ACCEPTED.

2026-11-04 restricted-token DACL negative tests: a restricted interactive-user token (Administrators SID disabled, privileges removed) was denied file write, WRITE_DAC, WRITE_OWNER, FILE_WRITE_ATTRIBUTES, directory file/subdirectory creation, broker-image write, and CreateHardLinkW into the protected fixture. The hardlink result was counted only after replacing an invalid nonexistent-target CreateFile test with an actual CreateHardLinkW call; it returned ACCESS_DENIED and no link remained. An alternate service-token authorization test was inconclusive because OpenProcessToken on the service PID returned ACCESS_DENIED; different-user/session denial remains NOT TESTED. Remote-client and explicit pipe-DACL connection denial also remain open. Disposable service and ProgramData artifacts were cleaned. Production NO-GO; Phase 1 NOT ACCEPTED.

## 2026-11-04 - direct oversized named-pipe message boundary

A native Win32 client bypassed the Node adapter and sent one 257-byte message directly to the broker's 256-byte message-mode request buffer. The broker rejected it by closing the connection; the client observed `ERROR_PIPE_NOT_CONNECTED` (233), not a `RAYA/1 ERR BAD_REQUEST` response. This is recorded as fail-closed rejection by disconnect, not as a successful protocol error response. The overlapped read path now preserves `ERROR_MORE_DATA` and the transferred-byte count. Immediately afterward the normal Win32 3/3 suite and full Node adapter protocol suite passed, showing the service remained usable. MSVC x64 builds succeeded. The dedicated virtual-account service was deleted and fixture/image roots were verified absent. Arbitrarily large messages, fragmentation/slow-send behavior, concurrency, alternate identities, remote clients and formal review remain untested. Production remains NO-GO; Phase 1 NOT ACCEPTED.

## 2026-11-04 - single-instance concurrent-client queue

Eight independent native client processes were launched concurrently against the disposable virtual-account service. Each ran the three-case protocol suite and produced PASS logs for the read-only fixture, unknown-root rejection and malformed-operation rejection (24 successful case outputs). All completed within 30 seconds. The Node adapter suite also passed afterward against the same service. The PowerShell process wrappers did not yield usable exit-code values, so this evidence is based on completion and captured PASS logs, not independently collected exit codes. This bounded smoke test does not establish fairness, starvation freedom, throughput, unbounded queue capacity, or parallel execution. The design remains one pipe instance with serial request handling.

## 2026-11-05 — execution planning and baseline revalidation

The user approved planning and continuing the Raya project. The binding phase order remains Phase 1 filesystem confinement first; Phases 2–8 remain gated by prior phase acceptance. Detailed plan: `docs/RAYA-EXECUTION-PLAN-20261105.md`.

Post-tunnel-restart evidence:
- 25/25 sequential low-risk `get_config` requests succeeded.
- `/healthz` and `/readyz` returned HTTP 200; `/health/mcp` reported initialized and child running.
- `tools_list.ok=false` is an observer field indicating no retained forwarded `tools/list` exchange; it is not proof the catalog is absent. Current local npm smoke test enumerates 47 tools.
- Runtime config remains maxSessions=32, maxSearches=8, searchMaxResults=500, searchMaxDepth=8; text read/write 256 KiB, aggregate reads 1 MiB, outputChars=131072.
- `npm_lint` passed. Initial `npm_test` failed on a stale smoke-test expectation (64 vs configured 32 sessions). The single assertion was aligned to 32; rerun `npm_lint` and `npm_test` both exited 0: 46 core smoke checks, 34 format/config checks, HTTP unauthorized blocked / authenticated initialize 200, 36 URL-security checks.
- Managed process sessions=0 and active search sessions=0 after cleanup.
- Git working tree has extensive pre-existing changes; all were preserved. No reset, cleanup, commit, or branch change was performed.

Phase 1 remains IN PROGRESS / NOT ACCEPTED. Production filesystem handlers remain path-based; hard-link and metadata ACL acceptance gaps remain. The dedicated-identity broker is an isolated disposable prototype, not Production approval. Next: design a deterministic pending-response-write disconnect test and complete pipe identity/remote/DACL negative tests using only the disposable prototype scope. Do not install or change Production services, migrate real workspace ownership/ACLs, or integrate broker handlers without separate explicit approval. The disposable service and fixture/image paths were then removed and verified absent. Production remains NO-GO; Phase 1 NOT ACCEPTED.

## Raya MCP Admin Panel — 2026-11-05
- Added `raya-admin-panel/` as a separate local-only management UI. It supports tunnel start/stop/restart (only for a tunnel child launched by the panel), allowlisted workspace root add/remove, maxSessions/maxSearches/searchMaxResults/searchMaxDepth, and PowerShell enablement.
- Panel binds only to 127.0.0.1:8766; uses a fresh in-memory 256-bit bearer token, requires the tokenized session URL to serve the dashboard, rejects unauthenticated API calls and unexpected Host/Origin values, and does not persist or display CONTROL_PLANE_API_KEY.
- Added `start-raya-admin-panel.ps1`, `start-mcp-stdio-panel.cmd`, and `raya-admin-panel/stdio-launcher.mjs`. The tunnel profile `raya-local` now points its stdio command to the panel-aware launcher. Backup: `raya-local.yaml.backup-panel-20261105`.
- The panel-aware stdio launcher reads `raya-admin-panel/panel-config.json`, validates local existing non-reparse workspace roots, requires the local-mcp-bridge root, rejects drive roots/duplicates/nested roots/system folders, then sets MCP_ALLOWED_ROOTS and the PowerShell gate for server.mjs.
- Initial panel config mirrors the 19 current roots. Six are stale/missing and are retained for user review; the panel warns and blocks server start until those are removed or the intended folders are restored. No existing workspace or root directory was created/deleted.
- Verification: panel source and embedded UI script syntax checks passed; panel smoke test passed 9 checks (token-gated dashboard, unauthenticated API blocked, authenticated status, drive-root rejection, rejected config leaves file unchanged); `npm run lint` and `npm test` both passed (46 core, 34 format/config, HTTP auth/initialize, 36 URL-security). Panel-aware stdio launcher was started against a temporary test config with 13 existing roots, spawned `server.mjs`, and was terminated; original 19-root panel config was restored.
- Live handoff remains pending: the current tunnel was not stopped or restarted. `tunnel-client doctor` loaded the updated profile but its control-plane key check failed in the MCP tool process because CONTROL_PLANE_API_KEY is not inherited into that separate process environment. Start the panel from the original PowerShell session that has the key after Ctrl+C-stopping the current tunnel. Do not touch IranPriceScout or port 8765.
- This is a local admin-panel prototype, not a Windows service or a production security boundary. Production filesystem confinement remains NOT ACCEPTED; no commits or PRs were created.\n\n## 2026-11-06 security continuation\n\n- Revalidated baseline: `npm run lint` PASS. First `npm test` exposed a real managed-process termination race: the Windows `taskkill.exe` request was launched asynchronously and the MCP tool returned before the child process emitted `close`; the smoke test observed `status=running`.\n- Corrected `advanced-tools.mjs` so Windows tree termination awaits the `taskkill.exe` result and then waits up to 5 seconds for the managed child `close` event. `force_terminate` now reports success only after process exit is confirmed; timeout-driven termination records a bounded error rather than silently claiming completion.\n- Revalidation after the fix: `npm run lint` PASS; `npm test` PASS — 46 core checks, 34 format/config checks, HTTP auth/initialize (unauthorized blocked, authenticated 200), and 36 URL-security checks. The failed-run child PID was confirmed absent afterward.\n- Isolated broker test preflight confirmed service, fixture, image, pipe and broker process absent. Attempted to provision only the disposable prototype service; Windows Service Control Manager returned ACCESS_DENIED (5) at `sc create`. Setup stopped immediately; no service, ProgramData fixture/image, broker process, Production root/ACL, runtime setting, or tunnel was changed. Administrative elevation is required to continue service-boundary tests.\n- Existing hard-link findings remain unresolved in Production: current same-user path-based handlers do not prove race-free single-link enforcement across operation commit boundaries. The broker remains an isolated read-only prototype; Production remains NO-GO and Phase 1 NOT ACCEPTED.\n- No commits, resets, cleanup of pre-existing work, Production handler changes, workspace ownership/ACL migrations, or IranPriceScout/8765 changes were made.\n

## 2026-11-20 � Stage 5.18 design decision
See `broker-prototype-v2-20261106/validation/STAGE-5-18-TRANSFERRED-WAKE-FAILURE-QUARANTINE-DESIGN-20261120.md`. The selected prototype direction is atomic Adapter in-flight quarantine publication at the Worker-locked handoff boundary plus bounded Worker wait to observe DEGRADED. The queue-owned uncertain operation is not a Worker POST_TERMINAL retained record; v1 inventory semantics must remain unchanged. Design only; implementation and runtime evidence remain outstanding. Stage 5.18 PARTIAL; Stage 5.20 INCOMPLETE; Production NO-GO.

## 2026-11-20 — Stage 5.18 Gate 1 source/lock audit

Read-only source audit: `broker-prototype-v2-20261106/validation/STAGE-5-18-GATE-1-SOURCE-LOCK-AUDIT-20261120.md`. No Adapter -> Worker nested lock acquisition was found in the audited Worker/Adapter/Submitter modules; Submitter callbacks run outside Submitter lock and Worker release callback outside Worker lock. Gate 1 remains PARTIAL: the existing test-only handoff callback fires for both WAKE_SIGNAL and OWNERSHIP_INVARIANT degraded handoffs, so cause-specific gating is not yet proven. The hook uses the existing Adapter retained list, not a distinct production in-flight registry. Production wait remains INFINITE; bounded wait is test-only. No code or runtime behavior changed and no tests/builds were run. Stage 5.18 PARTIAL; Phase 1 NOT ACCEPTED; Production NO-GO.

## 2026-11-20 — Stage 5.18 Gate 1 closure

Gate 1 is PASS for the audited lock-order contract and the test-only WAKE_SIGNAL-only callback gate. The test-only handoff callback is now invoked only after committed integrated/pipe-owner handoff and wake-signal failure; owner-handoff invariant failure cannot invoke it. The integrated Adapter API suite passed 10/10 iterations (12,570 assertions); all declared EXE/OBJ outputs were verified absent. No Production ABI/behavior changed. Stage 5.18 remains PARTIAL: distinct production in-flight quarantine, versioned diagnostics and bounded production wait remain outstanding.
## 2026-11-20 — Stage 5.18 Gate 2 test-only publication slice

Gate 2 positive test-only atomic publication slice passed: a distinct Adapter quarantine registry is now compiled only under RAYA_ADAPTER_TESTING, separate from Adapter retained and Worker v1 retained inventories. The callback validates exact identity and committed queued state under Worker lock, publishes once under Worker -> Adapter lock order, and a second wake forces Worker-lock contention. Adapter Destroy rejects quarantine-only state. Validation: test suite 10/10, 12,630 assertions PASS; non-test-macro compile PASS; native Adapter suite 10/10 x 1,020 PASS; all 18 declared outputs cleaned. Gate 2 remains PARTIAL pending broader contention/multi-operation coverage and production registry/diagnostic/bounded-wait implementation. Production behavior/ABI and v1 inventory unchanged; Phase 1 NOT ACCEPTED; Production NO-GO.

## 2026-11-20 — Stage 5.18 Gate 2 negative publication failure verified

Added a test-only fail-before-mutation quarantine publication hook and a dedicated process-isolated real named-pipe negative case. The Worker remains DEGRADED with WAKE_SIGNAL and secondary OWNERSHIP_INVARIANT causes; no false quarantine entry appears; queued operation, reservation, owner/collection state, live I/O handles and issuer lease remain intact; no user callback, cleanup or recovery occurs. API build: 10/10; negative test 430 assertions, positive API regression 10,890, strict dual-ACK 1,200, NOT_ISSUED 250, WAIT_FAILED 310; total 13,080 PASS. Non-test-macro compile PASS; native Adapter suite 10/10 x 1,020 PASS; outputs cleaned. Gate 2 remains PARTIAL pending multi-operation/worker-contention coverage and queue membership validation beyond the single-head fixture; Production registry, versioned diagnostics and bounded wait remain unimplemented. Stage 5.16 strict dual-ACK and v1 inventory semantics unchanged; Phase 1 NOT ACCEPTED; Production NO-GO.

## 2026-11-20 — Stage 5.18 Gate 2 multi-operation source trace

Source review confirms the Submitter processes one request at a time but can retain a second queued request while the first is being published (`dedicated-io-submitter-v2.c:125-177`). The current test-only quarantine callback requires the transferred operation to be the sole Worker queue member and the queue head (`integrated-submit-adapter-v2.c:280-343`). A real two-operation test can therefore suppress the first test wake and fail the second; the current callback is expected to reject the second publication and mark OWNERSHIP_INVARIANT. This is not a passing quarantine test. Because the production Worker wait is still INFINITE, the negative multi-operation fixture must be process-isolated and must not claim Worker exit. Next: add the deterministic test-only two-wake scenario and assert fail-closed state without synthetic queue mutation or extra wake; then separately complete bounded-wait and versioned-diagnostic gates. No Production/ABI/v1 semantics or recovery behavior changed.

## 2026-11-20 Stage 5.18 Gate 2 multi-operation negative fixture

The deterministic process-isolated two-operation fixture is now implemented test-only. It holds the Worker before its real wait, submits two real named-pipe READs through the Adapter API, reports the first wake successful without signaling, and fails the second wake. The test proves both operations committed in the Worker queue and the current single-head quarantine callback rejects the second publication with OWNERSHIP_INVARIANT; the Worker is not claimed to exit and no synthetic wake/queue mutation/cleanup/recovery occurs. Full API build passed 10/10 (13,300 aggregate assertions); EXE/OBJ outputs cleaned. Gate 2 remains PARTIAL: successful multi-operation quarantine, production registry, versioned diagnostics and bounded production wait remain open. Stage 5.16 strict dual-ACK and Stage 5.19 v1 inventory unchanged; Production NO-GO.

## 2026-11-20 Stage 5.18 Gate 3 bounded-wait source audit

Source audit confirms timeout safety: Worker checks DEGRADED under Worker lock before reset/promote/claim; WAIT_TIMEOUT resolves without selecting an operation and rechecks DEGRADED under the same lock before looping. The handoff/timeout race is serialized by that lock. A 100 ms finite wait exists only in RAYA_ADAPTER_TESTING when the WAIT_FAILED hook is absent; the non-test Production branch remains INFINITE. This is not Production bounded-wait evidence or a selected timeout. Gate 3 remains PARTIAL pending process-isolated runtime exit, timeout race, normal idle/completion regression and measurement. Production implementation is not authorized by the design-only approval.

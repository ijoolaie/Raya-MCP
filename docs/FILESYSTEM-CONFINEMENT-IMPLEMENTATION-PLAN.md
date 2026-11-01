# Filesystem confinement implementation plan

Scope: C:\Users\Ali\local-mcp-bridge only. This is a planning artifact; no production handler or dependency was changed.

## Current findings

- `server.mjs` uses `checkedPath()` and then ordinary path-based operations such as `stat`, `readFile`, `access`, `mkdir`, and `writeFile`.
- `advanced-tools.mjs` uses `securePath()` / `noSymlinkAncestors()` followed by path-based `stat`, `readFile`, `writeFile`, `rename`, `mkdir`, and `readdir`. Search enumerates paths and later opens them by path.
- A concurrent replacement between validation and use is therefore not ruled out. Rechecking the path immediately before use only narrows the window.
- The declared runtime is Node 22 on Windows. No filesystem-specific native dependency is declared; observed `@napi-rs` packages are for canvas.

## Native build feasibility check (2026-10-26)

A deeper read-only check found Visual Studio Build Tools installed at `C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools`, MSVC toolset `14.51.36231`, Windows SDK `10.0.28000.0`, CMake, and npm's bundled node-gyp. The MSVC compiler executable runs and reports compiler version `19.51.36248`. Thus a compiler is present; the earlier PATH-only check missed it.

The matching Node v22.23.2 x64 headers and win-x64/node.lib were downloaded into the isolated .native-probe\node-gyp-cache using npm bundled node-gyp@11.5.0; no project dependency was added. node-gyp configure build cannot identify Visual Studio Build Tools 18 (reported as unknown version undefined), even from its Developer Command Prompt. To separate that detection issue from compiler capability, a minimal C N-API addon was compiled directly with the installed MSVC toolchain and linked against the matching node.lib. Node loaded the resulting .node file and the probe returned N-API version 10 (Node v22.23.2, x64, module ABI 127). This verifies only the basic native build/load path, not a filesystem-confinement implementation. Probe sources and build artifacts remain under .native-probe; no production handler or package manifest was changed.

## Handle-relative prototype check (2026-10-26)

An isolated prototype now opens each descendant component relative to the previously opened directory handle using `NtCreateFile` with `RootDirectory` and `OBJ_DONT_REPARSE`. It opens the configured root as a directory handle, rejects reparse-point components and unexpected file/directory types, and queries size from the final handle. It can also perform an explicitly capped read (1 byte to 1 MiB) from the exact final handle, returning a Node Buffer; metadata-only calls remain the default. The prototype is in `.native-probe/handle-relative-probe.c`; its fixture harness is `.native-probe/handle-relative-test.cjs` with a child worker. It is not imported by production code.

Observed isolated checks passed:
- normal nested file open;
- bounded content read from the exact final handle;
- oversized read rejected before buffer allocation/read;
- `..` traversal rejected;
- missing target fails closed;
- directory rejected where a file is expected;
- static junction traversal rejected;
- configured root that is itself a junction rejected;
- synchronized ancestor-name swap: the native probe calls a test-only callback only after opening `root\\safe` relative to the held root handle. The child emits `PARENT_HANDLE_OPENED` and blocks on stdin; the parent waits for that signal, renames `root\\safe`, installs an outside-root junction at the old name, then sends `SWAP_DONE`. The rebuilt isolated test passed and read `INSIDE-OK` from the held in-root directory. This makes the swap schedule explicitly synchronized; it still proves only this narrow read/open pattern, not the complete bridge threat model.

Important limitations: this validates one read-only/opening pattern, not the complete bridge threat model. The configured root is still opened by its full path, so ancestors above the configured root have not yet been walked handle-relative. A capped read is tied to the verified final handle, but it is not an atomic snapshot if another process modifies the same file in place during the read. The prototype now implements only bounded read in addition to open/metadata. It does not implement writes, create-new, rename, delete, directory enumeration, root-chain verification, reparse-tag policy beyond rejection, volume checks, or full concurrency/fuzz testing. The optional test pause parameter remains only in this isolated prototype; the current race harness uses an explicit callback/stdin barrier and passes zero delay. Do not treat these results as production acceptance or a general proof of TOCTOU resistance.

## Operation coverage required

| Tool family | Current native-prototype coverage | Production status | Required next acceptance evidence |
|---|---|---|---|
| file read / multi-read / tail | Single-file bounded read from final handle only; no multi-read/tail integration | NOT ACCEPTED; production still uses path-based checks/opens | Race tests for each handler; size/type checks on the same opened handle; verify no outside-canary reads |
| write / edit | None | NOT ACCEPTED | Parent-handle-relative create-new; temp sibling and publish semantics; injected write/rename failures; no-overwrite regression and outside-canary immutability |
| directory creation | None | NOT ACCEPTED | Component-by-component relative create; static and raced junctions at each component; cleanup and partial-create policy |
| list / search | None | NOT ACCEPTED | Enumerate from a held directory handle; open children relative to it; race between enumeration and child open; no outside names/content leak |
| metadata | Final-file size/type from the exact handle in prototype | NOT ACCEPTED; production metadata uses path-based calls | Verify identity/type/reparse status from the final handle; test file replacement and directory policy |
| move / rename | None | NOT ACCEPTED | Hold source and destination parent handles; same-volume constraints; atomic no-replace behavior; source/destination race and rollback tests |
| process working directory | None | NOT ACCEPTED; separate process-launch boundary | Document residual race or use a process-creation strategy that pins directory identity; do not infer safety from path validation alone |

## Staged implementation gates

1. Completed: obtained matching Node 22 headers and node.lib in the isolated probe cache; direct MSVC compile/link works. The node-gyp Visual Studio 18 detection issue remains an environment/tooling limitation.
2. Completed: compiled and loaded a minimal N-API addon in .native-probe; it reported N-API version 10. Do not wire it into production handlers yet.
3. Review a Windows-specific handle-based design and the relevant Win32/NT APIs. Ordinary Node path-based `fs` calls are not enough to claim a strong handle-relative guarantee.
4. Define primitives before implementing: open configured root; open child relative to held parent with explicit reparse policy; query final path and type from handle; read/write/enumerate; create/rename/delete relative to held parent handles.
5. Fail closed on unsupported platforms, unexpected reparse tags, device/UNC paths, volume changes, missing native binary, or any fallback to check-then-use path operations.
6. Test static junctions, dangling/nested links, root-as-link, replacement/rename, and no-overwrite behavior first. Then use a deterministic adversarial junction-swap test with disposable in-root and outside-root canaries. Any outside-root access or modification is a hard failure.
7. Review handle lifetime, sharing flags, rename/delete semantics, exception cleanup, and reproducible Node 22 x64 builds before integrating.

## Next isolated primitive gate: create-new relative to held parent (2026-10-30)

The first write-capable native primitive must not call the current `openRelative` and then write to a separately reconstructed path. The current helper also closes each previous directory handle as it walks, so it does not expose a held parent handle for a final relative create. Refactor the internal walk into a reviewed helper that can return ownership of the validated parent handle, while preserving existing read-only behavior and cleanup discipline.

Required invariants before coding:
- Parent walk is rooted at the configured root and each component is opened relative to a held directory handle; no full-path write fallback.
- The final filename is one component only; reject separators, colon/ADS syntax, dot components, trailing-dot/space aliases, and unsupported forms.
- Use `NtCreateFile` with `OBJECT_ATTRIBUTES.RootDirectory` set to the held parent and `FILE_CREATE` disposition; collision must fail without changing existing bytes.
- Write through the created handle, cap prototype input at 1 MiB, verify handle metadata and full write length, and surface cleanup failures.
- Test synchronized ancestor rename/swap, pre-existing sentinel preservation, outside-canary integrity, failed writes, cleanup failures, worker timeout/cleanup, and process handle-count stability.
- Keep the API isolated from production handlers and runtime dependencies until semantics and tests have been reviewed on the supported Windows/Node build.

Do not conflate this gate with the existing atomic-create smoke test: that test checks current JavaScript behavior under ordinary concurrency, not a native parent-handle-relative create under an adversarial swap.

## Acceptance status

Production confinement is not implemented and production handlers have not been race-tested. The isolated N-API prototype and its synchronized descendant/root-chain junction swap tests passed in prior isolated runs, but cover only a narrow read/open pattern. On 2026-10-29, the harness was revised to track both worker processes and their `close` events explicitly (rather than relying on a `proc.closed` property), terminate and await them during final cleanup, remove only fixture junctions verified against the expected target, restore renamed directories, and report cleanup errors. The package syntax-check now includes both native probe scripts, and `npm_lint` passed after the change. On 2026-10-29, normal native harness runs exited 0 and exercised the synchronized descendant-ancestor swap and configured-root-chain swap. Controlled test-only fault injections then covered: assertion failure after worker synchronization (expected exit 1); missing synchronization signal with a 250 ms timeout (expected exit 1); and simulated junction-creation denial (expected skip for that swap, restoration of the renamed directory, followed by a successful root-chain race). After each failure-path run, the workspace listing showed no new fixture directory; a subsequent normal run exited 0 and passed both race cases. Three older `fixture-*` directories were already present and were not removed because their provenance/targets were not fully verified. `npm_lint` and a fresh `npm_test` exited 0 (42 core, 34 format/config, HTTP auth, 36 URL-security checks). The test hooks are isolated to `.native-probe` and are not production code. These tests do not cover every OS-level cleanup failure. Keep the harness out of unattended CI and continue primitive design and operation-family tests before production integration.

## Parent-handle ownership implementation update — 2026-10-30

Implemented in the isolated native probe (not production):
- `openParentRelative(root, relativeParent)` performs a component-by-component directory walk from the verified root and transfers the final directory handle to a N-API external owner.
- Explicit `closeParentHandle` is idempotent; the external finalizer closes any handle not explicitly closed. `inspectParentHandle` returns directory/reparse flags and volume/file identity from the held handle.
- `readChildFromParent(parent, childName, maxReadBytes)` has been added as a bounded handle-relative child-read helper. It compiles but is not yet called by the test harness; status is IMPLEMENTED/UNTESTED.
- Tests currently prove file identity is stable after rename plus replacement of the old name with an outside junction; explicit and repeated close; rejection of inspection after close; and stable process handle count across 300 invalid/missing/trailing-path failures and a successful open/close.
- Native build/harness exit 0; `npm_lint` exit 0; `npm_test` exit 0 (42 core, 34 format/config, HTTP auth, 36 URL-security checks).
- A proposed harness edit to exercise the child-read API was blocked by the local safety layer; no workaround was attempted.

## Parent-relative child-read acceptance update — 2026-10-30

The harness now exercises readChildFromParent through the retained parent HANDLE after renaming the directory and replacing its former name with an outside junction. The helper returned the in-root PARENT-IDENTITY marker (15 bytes), while the outside OUTSIDE-MARKER canary remained unchanged.

Verified negative cases: traversal and multi-component child names, missing child, too-small read cap, directory child, and reparse-point child. The child junction was successfully created and rejected on this host. Repeated successful child reads and missing/cap failures (25 iterations) left process handle count unchanged. Existing explicit parent close, double-close, inspect-after-close rejection, 300 parent-path failure cases, and parent file-ID stability across rename/junction replacement also passed.

Latest native MSVC build and adversarial harness exited 0. The harness also passed static junction checks, synchronized descendant ancestor swap, and configured-root-chain swap failing closed. Fresh npm_lint exited 0. Fresh npm_test exited 0: 42 core, 34 format/config, HTTP auth, and 36 URL-security checks.

The external finalizer fallback remains implemented but not independently tested with deterministic GC/finalization evidence; explicit close is tested. No create/write operation, production handler, dependency, or runtime integration was added.

Acceptance status: parent-relative read path is TESTED in the isolated prototype; Phase 1 and production confinement remain NOT ACCEPTED as race-free. The parent external finalizer is independently verified in the following update. Production integration is not authorized.

## Parent external-finalizer verification — 2026-10-30

The native harness now spawns a bounded Node child process with `--expose-gc` to test the N-API external finalizer without relying on GC timing in the main harness. The child creates a disposable directory, records the process handle count, opens a parent-relative directory handle, verifies the count rises by exactly one, drops the only JS reference, explicitly invokes GC, and requires the finalizer to return the count exactly to baseline. Observed result: baseline 164, opened 165, after finalizer 164, first collection cycle; child exited 0. The parent harness applies a 10-second timeout, kills and reaps the child on timeout, and the child removes its temporary fixture in `finally`.

Latest full native build/harness exited 0 and included the finalizer test plus the existing parent-relative child-read, invalid-name/cap/type/reparse, handle cleanup, explicit close/double-close, static junction, synchronized descendant swap, and configured-root-chain fail-closed tests. Fresh `npm_lint` and `npm_test` exited 0 (42 core, 34 format/config, HTTP auth, 36 URL-security checks).

Parent/child ownership lifecycle now has tested evidence for explicit close, double-close, child-handle success/failure cleanup, and external finalizer cleanup on the current Windows/Node 22 x64 host. Residual limitations remain: no create/write primitive, no production integration, and no full operation-family race acceptance. Next: review N-API ownership/API edge cases and expand the operation-family matrix before considering an isolated FILE_CREATE experiment.

## N-API wrapper and leaf-name gate update — 2026-10-30

The isolated prototype now binds `ParentHandleRef` to the returned JS object with `napi_wrap` and retrieves it with `napi_unwrap`; the writable `_nativeParentHandle` external property was removed. Tests prove mutation/deletion of that legacy-looking property does not detach the owner and a copied plain object cannot alias it. Child leaf validation now rejects trailing dots/spaces, reserved DOS device basenames (CON/PRN/AUX/NUL, CONIN$/CONOUT$, COM/LPT numeric and superscript forms), controls and forbidden punctuation. The child-read success path checks the final `CloseHandle` result. Native MSVC build/harness, `npm_lint`, and `npm_test` passed after the changes (42 core, 34 format/config, HTTP auth, 36 URL-security checks).

The close audit is not complete: unchecked closes remain in root-chain/intermediate/failure cleanup paths; finalizer close failures cannot currently be surfaced to the caller; and no deterministic close-failure injection exists. Do not begin the isolated `FILE_CREATE` experiment until the remaining close/error semantics are audited and documented with a defensible lifecycle contract. No production integration is authorized.

## Close lifecycle gate update — 2026-10-30

All direct native-prototype close sites now use `close_handle_once(HANDLE *)`. The helper consumes the caller's handle slot before calling `CloseHandle`, increments an interlocked aggregate failure counter, and never retries. Root-chain and directory-walk transitions abort on close failure. Explicit owner close reports failure after consuming ownership. Finalizer and failure-cleanup paths make one best-effort attempt and publish failures through `closeFailureCount`; they do not retry or overwrite the original operation error.

The native harness includes a one-shot test-only fault that closes the real OS handle and then returns a synthetic failure status. Verified cases:
- Explicit parent close: throws, counter increments once, OS handle count returns to baseline, second close is idempotent and does not retry.
- Parent finalizer: injected failure increments the counter and OS handle count returns to baseline after GC.
- Root-chain intermediate transition: injected failure aborts the walk, increments the counter and does not leak the current/next handles.

This validates lifecycle/error semantics under deterministic post-close failure, not every possible kernel failure state. The test hook is in the isolated native-probe addon only. Latest native MSVC build/harness passed. npm lint/full tests passed before the final test-output-label-only adjustment; rerun at the next acceptance boundary.

**Gate decision:** the close-lifecycle review is sufficiently closed to begin a separate isolated no-overwrite `FILE_CREATE` experiment. This is not Phase 1 acceptance and does not authorize production integration. The experiment must hold the parent directory HANDLE, use one validated leaf component, preserve an existing collision target byte-for-byte, prove ancestor rename plus old-path junction replacement still creates under the held parent, and verify handle counts on all success/failure paths.

## Initial FILE_CREATE experiment — 2026-10-30

Implemented only in the isolated native probe: `createChildExclusive(parent, childName)` uses the held parent HANDLE as `OBJECT_ATTRIBUTES.RootDirectory`, `OBJ_DONT_REPARSE`, strict single-leaf validation and `NtCreateFile(FILE_CREATE)`. It creates a zero-byte regular file and closes the child HANDLE through the checked consume-once helper.

Passed:
- Existing target collision rejects; original `DO-NOT-OVERWRITE` bytes remain identical.
- Collision failure does not change process HANDLE count.
- Trailing-dot and reserved-device names reject before native create.
- After parent directory rename and old-path outside-junction installation, create through the held parent places the file only in the renamed original directory, not the outside target.
- Successful create returns process HANDLE count to baseline.
- Native MSVC x64 build/harness, npm_lint and npm_test passed (42 core, 34 format/config, HTTP auth 401/200, 36 URL-security).

## FILE_CREATE acceptance expansion — 2026-10-30

The harness now uses filesystem ready/go/report files to synchronize eight independent Node processes before they contend for the same leaf. Exactly one process created the zero-byte file and seven reported collisions. Thirty repeated create/collision pairs completed without process HANDLE growth. A Unicode BMP/supplementary-plane filename was created; ADS-colon, `::$DATA`, named-stream `:$DATA`, and control-character names were rejected. An ASCII case-only collision preserved the existing file; composed and decomposed acute-accent names were distinct on this test volume.

A one-shot post-create close-status fault was injected. `createChildExclusive` returned an error and incremented the close-failure counter, but the file remained present and zero bytes; process HANDLE count returned to baseline. Treat this as a committed-but-reported-failed ambiguous outcome. A caller must not assume rollback or retry through an overwrite-capable path.

The harness now requires `--expose-gc`, collects child-process objects after the race, and uses report files rather than retaining child stdio pipes. This avoids unstable process HANDLE accounting. The MSVC x64 build and full native harness exited 0. `npm_lint` and `npm_test` exited 0: 42 core checks, 34 format/config checks, HTTP auth/initialize checks, and 36 URL-security checks. The build uses a separately named acceptance-probe addon because the prior addon file was locked by an orphaned timed-out harness; the locked artifact was preserved.

At the initial FILE_CREATE checkpoint, child file ID/volume identity readback, case-folding/normalization and ADS alias cases, parent/leaf replacement schedules, and broader operation-family races remained open. Subsequent acceptance updates below close the first three narrow leaf/identity checks, but the broader parent/leaf race schedules and operation-family matrix remain outstanding. The prototype still creates only an empty file; it does not write content or integrate with production. Phase 1 remains IN PROGRESS / NOT ACCEPTED.

## Created-child identity readback — 2026-10-30

Added `inspectChildFromParent(parent, childName)` to the isolated addon. It opens the named child relative to the held parent with reparse protections and queries `FILE_ID_INFO`, `FILE_ATTRIBUTE_TAG_INFO`, and size from the opened HANDLE. The harness checks a nonzero 128-bit file ID, volume serial equality with the held parent, regular/non-reparse type, zero-byte size, stable repeated identity, and rejection of missing, traversal and ADS-colon names. The test is performed after renaming the parent and replacing the old path with an outside junction.

MSVC x64 build and full native harness exited 0; `npm_lint` and `npm_test` exited 0 (42 core, 34 format/config, HTTP auth/initialize, 36 URL-security). The harness also compares the low 64 bits of the native FILE_ID_INFO identifier with Node bigint `fs.stat().ino` for the same created file; the values matched on this test volume. Next: add synchronized parent/leaf replacement and failure-cleanup schedules, then expand the operation-family matrix. No production integration; Phase 1 remains NOT ACCEPTED.

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

Exact next action: extend the read primitive to multi-read/tail and directory-handle/descendant operations, then cover list/search, metadata, write/edit, and move/rename with equivalent race and failure-cleanup schedules. Keep production handlers unchanged.

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

## 2026-10-30 directory-walk verification update

An isolated native descendant-open and directory-page enumeration prototype has been built and tested. The persistent harness is `.native-probe/handle-relative-dirwalk-test.cjs` with root command `npm run test:native-dirwalk-race`. It verifies nested handle-relative opens, expected listing, post-acquisition intermediate junction replacement rejection, reparse-entry metadata without traversal, explicit over-cap failure plus restart by reopening, and HANDLE cleanup. The enumeration implementation validates the entire OS page and record boundaries; it no longer silently truncates at `maxEntries`. MSVC x64 build, native harness (seven checks), npm_lint, and npm_test all pass. npm_test reports 42 core, 34 format/config, HTTP unauthorized/authorized 401/200, and 36 URL-security checks.

Scope caveat: enumeration is currently a single 64-KiB OS page, with no continuation token. On over-cap failure the cursor is consumed; reopen the directory handle to restart. This is isolated prototype evidence only; Production handlers remain unchanged and Phase 1 is NOT ACCEPTED.

## 2026-10-30 enumeration pagination hardening

The isolated API now supports `enumerateDirectoryPage(directoryHandle, maxEntries, restart?)`: repeated calls continue from the current handle cursor; `restart=true` restarts on the same handle; `ERROR_NO_MORE_FILES` returns explicit `done=true` with an empty entry list. Entry cap is 1..1024 and overflow fails explicitly without silently truncating. The persistent test creates 1,000 extra files and verifies all 1,002 entries across multiple 64-KiB pages exactly once, plus reparse-entry reporting without traversal, intermediate junction replacement rejection, over-cap failure, same-handle restart, end-of-directory, and HANDLE cleanup. Build exit 0/no warnings; native test eight checks pass; npm_lint and npm_test pass (42 core, 34 format/config, HTTP 401/200, 36 URL-security).

Still an isolated prototype; Production handlers unchanged. Pagination is now exercised but full filesystem adversarial/integration acceptance remains outstanding. Phase 1 remains IN PROGRESS / NOT ACCEPTED.

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

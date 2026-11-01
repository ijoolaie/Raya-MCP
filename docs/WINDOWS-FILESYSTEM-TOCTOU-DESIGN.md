# Windows filesystem TOCTOU hardening â€” design note
Date: 2026-10-26
Scope: C:\Users\Ali\local-mcp-bridge only

## Decision
Do not claim race-free filesystem confinement using the current path-validation helpers. The current implementation canonicalizes and checks paths, but later invokes path-based Node filesystem APIs. Validation and use are separate operations, so a concurrently writable ancestor can theoretically be replaced between them.

No production filesystem code is changed by this note.

## Threat model
An untrusted local process can modify a directory or reparse point inside a configured workspace while the bridge is running. The attacker attempts to swap a checked directory for a junction/symlink between validation and open/read/write/rename. Static malicious links are already rejected in tested cases; the concern here is mutation after the check.

## Code inspection findings (2026-10-26)
The current Node implementation was read directly in `server.mjs` and `advanced-tools.mjs`. `server.mjs` validates with `checkedPath()` and then uses `fs.stat`, `fs.readFile`, `fs.access`, `fs.mkdir`, and `fs.writeFile`. `advanced-tools.mjs` validates with `securePath()` and then uses path-based `fs.stat/readFile/writeFile/rename/mkdir/readdir`; `walk()` enumerates by directory path and later search opens each entry by path. `noSymlinkAncestors()` uses `lstat()` but runs before the later operation, so it also has a check/use gap. The affected surface is therefore broader than read/write: list/search, create-directory, metadata, multi-file read, move/rename, edit, and file-tail operations need a reviewed policy.

The declared runtime is Node 22.23.2 on Windows. package.json does not declare a filesystem handle-relative/native security dependency. The observed @napi-rs packages are for canvas, not filesystem confinement. No production dependency or production native code has been added. On 2026-10-26, an isolated N-API probe was compiled directly with MSVC 19.51.36248 against the matching Node v22.23.2 x64 headers and node.lib; Node loaded it and it returned N-API version 10. The bundled node-gyp 11.5.0 could not identify Visual Studio Build Tools 18, so this probe bypassed node-gyp and proves only that a basic native addon can be built and loaded. Native filesystem confinement remains design investigation, not implementation.

## Isolated handle-relative proof of concept (2026-10-26)

A separate native addon prototype now walks descendant path components by opening each child relative to the held parent directory handle (`NtCreateFile`, `OBJECT_ATTRIBUTES.RootDirectory`, `OBJ_DONT_REPARSE`). It rejects reparse-point components and mismatched file/directory types, obtains final file size from the opened handle, and supports a bounded read (maximum 1 MiB) from that same final handle. Tests passed normal nested open, bounded content read, read-cap rejection, dot-dot rejection, missing target rejection, final-directory rejection, static junction rejection, and configured-root-as-junction rejection.

The isolated race harness uses explicit synchronization barriers rather than a fixed delay. For the descendant swap, the native probe calls a test-only callback after opening the first child directory relative to the held parent handle; the worker signals `PARENT_HANDLE_OPENED` and blocks until the parent replaces the old path with an outside-root junction. For the configured-root-chain swap, the worker similarly signals after opening the `root-race` component; the parent swaps the next component to an outside-root junction and the native root walk must fail closed. Prior isolated runs passed both scenarios. These are narrow read/open proofs, not blanket security guarantees.

### Harness cleanup review — 2026-10-29

A source review on 2026-10-29 found cleanup gaps in `.native-probe/handle-relative-test.cjs`: child workers were not tracked by the outer `finally`, the root-chain junction/renamed directory were not comprehensively restored, and cleanup errors were swallowed. The harness has now been updated to retain both child-process references, terminate and await workers in `finally`, remove only verified junctions whose targets match the fixture's outside directory, restore renamed directories, and surface cleanup failures (including alongside the primary test failure). The package `check` script now syntax-checks both native probe scripts. `npm_lint` passed after these edits. The synchronized native race harness was subsequently executed after the cleanup change, including controlled assertion-failure, timeout, and simulated junction-creation-denial paths; see “Harness cleanup follow-up” below. Those results verify only the exercised paths, not every OS-level failure, and do not justify unattended CI promotion or production integration.

Limitations remain material: the isolated prototype now opens the drive root and walks the configured root component-by-component using held directory handles, and a synchronized configured-root-chain swap test fails closed. This narrows the specific full-path root-open gap in the prototype; it does not establish complete confinement. The prototype opens and queries metadata and supports capped reads only; it does not write, create or rename files, enumerate directories, enforce no-overwrite publication, verify volume identity, or cover all tool families. The optional delay parameter remains test-only; the current race harness uses explicit callback/stdin barriers. Nothing from this prototype is connected to production handlers or the package manifest. Production status remains NOT ACCEPTED as race-free.

## Harness cleanup follow-up — 2026-10-29

The cleanup path tracks each worker's `close` event immediately after spawn and records the closed state in a `WeakSet`; it does not rely on `proc.closed`. On 2026-10-29 the native harness passed normal execution twice before the fault-injection work, exercising both synchronized swaps. It was then tested with controlled test-only fault hooks: (1) injected assertion failure after the descendant worker synchronized exited 1 as expected and cleanup left no new fixture; (2) a suppressed parent-handle signal with a 250 ms test timeout exited 1 with the expected timeout and cleanup left no new fixture; (3) simulated descendant junction-creation denial restored the renamed directory, reported that swap test as skipped, completed the independent root-chain race successfully, and exited 0. A subsequent normal harness run exited 0 and passed both swap scenarios. The workspace listing after the fault tests showed only three pre-existing `fixture-*` directories; these were left untouched because their provenance and link targets were not fully verified. `npm_lint` and a fresh `npm_test` passed (42 core, 34 format/config, HTTP auth, 36 URL-security checks). These results validate the exercised cleanup paths but not every possible OS-level failure. TOCTOU remains NOT ACCEPTED as race-free; do not add the harness to unattended CI pending broader operation-family coverage.

## Candidate implementation approaches
1. Native Windows handle-based module (preferred for a strong guarantee).
   - Open the configured root and each descendant through Win32 handles with explicit reparse-point policy.
   - Verify final resolved paths using handle-derived canonical names, not a fresh path lookup.
   - Perform operations against already-validated handles, or use a carefully reviewed relative-open primitive rooted at a held directory handle.
   - Explicitly handle volume/device paths, UNC paths, reparse tags, sharing modes, file replacement, and delete/rename semantics.
   - Package as a small N-API addon with supported Node 22/Windows builds; fail closed if unavailable.
   - Risk: implementation complexity and Windows API subtleties. Do not assume that FILE_FLAG_OPEN_REPARSE_POINT alone prevents traversal through every ancestor.

2. Recheck immediately before operation (temporary defense only).
   - Repeat ancestor lstat/realpath checks immediately before open/write.
   - This reduces the window but does not eliminate the race and must not be described as race-free.
   - Not selected as a security closure.

3. Operational mitigation (available now).
   - Keep workspace roots narrow and stable.
   - Ensure only the trusted bridge user/admin can write to roots and their parent chain; do not share workspace parent directories with untrusted users/processes.
   - Run with least privilege; PowerShell remains arbitrary, unsandboxed execution when enabled.
   - Treat these controls as mitigation, not a code-level proof of confinement.

## Next primitive: create-new relative to a held parent (design contract, 2026-10-29)

Do not implement this by validating a full destination path and then calling path-based write or rename APIs. The first write-capable prototype should be a separate, isolated native operation with the following contract:

1. Resolve the configured root and every parent component using the same held-handle walk and fail-closed reparse policy as openRelative; retain the final parent directory handle until the child operation and verification are complete.
2. Accept only a validated relative parent path plus one final filename component. Reject empty names, dot/dot-dot, separators, colon/ADS syntax, device/UNC syntax, trailing dot/space aliases, and unsupported Windows name forms before invoking the native open.
3. Create the final file relative to that parent handle via NtCreateFile with OBJECT_ATTRIBUTES.RootDirectory set to the held parent, OBJ_DONT_REPARSE, FILE_CREATE disposition (not FILE_OPEN_IF or FILE_OVERWRITE_IF), FILE_NON_DIRECTORY_FILE, and minimum required access. FILE_CREATE is required to preserve strict no-overwrite semantics when a name already exists. Treat a collision as failure; never silently overwrite.
4. Keep sharing and access masks minimal and explicit. Reject a final handle that reports a directory or reparse-point attribute. Do not assume FILE_OPEN_REPARSE_POINT alone protects ancestors; the ancestor walk and final relative operation are separate required controls.
5. For the first prototype, accept only a small explicit byte buffer (hard cap 1 MiB) and write through the returned file handle. On short write or any post-create verification failure, close handles and attempt cleanup only through a verified parent handle and the exact created entry; surface cleanup failure rather than masking it. Never fall back to a path-based delete or write.
6. Return success only after the complete byte count is written and final-handle metadata has been checked. Tests must prove that a pre-existing target remains byte-for-byte unchanged, a new file is created in the held in-root parent despite an ancestor rename, and an attempted reparse-point/parent swap fails closed or stays anchored to the already-open parent. Test write failure, collision, cleanup failure, worker timeout, and handle-count stability.
7. Keep the primitive under .native-probe and out of production handlers, package runtime dependencies, and unattended CI until the API semantics, cleanup behavior, and race tests have been reviewed on the supported Windows/Node version.

This contract is a design gate, not an implementation claim. The native prototype currently supports open/metadata/bounded-read only; no create-new operation exists yet. Before coding, confirm exact SDK constants and the observed NtCreateFile status mapping on the supported Windows build, and make each negative test assert both the expected error and that no outside canary was changed.

## Required acceptance tests before closure
- Static junction/symlink escape: blocked for read, write, list, move, and search.
- Deterministic race harness using a disposable in-root directory, an outside-root canary directory, and a concurrent reparse-point swapper. The isolated prototype now covers one synchronized descendant-ancestor swap and one configured-root-chain component swap, both fail-closed/in-root as expected. Extend coverage to all relevant operation families and assert no outside canary read or write ever succeeds; test must be repeatable and fail closed.
- File replacement and rename races, including existing-target overwrite and create-new semantics.
- Missing-path suffixes, dangling links, nested reparse points, UNC/device path forms, and root itself being a link.
- Verify cleanup of every temporary junction, file, child process, and fixture even on test failure.
- Run npm run lint, npm test, and live acceptance on Windows after each implementation change.

## Go/no-go
Current state: NOT ACCEPTED as race-free. Static reparse-point escape denial and atomic no-overwrite create are useful but distinct controls. The isolated race harness now has an explicit synchronization barrier, but do not add it to the normal suite until cleanup-on-failure and broader edge-case coverage are reviewed. Do not add a native dependency until the implementation can be reviewed and tested on the supported Windows/Node matrix.

## Native rerun and parent-handle ownership gate — 2026-10-30

The isolated build script was rerun through the managed process runner on 2026-10-30. MSVC compilation succeeded and the harness exited 0. It passed the existing parser/root-chain rejection and handle-count checks, bounded read and size-cap checks, static reparse checks, synchronized descendant swap (read remained on the held in-root directory), and synchronized configured-root-chain swap (failed closed). `npm_lint` also exited 0. The `npm_test` check was blocked by the local tool safety layer; therefore there is no fresh full-suite result for this rerun. No production source, package dependency, or handler was changed.

Source review confirms a critical ownership limitation before any write-capable extension: `openRelative` closes each previous directory handle as it advances and ultimately retains only the final target handle. It does not return or retain the final parent handle. Therefore it is not safe to add create/write by simply appending an NtCreateFile write call to the current routine. The next isolated step must introduce a separate parent-walk primitive with an explicit ownership-transfer contract (caller owns the returned parent handle on success; helper closes all intermediate handles on every failure; one clear close/finalizer path), and test held-handle lifetime, rename/swap anchoring, and repeated success/failure handle-count stability without performing any write. Only after that lifetime contract is verified should a separate create-new experiment be considered. The 2026-10-30 result does not change the no-go decision: production filesystem confinement remains NOT ACCEPTED as race-free.

## Parent-handle ownership primitive — 2026-10-30

An isolated no-write parent-handle API was added to `.native-probe/handle-relative-probe.c`:
- `openParentRelative(root, relativeParent)` walks each parent component relative to a held directory handle and transfers the final directory HANDLE into a N-API external owner.
- The external finalizer closes an unclosed handle; `closeParentHandle(parent)` provides an explicit idempotent close path; `inspectParentHandle(parent)` reads directory/reparse metadata and volume/file identity from that same held handle.
- `readChildFromParent(parent, childName, maxReadBytes)` was also added as an isolated bounded, one-component, handle-relative read helper. It compiles, but the harness does not yet exercise it; treat it as IMPLEMENTED/UNTESTED, not accepted.
- Harness evidence: held directory file ID remained identical after the original directory was renamed and its old name replaced by an outside junction; explicit close and double-close behaved as expected; inspection after close failed; 300 missing/dot-dot/trailing-separator failures plus the successful open/close path showed no process-handle-count increase.
- The deterministic test verified the handle's identity/lifetime across rename and junction replacement. It did not yet prove a child open/read through that held parent; add that test before accepting the full parent-relative operation contract.
- Native build/harness exited 0 after the change. `npm_lint` exited 0. A fresh `npm_test` exited 0: 42 core checks, 34 format/config checks, HTTP auth 401/200, and 36 URL-security checks.
- One attempt to extend the harness to exercise `readChildFromParent` was blocked by the local safety layer. No alternate edit path was used. This helper remains untested.
- No production handler, package dependency, runtime setting, service/tunnel process, or unrelated workspace was changed.

## Parent-relative child-read acceptance update — 2026-10-30

The previously untested readChildFromParent helper is now exercised by the isolated harness:
- Opened parent-safe through openParentRelative, renamed the directory to parent-held, and installed an outside junction at the old parent-safe name.
- Read marker.txt using the retained parent handle. The returned bytes were exactly PARENT-IDENTITY (15 bytes); the outside marker remained OUTSIDE-MARKER.
- Rejected traversal/multi-component names, a missing child, a too-small read cap, a directory child, and a child junction. The child junction was successfully created on this host and the native open rejected it (not skipped).
- Repeated successful reads and missing/cap failures 25 times; process handle count remained stable, showing child handles were closed on the exercised success/failure paths.
- Existing explicit parent close, double-close, inspect-after-close rejection, 300 invalid/missing/trailing parent-path failures, and parent file-identity stability after rename/junction replacement continue to pass.
- Latest isolated MSVC build/harness exited 0, including existing static junction, synchronized descendant swap and configured-root-chain fail-closed tests. Fresh npm_lint and npm_test exited 0 (42 core, 34 format/config, HTTP auth, 36 URL-security checks).

The N-API external finalizer is implemented but its fallback close path has not yet been independently verified with a deterministic GC/finalization test. Explicit close is verified. No create/write operation was added; no production handlers or dependencies were changed.

### N-API ownership/API review follow-up — 2026-10-30

A further source review after the finalizer pass identified API-contract items that must be closed before any create/write primitive:
- The parent owner is stored as a writable JavaScript property (`_nativeParentHandle`) containing an N-API external. Replacing that property can make the wrapper unusable and allow the external to finalize/close the handle while the wrapper still exists. Copying the external to another wrapper creates aliases that share one close state. This is not evidence of an escape, but the ownership API is mutable/aliasable and should be hardened (for example, N-API object wrapping/unwrapping) or explicitly constrained and tested before it becomes a reusable primitive.
- Several `CloseHandle` calls on intermediate/final child handles are unchecked. The explicit parent close path reports failure, but read/metadata paths can currently return success even if a close fails. This is a resource-lifecycle observability gap; review whether close failure should fail the operation and how to avoid double-close/retry ambiguity.
- The child-name validation rejects separators, colon, dot components, and overlong components, but does not yet explicitly reject Windows trailing-dot/space aliases and reserved device basenames. This does not currently demonstrate traversal, but must be resolved before create-new semantics are designed.
- The current finalizer test proves one deterministic GC path on the present Node 22 x64 host, not environment teardown, process abort, or adversarial JS wrapper mutation.

These are review findings, not claimed fixes. No native source was changed in this continuation. The next step is to harden/test the wrapper ownership contract, audit all close/error paths, and define a strict Windows leaf-name policy. Only after that review should the isolated `FILE_CREATE` experiment begin.

Current gate: **PARENT-RELATIVE READ PATH TESTED; PHASE 1 STILL NOT ACCEPTED**. The finalizer path was independently verified in the next update below. Production filesystem confinement remains **NOT ACCEPTED as race-free**.

## Parent external-finalizer verification — 2026-10-30

Added a deterministic child-process check to `.native-probe/handle-relative-test.cjs`. The child runs Node with `--expose-gc`, creates a disposable root/parent directory, records `GetProcessHandleCount`, opens a parent through `openParentRelative`, and verifies exactly one HANDLE was transferred (164 -> 165 on this run). It then drops the only JS reference, explicitly invokes GC, yields to the event loop, and requires the N-API external finalizer to return the process handle count exactly to baseline (165 -> 164). The probe exited 0 on the first collection cycle. The child process is bounded by a 10-second timeout; on timeout it is killed and reaped before the harness fails. Its temporary directory is removed in a `finally` block.

The complete native build-and-harness script then exited 0. The reported outcomes include the new external-finalizer test, held-parent child read after rename/outside-junction replacement, child reparse rejection, repeated child-handle cleanup, explicit/double close, the existing descendant race staying in-root, and configured-root-chain race failing closed. Fresh `npm_lint` exited 0. Fresh `npm_test` exited 0 (42 core, 34 format/config, HTTP unauthorized/authenticated initialization, 36 URL-security checks).

This verifies the exercised N-API external finalizer path on the current Windows/Node 22 x64 host; it is not proof of every environment-shutdown or process-abort scenario. A subsequent harness review found that the finalizer subprocess originally ran before the fixture's outer `try/finally`; it was moved inside that scope so its assertion/timeout failures also trigger cleanup. The timeout handler now kills and awaits the child's `close` event before reporting failure. After this correction, the native build/harness, `npm_lint`, and `npm_test` were rerun and all exited 0. No create/write primitive, production handler, dependency, service/tunnel, or runtime setting was changed. Phase 1 remains open: review remaining ownership/API edge cases and broaden operation-family race coverage before any create-new experiment or production integration.

## N-API wrapper and Windows leaf-name hardening — 2026-10-30

The isolated parent-handle owner now uses `napi_wrap` on the returned JS object and `napi_unwrap` in every parent API. The prior writable `_nativeParentHandle` property and N-API external alias route were removed. Harness assertions mutate and delete a legacy-looking property and verify the original wrapper still inspects successfully; copying that property to a plain object does not produce an unwrap-able alias. The native finalizer remains attached to the wrapped object and the existing deterministic GC finalizer test still passes.

The one-component child-name policy now rejects:
- Empty/overlong names, dot components, separators, colon/ADS syntax, control characters, and Win32-forbidden punctuation.
- Names ending in dot or space.
- Reserved DOS device basenames CON, PRN, AUX, NUL, CONIN$, CONOUT$, COM1–COM9 and LPT1–LPT9, including superscript ¹/²/³ COM/LPT variants; reserved basenames are checked before extensions and with trailing dot/space normalization in the basename.

The harness includes mutation/deletion, alias rejection, trailing dot/space, reserved device, superscript device, and punctuation cases. The final close of the bounded child-read success path now invalidates the local variable before calling `CloseHandle` and reports failure rather than returning a successful read if that close fails. Native MSVC build/harness exited 0; `npm_lint` and `npm_test` exited 0 (42 core, 34 format/config, HTTP auth 401/200, 36 URL-security checks).

### Remaining close/lifecycle gate

This change does **not** close the entire handle-cleanup review. Unchecked `CloseHandle` calls remain in root-chain walks, intermediate directory transitions, and some failure cleanup paths. The N-API finalizer cannot return a close failure to the original caller, and the harness has no deterministic mechanism to inject a `CloseHandle` failure. These are unresolved lifecycle/observability questions, not accepted behavior. Before any create-new primitive, audit each close site, define fail-closed cleanup semantics without ambiguous double-close/retry, and determine how failures are observed or explicitly bounded. Production handlers remain unchanged and production filesystem confinement remains **NOT ACCEPTED as race-free**.

### Close-site inventory for the next review

A source-level inventory after the wrapper/leaf hardening found the following classes; none of the unchecked entries should be interpreted as accepted:
- `open_root_chain`: closes the drive-root handle on type/parser/open failures; closes the previous directory during each successful component transition; closes the new and previous handles on type failure; closes the current handle on test-callback and repeated-separator failures. These calls currently ignore their return values.
- `open_relative`: closes each prior directory handle during descent, the final handle on success, and current/next handles in shared failure cleanup. These calls currently ignore their return values.
- `open_parent_relative`: closes each prior directory handle during descent and current/next/allocated-owner handles in failure cleanup. These calls currently ignore their return values.
- Parent owner finalizer: best-effort closes the owned handle and frees the owner; a finalizer cannot report failure synchronously to the JS caller.
- Explicit `close_parent_handle`: checks `CloseHandle` and only marks the owner closed after success; failure is surfaced as an exception. This behavior is covered only for ordinary success/double-close, not injected OS close failure.
- `read_child_from_parent`: checks the normal success-path close before returning; its failure cleanup still makes a best-effort unchecked close.

The next work item is to specify what a failed close means for ownership and the operation result, then refactor each close site to one consistent helper/ownership transition. Avoid blindly retrying a failed close: the code must not risk closing a reused handle value. Add a deterministic close-function indirection for tests only if it can be kept isolated from production behavior and prove both no false success and no double-close. Until then this gate remains open.

## Close lifecycle hardening update — 2026-10-30

The previous inventory above is superseded by this implementation update. Every direct close site in the native prototype now routes through `close_handle_once(HANDLE *)`. The helper:
- Copies the numeric HANDLE locally and immediately sets the owning slot to `INVALID_HANDLE_VALUE` before calling the OS.
- Never retries a failed close, avoiding a second close against a potentially reused numeric handle.
- Increments an interlocked aggregate `closeFailureCount` on actual or injected close failure.
- Returns success/failure so ownership transitions can fail closed.

Intermediate transitions in `open_root_chain`, `open_relative`, and `open_parent_relative` now abort instead of proceeding if releasing the previous directory handle reports failure. Explicit parent close marks ownership consumed before attempting the close and throws if the helper reports failure. Finalizer and cleanup paths are best-effort because they cannot replace the original exception or synchronously return an error; any failure is counted and queryable through the aggregate counter. The finalizer still frees the owner after the one close attempt, never retries.

### Deterministic fault-injection evidence and limitation

The isolated native-probe addon exposes a test-only one-shot hook. It performs the real `CloseHandle` and then reports a synthetic failure status, allowing deterministic validation without intentionally leaking a live OS handle. Tests prove:
- Explicit parent close throws, increments the counter once, returns process handle count to baseline, and a second close does not retry.
- Finalizer close failure increments the counter while the finalizer returns process handle count to baseline.
- Root-chain intermediate close failure aborts the walk, increments the counter, and leaves no process-handle leak.

This is a post-close status-failure simulation. It validates wrapper ownership, error propagation, counter observability and no-double-close semantics, but it does **not** reproduce every possible kernel/handle-table failure in which the OS may leave handle state uncertain. Cleanup-path close failures remain best-effort with aggregate observability; the original operation error remains primary. Native MSVC build/harness passed with these tests. Full npm lint/test passed before the final test-output-label-only edit; rerun before any phase boundary.

This closes the immediate close-lifecycle code-review gate sufficiently to begin a separate isolated `FILE_CREATE` experiment. It does not accept Phase 1, does not create/write files through production handlers, and does not change the status: production filesystem confinement remains **NOT ACCEPTED as race-free**.

## Initial handle-relative exclusive create experiment — 2026-10-30

Added isolated native-probe API `createChildExclusive(parent, childName)`. It unwraps the parent object, requires a live held directory HANDLE, applies the same strict one-component leaf policy, and calls `NtCreateFile` with:
- `OBJECT_ATTRIBUTES.RootDirectory = parent->handle`
- `OBJ_CASE_INSENSITIVE | OBJ_DONT_REPARSE`
- `CreateDisposition = FILE_CREATE` (no overwrite/open-existing fallback)
- `FILE_NON_DIRECTORY_FILE | FILE_OPEN_REPARSE_POINT | FILE_SYNCHRONOUS_IO_NONALERT`
- minimal write-data/read-attributes/synchronize access for a zero-byte file creation

The returned child HANDLE is checked for reparse/directory attributes and closed through `close_handle_once`. The API returns only after the close reports success. On a name collision, the native status is surfaced as an error; no open-existing or truncation path is attempted.

### Initial harness evidence

- Existing `collision.txt` contains `DO-NOT-OVERWRITE`; exclusive create rejects it and a readback proves the bytes are unchanged.
- Collision failure leaves process HANDLE count unchanged.
- Trailing-dot and reserved-device names are rejected before native create.
- After opening the parent HANDLE, the harness renames the parent directory and installs an outside junction at the old path. Calling `createChildExclusive` through the held parent creates a zero-byte file under the renamed original directory; no file appears in the outside junction target.
- Successful create returns process HANDLE count to baseline.
- Native MSVC x64 build/harness exited 0; `npm_lint` and full `npm_test` exited 0 (42 core, 34 format/config, HTTP auth 401/200, 36 URL-security checks).

### Remaining create-new acceptance work

This was the initial create proof. The 2026-10-30 acceptance expansion below closes several of its listed test gaps, but does not complete the operation-family acceptance matrix. Child file ID/volume identity readback and broader race schedules remain outstanding. No write-content, overwrite, rename, delete, directory-create or production handler integration is implemented. Phase 1 remains IN PROGRESS; production filesystem confinement remains **NOT ACCEPTED as race-free**.

## FILE_CREATE concurrency, lifecycle and name-edge acceptance — 2026-10-30

The isolated harness was expanded and rebuilt with MSVC x64 using a separately named acceptance addon. The previous probe binary was not overwritten because a timed-out earlier harness left it locked; the locked artifact was preserved. The test uses filesystem ready/go/report files as a barrier so eight independent Node processes reach the same start gate before attempting one leaf.

Verified results:
- Exactly one of eight independent processes successfully created the shared leaf; the other seven received collision errors. The final file is zero bytes.
- Thirty repeated create-success / same-name-collision pairs completed without process HANDLE growth.
- A Unicode leaf containing BMP and supplementary-plane characters was created and read back as a zero-byte file.
- ADS colon, `::$DATA`, named-stream `:$DATA`, and control-character names were rejected. An ASCII case-only collision was rejected without replacing the original leaf. Composed `é` and decomposed `e + combining acute` names were distinct on this NTFS test volume; this is a host observation, not a cross-volume normalization guarantee.
- A one-shot injected post-create close-status failure caused the API to throw and increment the close-failure counter; the process HANDLE count returned to baseline, but the zero-byte file remained. This confirms an ambiguous commit outcome: the caller cannot infer that no file was created from the reported error and must not blindly retry using an overwrite-capable path.
- The concurrency harness uses report files and explicit GC (--expose-gc) to avoid retaining child-process stdio/process handles and to make HANDLE-count comparisons deterministic.

The full native harness exited 0. npm_lint and npm_test exited 0; npm test reported 42 core checks, 34 format/config checks, HTTP unauthorized/authenticated initialization checks, and 36 URL-security checks.

Still required: open/read the created child by handle and compare its volume/file ID to a trusted readback; test case-folding and normalization collisions, ADS aliases and additional Unicode edge cases; expand parent/leaf swap schedules and failure cleanup; then continue the read/write/mkdir/list/search/metadata/move operation-family race matrix. These results are isolated prototype evidence only; no production handler integration or race-free production claim is authorized.

## Created-child identity readback — 2026-10-30

Added isolated native-probe API `inspectChildFromParent(parent, childName)`. It opens the leaf relative to the held parent HANDLE with `OBJ_DONT_REPARSE`, `FILE_OPEN_REPARSE_POINT`, and `FILE_NON_DIRECTORY_FILE`; queries `FILE_ID_INFO`, `FILE_ATTRIBUTE_TAG_INFO`, and file size from that same child HANDLE; then closes through `close_handle_once`. It returns the volume serial, 128-bit file ID, size, regular-file flag and reparse flag.

The harness now verifies the newly created file has a nonzero 128-bit ID, is a regular non-reparse file, has size zero, resides on the same volume as the held parent, and returns identical volume/file identity on repeated handle-relative readback. Missing leaf, traversal syntax and ADS-colon leaf names are rejected. These checks ran after the parent directory was renamed and its former path replaced with an outside junction, so the identity readback still resolves through the original held parent.

MSVC x64 build plus the complete native harness exited 0 after this addition. `npm_lint` and `npm_test` also exited 0 (42 core, 34 format/config, HTTP auth/initialize, 36 URL-security). The harness additionally reads the created file through Node `fs.statSync(path, { bigint: true })` and verifies `stat.ino` equals the low 64 bits of the native 128-bit `FILE_ID_INFO` identifier (decoded in Windows little-endian byte order), providing an independent API-level identity cross-check on this test volume. Further parent/leaf replacement schedules and failure-cleanup race coverage remain open. Phase 1 remains NOT ACCEPTED; no production integration.

## Synchronized held-parent create race evidence — 2026-10-30

A standalone isolated test now coordinates a worker after `openParentRelative` has returned a held directory HANDLE. The parent test process renames the original directory and installs an outside-root junction at the former path before allowing the worker to call `createChildExclusive`. The file appears only under the renamed original directory; outside/in-root sentinels remain unchanged. A second run injects a worker failure after parent acquisition and path replacement; the worker's `finally` closes the HANDLE and no outside file appears. Worker-local HANDLE counts return to their pre-open baselines. This strengthens evidence for the isolated zero-byte `FILE_CREATE` operation only. It does not establish concurrent leaf replacement behavior or production filesystem confinement. No production handler was changed; Phase 1 remains NOT ACCEPTED.

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

An isolated native descendant-open and directory-page enumeration prototype has been built and tested. Persistent harness: `.native-probe/handle-relative-dirwalk-test.cjs`; command: `npm run test:native-dirwalk-race`. Verified nested handle-relative opens, expected listing, post-acquisition intermediate junction replacement rejection, reparse-entry metadata without traversal, explicit over-cap failure plus restart by reopening, and HANDLE cleanup. Enumeration validates the full OS page and record boundaries; it no longer silently truncates at `maxEntries`. MSVC x64 build, native harness (seven checks), npm_lint, and npm_test pass. npm_test reports 42 core, 34 format/config, HTTP 401/200, and 36 URL-security checks.

Scope caveat: enumeration is one 64-KiB OS page per call, with no continuation token. On over-cap failure the cursor is consumed; reopen the directory handle to restart. Isolated prototype only; Production handlers unchanged and Phase 1 NOT ACCEPTED.

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

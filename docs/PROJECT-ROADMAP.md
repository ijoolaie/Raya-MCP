# Project Roadmap — Raya Local MCP Bridge

Last updated: 2026-10-30
Authority: this file is the phase sequence; CURRENT-STATUS.md is the live snapshot; HANDOFF.md is the chronological transfer record.

## Mission
Deliver a reliable, secure, useful Windows local MCP bridge with practical Desktop Commander-like outcomes. Preserve least privilege, compatibility, bounded operations, lifecycle cleanup, and truthful security claims.

## Execution policy (binding)
1. Each phase is a deliverable with explicit scope, outputs, dependencies, and acceptance criteria.
2. Execute phases pragmatically: avoid unnecessary process, ceremony, or premature blockers. Make small, reversible changes and keep momentum.
3. Pragmatic execution never lowers security, correctness, data-integrity, or acceptance standards.
4. At the end of EVERY phase, run a comprehensive, adversarial, phase-relevant acceptance suite. Include positive, negative, boundary, concurrency/race, failure/cleanup, compatibility, and regression cases where relevant.
5. A phase is not complete merely because implementation or a basic test passes. Record exact commands/checks, environment, results, skipped/blocked tests, limitations, and residual risks.
6. Keep documentation continuously synchronized. At minimum update CURRENT-STATUS.md, PROJECT-ROADMAP.md, and HANDOFF.md as work progresses and especially at every phase boundary. Update relevant design, implementation, parity, and user docs when behavior or evidence changes.
7. Before moving to the next phase, create a handoff containing the accepted deliverables, test evidence, open defects/risks, exact next action, and constraints. The next phase starts from that handoff.
8. Use statuses consistently: Planned, Implemented, Tested, Verified, Not accepted, Not applicable. Never promote a status without evidence. A blocked test is not a pass.
9. If a phase fails acceptance, keep it open, record failures, fix them, and rerun the full relevant suite. Do not hide or waive failures merely to advance.
10. Do not modify unrelated workspaces, services, tunnels, credentials, or runtime settings. Preserve existing fixtures whose provenance/targets are uncertain. Never invent Git commits, branches, or PRs.

## Phase map

| Phase | Scope | Exit deliverable / gate | Current status |
|---|---|---|---|
| 0. Baseline and architecture | Inventory, threat model, allowed roots, tool map, docs and acceptance policy | Reproducible baseline and approved roadmap | Implemented; ongoing doc maintenance |
| 1. Filesystem confinement | Windows handle-relative primitives; parent-handle ownership; race-resistant operation semantics | Comprehensive operation-family race/escape/cleanup evidence; production claims only for accepted operations | In progress; NOT ACCEPTED as race-free |
| 2. Safe filesystem operations | Read/write/edit/create/list/search/metadata/move and multi-file operations | Per-operation security and regression acceptance; no path-based unsafe fallback | Planned |
| 3. Process and command lifecycle | Launch, sessions, output, timeout, cancellation, safe termination, working directory | Controlled process lifecycle, permission and leak tests | Planned |
| 4. Developer/workspace capabilities | Workspace discovery, project operations, Git and test workflows, diagnostics | End-to-end workflows with bounded outputs and compatibility tests | Planned |
| 5. MCP management and observability | Tool schemas, roots/config visibility, diagnostics, session/search lifecycle, structured errors | Protocol/schema and connected-client acceptance | Planned |
| 6. Integrated security and reliability | Cross-tool threat tests, load/concurrency, resource exhaustion, recovery, dependency review | Full security/reliability acceptance report and resolved critical/high findings | Planned |
| 7. Setup, UX and compatibility | Installation, configuration, client compatibility, troubleshooting and user docs | Clean-machine setup and supported-client acceptance | Planned |
| 8. Release and maintenance | Versioning, release checklist, known limitations, upgrade/rollback and maintenance process | Release candidate accepted with evidence and residual-risk disclosure | Planned |

## Phase operating rules
- Work one phase at a time unless tasks are demonstrably independent and do not undermine the current phase gate.
- Keep each change narrow and reversible; inspect before editing.
- Run focused tests during development, then the full phase acceptance suite at the phase boundary.
- At each boundary: review diffs/source, inspect unintended changes/secrets, run lint and full tests, run adversarial tests, update docs, record status, and prepare the next handoff.
- If a tool or safety layer blocks a check, record it as blocked and use only an approved, equivalent check; never bypass the control.
- Security-sensitive native code stays isolated until its ownership, API semantics, cleanup, build reproducibility, and operation-family behavior are accepted.
- Production filesystem confinement is NOT ACCEPTED as race-free until each production operation family has evidence tied to the exact handle/operation semantics.

## Current focus
Phase 1. The isolated native prototype now has a tested held-parent child read and zero-byte exclusive create. The create harness synchronizes eight independent processes against one leaf and verifies exactly one winner/seven collisions; 30 repeated create/collision pairs return HANDLE count to baseline; Unicode BMP/supplementary filenames are accepted while ADS-colon/`::$DATA`/named-stream/control-character names are rejected. An ASCII case-only collision is atomic; composed and decomposed acute-accent names were distinct on this test volume. A post-create close-status fault is surfaced and counted, but the file remains committed, explicitly documenting the ambiguous outcome. The test harness uses filesystem ready/go/report barriers and explicit GC for stable process-handle accounting. Native MSVC x64 build/harness passed; npm_lint and npm_test passed (42 core, 34 format/config, HTTP auth/initialize, 36 URL-security). The acceptance addon was built under a separate filename because an earlier timed-out harness retained a lock on the previous artifact; the locked file and uncertain fixtures were preserved. Child FILE_ID_INFO and volume serial readback now pass through the held parent HANDLE, with regular-file/type checks, stable repeated identity, same-volume comparison, nonzero file ID, and missing/traversal/ADS rejection. The handle-derived 128-bit file ID low 64 bits now match Node's independent bigint `stat.ino` readback on the test volume. Latest continuation re-verification (2026-10-30): npm_lint/npm_test exited 0 (42 core, 34 format/config, HTTP auth 401/200, 36 URL-security). MSVC x64 native build/harness exited 0 with `ok: true` and 27 outcome groups. A deliberate injected assertion failure after descendant-worker synchronization exited 1 as expected; the harness `finally` path returned the original assertion error without a cleanup AggregateError, then a clean native build/harness rerun passed. This covers that one assertion-failure cleanup schedule only. Remaining gates: synchronized held-parent FILE_CREATE tests now pass for parent-path replacement, injected post-open worker failure, regular-file leaf insertion/collision, and leaf-junction insertion/collision; all worker-local HANDLE counts return to baseline and fixtures are cleaned. Still required: safe directory-handle/descendant traversal and enumeration, production-equivalent list/search and metadata semantics, then write/edit and move/rename race coverage plus per-family cleanup/fault schedules. Production handlers remain path-based and unchanged; production confinement remains NOT ACCEPTED as race-free. No production integration.

## Document responsibilities
- `CURRENT-STATUS.md`: concise authoritative snapshot, active phase, recent checks, blockers, next action.
- `PROJECT-ROADMAP.md`: phase definitions, sequence, dependencies, binding execution policy.
- `HANDOFF.md`: detailed chronological evidence and exact continuation instructions.
- `AGENTS.md`: mandatory operating rules for every agent/session.
- Relevant `docs/*.md`: technical designs, operation matrices, parity and test details.

## Continuation update — synchronized held-parent FILE_CREATE race (2026-10-30)

A new standalone isolated harness/worker pair now synchronizes after a child process acquires the parent directory HANDLE, replaces the old parent path with an outside-root junction, then releases the worker to call `createChildExclusive`. The worker creates only under the renamed held directory; outside and in-root sentinels remain unchanged. A second run injects a post-open worker failure after the same replacement; the worker exits nonzero as expected and its `finally` closes the parent HANDLE. Each worker's process HANDLE count returns to its own pre-open baseline. The standalone harness exits 0 and removes its temporary fixture in `finally`. Its syntax checks are now included in `npm run lint`. This covers only the isolated zero-byte FILE_CREATE primitive; no production code was integrated. Phase 1 remains NOT ACCEPTED. Next: synchronized leaf replacement/collision and broader operation-family race coverage.

## Leaf-race acceptance update — 2026-10-30

The isolated held-parent `FILE_CREATE` harness now additionally synchronizes two leaf races: a regular-file sentinel is atomically renamed into the target name after parent-HANDLE acquisition (native create collides and preserves bytes), and an outside-root junction is inserted at the target name after acquisition (native create collides without following it or changing outside canaries). Worker-local HANDLE counts return to baseline; worker timeout/error handling reaps the child before fixture cleanup. The standalone harness exits 0 with four outcome groups. `npm_lint` and `npm_test` pass (42 core, 34 format/config, HTTP auth 401/200, 36 URL-security). The native test remains separate from `npm_test`. No production integration. Phase 1 remains NOT ACCEPTED. Next: broaden operation-family races, starting with read and directory creation.

## Handle-relative child-read race update — 2026-10-30

The standalone isolated harness now also synchronizes leaf insertion after parent-HANDLE acquisition for `readChildFromParent`: it reads the exact in-root canary bytes from an inserted regular file, and rejects an inserted outside-root junction without returning the outside secret. Worker-local HANDLE counts return to baseline. The harness exits 0 with six outcome groups; fresh `npm_lint` and `npm_test` pass (42 core, 34 format/config, HTTP auth 401/200, 36 URL-security). This covers only the isolated single-child read helper, not production file_read/multi-read/tail or enumeration/search. Next: extend read semantics and add handle-relative directory creation. Phase 1 remains NOT ACCEPTED.

## Isolated directory-create prototype update — 2026-10-30

Added `createDirectoryChildExclusive(parent, childName)` in a separate native source/addon so the pre-existing locked artifact remains untouched. The API uses `NtCreateFile(FILE_CREATE | FILE_DIRECTORY_FILE | FILE_OPEN_REPARSE_POINT)` relative to the held parent, validates the final directory/non-reparse type, and closes through the consume-once helper. Unique MSVC x64 build exited 0. A separate harness passed four schedules: parent rename/outside-junction replacement, existing-directory collision with content preservation, leaf-junction collision without following the target, and injected post-create close-status failure with the committed directory explicitly recorded as an ambiguous outcome. Worker HANDLE counts returned to baseline. `npm_lint` and `npm_test` pass (42 core, 34 format/config, HTTP auth 401/200, 36 URL-security). Native directory test is manual/separate from `npm_test`. No production handler integration. Next: multi-read/tail and directory-handle/descendant operations, then remaining operation families. Phase 1 remains NOT ACCEPTED.

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

## 2026-10-31 — root HANDLE API and read-only adapter harness

Added the isolated native source/build target `.native-probe/handle-relative-probe-phase1-rootapi.c` and `build-raya-phase1-rootapi.cmd`. Its new `openWorkspaceRoot(rootPath)` export returns an opaque root HANDLE owner, allowing root-level child access. Root and parent path components now use the strict Windows filename validator. The API remains test-only and accepts a path string; any future adapter must resolve that path from configured `ROOTS`, never from caller authority.

The root API harness passed 10 checks, including root-level read/metadata, nested reads, unsafe component rejection, root-directory rename followed by outside-junction replacement while the held root HANDLE continued to return original bytes, and HANDLE count baseline. The native x64 build exited 0.

Added `.native-probe/handle-relative-readonly-adapter-test.cjs`, a JS-only comparison harness. It passed 9 checks covering allowlist root selection, root-level/nested reads, Node-compatible UTF-8 decoding including invalid bytes, the 256-KiB cap, root/file/directory metadata, ordinary file size and creation/modification timestamp parity, read-only mapping on the fixture, outside-root rejection, reparse fail-closed behavior, and HANDLE cleanup. Reparse-point metadata is intentionally rejected until Windows reparse tags can be mapped to Node's symlink semantics without conflating junctions and other reparse types.

All six isolated native suites (the four pre-existing race suites, root API, and read-only adapter comparison) were chained after the final native metadata build and exited 0. Full `npm test` passed (42 core, 34 format/config, HTTP 401/200, 36 URL-security); `npm lint` passed with both new harnesses in the syntax chain. No Production handler, native production source, dependency, runtime setting, root allowlist, or permissions changed. Next: verify reparse-tag parity with Node symlink semantics and test explicit error mapping before expanding adapter fault schedules. Phase 1 remains IN PROGRESS / NOT ACCEPTED.

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

Inspected the installed MCP SDK request lifecycle. Tool handlers receive extra.signal; the SDK aborts it on notifications/cancelled and when the connected transport closes. The Streamable HTTP transport response-stream cancel callback removes its response mapping but does not itself abort the Protocol request-handler controller.

Added three isolated harnesses and package scripts:
- .native-probe/mcp-cancellation-lifecycle-test.mjs / test:native-mcp-cancellation: real MCP SDK tool dispatch proves protocol cancellation notification and transport close abort extra.signal; request HANDLEs close after the awaiting callback resumes (6 checks).
- .native-probe/http-disconnect-cancellation-test.mjs / test:native-http-disconnect: real local Node HTTP + StreamableHTTPServerTransport request confirms cancelling the client response stream while a handler is pending leaves extra.signal.aborted false. HANDLEs close only after the callback eventually settles (6 checks). This is confirmed current behavior.
- .native-probe/http-disconnect-bridge-prototype-test.mjs / test:native-http-disconnect-bridge: test-only adapter prototype buffers/parses the JSON-RPC body and forwards parsedBody, keys disconnect controllers by session ID plus JSON-encoded request ID, aborts on premature HTTP response close, combines that signal with SDK extra.signal via AbortSignal.any, and cleans the registry after request handling. Real HTTP disconnect aborts the combined signal; HANDLEs close and the registry entry is removed (8 checks).

Verification: all three new tests passed; root identity and request-scope suites passed; npm run lint and npm test passed (42 core, 34 format/config, HTTP auth 401/200, 36 URL-security). New harnesses are included in syntax checks. No Production handler/HTTP wrapper, native production source, configured ROOTS, dependency, service, tunnel, runtime setting, or permission changed.

Limits: bridge is test-only, not wired into server.mjs or live MCP tools. Request-body buffering, size/error handling, batches, session/request-ID correlation, normal-response/close races, and compatibility with the production HTTP wrapper need hardening. Cancellation is cooperative and cannot interrupt a synchronous native filesystem call already executing; check at safe operation boundaries and close in finally. Production integration remains NO-GO; Phase 1 IN PROGRESS / NOT ACCEPTED. Live ACL denial acceptance remains open.

Next: harden the disconnect bridge with malformed/oversized body tests, batches, concurrent sessions with colliding IDs, premature request abort, normal completion/close races, cleanup on all error paths and transport shutdown; then extend request-scope adapters to operation families. No Production changes before all acceptance gates, including live ACL denial.


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


## 2026-11-27 Phase 1 research addendum

PR #19 and PR #20 add test-only Broker v2 evidence, but they do not satisfy or waive the Phase 1 gate.

- PR #19 is a Draft fixed-root Broker v2 read-only probe with a dedicated virtual service account and protected-root DACL checks. Its one-host elevated fixture tests passed and were cleaned up; it is not a Production service or an accepted general-purpose filesystem layer.
- PR #20 adds lexical path validation, pinned HANDLE traversal/final-file open, bounded HANDLE-relative directory enumeration, cancellation/reaper experiments, response framing and a message-mode pipe transport test.
- Directory enumeration is not a snapshot: a child create succeeded while the directory HANDLE was held, although a rename of the directory was blocked. Returned names must never be treated as authorization.
- A point-in-time single-link check remains vulnerable to later hard-link creation. The restricted-token protected-root denial evidence does not by itself close the final hard-link threat-model decision.
- Frame transport deadlines, REQUEST/ERROR semantics, request-scoped root/handle binding, operation-family parity and production-equivalent acceptance remain open.
- All 15 production filesystem operations/modes remain NOT ACCEPTED. Phase 1 remains IN PROGRESS / NOT ACCEPTED; production integration is NO-GO.

Latest PR #20 CI after the directory-enumeration pagination fix must complete successfully before the new test suite can be marked CI-tested. A local pass on one Windows x64 / NTFS host is not provider or version compatibility acceptance.


### PR #20 follow-up — request/error contract and deadlines (2026-11-27)
- Latest PR #20 head: `e9e1788a954a572a221160ed7e83a4a0c46bfc9d`; remains Draft, not merged.
- Added REQUEST payload schema: operation code, syntactically constrained rootId, UTF-16LE relative path, zero reserved field; READ_FILE requires a nonempty path and LIST_DIRECTORY permits the configured root. This validates syntax only; it does not bind rootId to configured ROOTS or authorize the caller.
- Added terminal ERROR payload with bounded numeric codes, retryable flag and reserved-zero checks; ACK is required for the terminal error.
- Added overflow-safe monotonic deadline arithmetic: one overall request deadline and each frame budget is the minimum of its per-frame cap and remaining request time.
- Extended actual overlapped named-pipe tests to 23 assertions. Two pending reads were canceled and safely reaped under one 500 ms request deadline with 200 ms per-frame caps; the second frame received only the remaining budget.
- Added request/error contract tests (44) and deterministic deadline tests (20). Message-mode pipe transport now has 28 assertions, including REQUEST and terminal ERROR round-trip.
- All ten native commands passed locally on this host. The preceding GitHub Actions run #56 completed green end-to-end for commit `d559e1d` (all then-configured native tests, npm audit, syntax, modular verification and MCP tests). The new commit `e9e1788` adds two more CI test commands; its own CI run must complete before claiming latest-head CI acceptance.
- The framed message-mode transport remains a synchronous test harness. The deadline arithmetic is tested against real overlapped reads, but it is not yet wired into the message-frame transport/reaper.

## 2026-11-27 root registry/request-context research update

The isolated PR #20 branch now includes a test-only configured-root registry and per-request HANDLE context. This is evidence for rootId lookup and request-scoped resource lifetime, not production authorization: the module does not validate root owner/DACL, authenticate clients, load production ROOTS, or integrate with Node handlers.

The 44-assertion test maps two rootIds to different configured directories, exercises READ_FILE and LIST_DIRECTORY on root and nested paths, rejects unknown roots and invalid paths, enforces file size caps, verifies active-context shutdown refusal and closes request-owned handles. The local configured-root symbolic-link test was skipped due privilege restriction; latest CI run #59 is expected to exercise it.

Phase 1 remains IN PROGRESS / NOT ACCEPTED. All 15 production filesystem operations/modes remain unaccepted, and production integration remains NO-GO. The latest PR #20 head is ae0ea46126b4daee5d62e8ea2d5fafb94acabae5; CI run #59 is pending.

## 2026-11-27 read-only vertical-slice evidence

PR #20 now includes a test-only end-to-end message-mode pipe test that exercises the path from REQUEST parsing through configured rootId lookup, request-scoped HANDLE acquisition, file read, bounded RESPONSE_HEADER/DATA/END frames, and ACK-per-frame. The message-mode transport suite now has 86 assertions.

This does not constitute production integration: the message-frame transport is synchronous, the deadline budget is tested separately against overlapped reads, and the test-only root registry does not validate root owner/DACL or authenticate callers. Production ROOTS binding and Node handler integration remain open.

GitHub Actions run #59 completed green for the preceding head ae0ea46. Run #60 (ID 45971616128) is queued for latest head d6a896675f1500a3834879bbed64a68efd43228d.

Phase 1 remains IN PROGRESS / NOT ACCEPTED. All 15 production filesystem operations/modes remain unaccepted.

## 2026-11-27 read-only operation expansion

The test-only REQUEST contract now has five read-only operation codes: READ_FILE, LIST_DIRECTORY, GET_FILE_INFO, READ_FILE_CHUNK and READ_FILE_TAIL. The fixed payload header is 20 bytes and carries a checked 64-bit byte offset plus a bounded 32-bit length. The request context reads file contents with NtReadFile and an explicit offset on the same validated HANDLE; GET_FILE_INFO queries metadata from that same HANDLE. Per-context read/metadata calls are serialized; the caller must stop and join all context users before close. Out-of-range chunks are rejected before a context is returned; tail reads return up to the requested suffix.

Current local evidence: REQUEST/ERROR tests 62; configured root/request context tests 67; message-mode end-to-end transport 109; overlapped message-mode deadline/cancellation reads 27; all twelve native commands pass. This remains isolated research, not production integration.

Not yet covered: workspace_roots/workspace_list, read_multiple_files, filename/content search, write/edit/create/move operations, production root owner/DACL/client identity enforcement, and a persistent heap-owned reaper wired into the full asynchronous framed transport. The overlapped message-mode experiment drains cancellation before returning but still blocks the caller until completion. Phase 1 remains IN PROGRESS / NOT ACCEPTED; all 15 production filesystem operations/modes remain unaccepted.

## 2026-11-27 overlapped message-mode ACK experiment

A separate test harness now applies the monotonic request deadline and per-frame cap to real overlapped message-mode pipe reads. It validates successful framed ACK receipt, a timeout followed by CancelIoEx and terminal completion drain, a second frame constrained by the remaining request budget, ERROR_MORE_DATA and peer disconnect. The earlier test had 27 assertions; the expanded heap-owned reaper version now has 58 assertions and is included in the local twelve-command suite. It tests per-operation heap ownership, return-with-ticket, terminal cleanup, a four-operation outstanding cap, and rejection of a fifth operation.

This does not complete asynchronous transport: the full response TX/RX loop is still synchronous, and the test helper waits without a deadline after cancellation to preserve stack OVERLAPPED/event/buffer lifetime. The heap-owned timeout path now transfers operation records to one reaper thread per timed-out operation, with a four-operation cap. Production integration still needs full framed TX/RX wiring and a decision between shared reaper queue and IOCP.

CI run #63 for d74c23c is fully green. The new helper and its CI step are being added in the next isolated commit. Phase 1 remains IN PROGRESS / NOT ACCEPTED.

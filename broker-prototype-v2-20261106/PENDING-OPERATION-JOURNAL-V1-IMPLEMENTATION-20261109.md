# Pending Operation Journal v1 — Pure C Core Implementation

Date: 2026-11-09
Status: isolated prototype core only; Production NO-GO

## Files added

- pending-operation-journal-v1.h
- pending-operation-journal-v1.c
- pending-operation-journal-v1-test.c
- build-pending-operation-journal-v1-test.cmd

The implementation is a pure C record codec and in-memory state machine. It performs no filesystem, registry, Windows service, ACL, process, network, TLS, cryptographic digest, or Witness operation.

## Record codec

Implements the design's fixed 128-byte v1 record:
- RYPI magic and big-endian uint32 version/state/flags
- 16-byte namespace and operation identifiers
- big-endian expected/next uint64 sequence
- 32-byte caller-supplied request digest
- big-endian witness sequence
- zero-reserved regions
- CRC-32 over bytes 0..111 and complemented CRC

Decode rejects malformed identifiers, unsupported state/version, nonzero flags/reserved bytes, sequence overflow or mismatch, invalid witness-sequence/state combinations, CRC mismatch, and complement mismatch. A failed load retains the exact raw 128 bytes in memory for incident evidence while entering RECOVERY_REQUIRED. CRC is corruption detection only, not authentication or anti-rollback.

## State-machine operations

- initialize an in-memory EMPTY instance;
- load and validate a 128-byte record;
- prepare exactly one PREPARED operation;
- mark WITNESS_COMMITTED only with matching namespace, operation ID, digest, and exact next sequence;
- mark LOCAL_COMMITTED only after WITNESS_COMMITTED and exact local sequence;
- finalize only matching LOCAL_COMMITTED operation;
- classify recovery outcomes for the supplied Witness status, sequence, local epoch, and caller-provided exact-operation-match result.

The classifier is advisory pure logic. The caller must independently authenticate the Witness and establish that its operation-status response exactly matches the stored operation tuple. The exact_operation_match argument is not itself proof.

## Verification

- MSVC x64 compilation with /W4 /WX: PASS.
- Deterministic test suite: 179 assertions per process.
- 25 fresh process executions: 4,475 assertions PASS.
- Test executable and object files created by this build were removed; artifact audit reported zero remaining.

## Explicit limitations and unresolved work

1. This is not a storage adapter. No write/flush/readback ordering or crash durability is implemented or tested.
2. The state machine is in-memory only. finalize clears memory; it is NOT a durable tombstone and must not be interpreted as satisfying the design's durable finalization requirement.
3. No callback fault injection, process-restart recovery, power-loss testing, cross-process lock, or concurrent issuer serialization exists yet.
4. No canonical ADVANCE wire schema or SHA-256 digest implementation is included. Digest bytes are supplied by the caller.
5. No Witness authentication, TLS/mTLS, operation-status client, idempotent request transport, or remote linearizability is implemented.
6. No CNG operation-ID generation, service identity, DACL, handle-bound file access, or audit persistence is implemented.
7. ABORTED is representable in the codec under its strict witness-sequence invariant, but no state transition to ABORTED is exposed.
8. The record can be forged or rolled back by a privileged local actor; CRC is not a MAC or signature.
9. The pure core does not prove the existing epoch ledger can safely reconcile after a remotely committed operation.
10. Recovery classifications require independent integration review before they can drive runtime behavior.

## Decision

This implementation advances only the isolated pure-C prototype. Do not wire it to the remote Witness or generation ledger, create persistent runtime state, install a service, modify ACLs, provision credentials, or enable Production. All original no-go gates remain in force.

## Follow-on: injected storage callback transaction model

A callback-only persistence wrapper was added to the pure C core:
- injected write_record, flush, and readback callbacks;
- candidate state is prepared separately and published to the in-memory journal only after exact readback;
- any callback failure or readback mismatch sets the journal to DEGRADED and retains the attempted candidate record for reload/reconciliation;
- no concrete file, registry, or Windows storage adapter was added.

Verification boundary:
- The previous revision (without these callback wrappers) passed 25 fresh processes x 179 assertions = 4,475 assertions.
- The callback-wrapper revision has NOT compiled or passed tests. The MSVC build stopped with C1085, "No space left on device", while writing pending-operation-journal-v1.obj.
- Only the exact build artifacts belonging to this target were checked and removed; audit reported zero remaining.
- Do not treat the earlier 4,475 result as verification of the new callback code.

The callback interface is still a model, not proof of durable storage semantics. In particular, no real FlushFileBuffers, atomic replace/two-slot layout, tombstone protocol, restart recovery, or power-loss behavior is implemented. Production remains NO-GO.

The callback test source was also extended with a mock store covering write failure, flush failure, readback failure, readback mismatch, and a successful three-transition sequence. These cases are source-only at this point: compilation was blocked by host disk exhaustion, so their assertion count and behavior remain unverified.

## Follow-on review: empty-journal recovery gate

A source review found that `raya_pending_classify()` previously returned `READY` for an EMPTY local journal whenever the supplied local and Witness sequence numbers matched, even if the supplied Witness status was UNKNOWN or NOT_FOUND. That could incorrectly classify an unauthenticated/unresolved Witness observation as ready.

The EMPTY-state classifier now requires `RAYA_WITNESS_COMMITTED` before sequence comparison; UNKNOWN and NOT_FOUND fail closed as `RAYA_RECOVERY_REQUIRED`. Three deterministic assertions were added for UNKNOWN, NOT_FOUND, and a confirmed COMMITTED/equal-sequence case.

This source change and the callback-wrapper revision are both UNVERIFIED: the host has only about 0.32 GB free, and no compilation or test execution was attempted. The earlier 4,475 assertions apply only to the older pre-callback/pre-review revision. Do not treat this patch as validated. Production remains NO-GO.

## Callback revision verification and classifier correction

- The first compile attempt exposed literal `\\n` text introduced in the two source edits; these were corrected to actual source line breaks.
- MSVC x64 compile with `/W4 /WX`: PASS.
- Test executable: 206 assertions PASS.
- 25 fresh process executions of the compiled test binary: 5,150 assertions PASS.
- This validates the pure-C callback mock and classifier source only; it does not validate a real storage adapter or durability.
- Exact target-owned `.exe` and `.obj` artifacts were removed; artifact audit: zero remaining.
- The EMPTY-journal recovery classifier now requires Witness status COMMITTED before sequence equality can produce READY; UNKNOWN and NOT_FOUND return RECOVERY_REQUIRED.

Remaining limitations: no read/load callback, durable tombstone/finalize, atomic file layout, Windows handle/DACL adapter, process-restart or power-loss acceptance, concurrency control, actual Witness integration, or rollback-resistant witness storage. Production remains NO-GO.

## Follow-on: durable tombstone callback path (pure-core model)

- Added `RAYA_PENDING_TOMBSTONED` as a terminal record state. It retains the exact namespace, operation ID, expected/next sequence, request digest, and confirmed Witness sequence; it does not erase the operation evidence.
- Added `raya_pending_store_finalize()`. It accepts only the matching `LOCAL_COMMITTED` operation, constructs a tombstone candidate, then uses the existing write -> flush -> exact readback callback contract. The in-memory journal becomes EMPTY only after all three callbacks succeed.
- On write, flush, readback, or byte-mismatch failure, the journal enters DEGRADED and retains the candidate tombstone bytes for reconciliation.
- `raya_pending_load()` recognizes a valid tombstone as terminal/EMPTY while preserving the exact tombstone bytes in memory. A subsequent PREPARED operation can replace it only through the callback persistence path.
- The legacy `raya_pending_finalize()` is explicitly documented as in-memory compatibility only; it is not a durable finalization API.
- Added tests for successful tombstone persistence, tombstone decode/reload, and injected write/flush/readback failures.

Verification:
- MSVC x64 `/W4 /WX`: PASS.
- Test suite: 234 assertions PASS.
- 25 fresh-process runs: 5,850 assertions PASS.
- Build artifacts are scheduled for exact-target cleanup and audit in this same work session.

Limitations remain material: callbacks are mocks, not a Windows adapter. There is no two-slot/append-only record revision, atomic replacement proof, `FlushFileBuffers` adapter, cross-process lock, process restart/power-loss test, Witness integration, or rollback-resistant local storage. A tombstone in this pure-core model does not itself prove crash consistency. Production remains NO-GO.

Additional tombstone reuse acceptance:
- After loading a persisted TOMBSTONED record, the test now starts a different operation through `raya_pending_store_prepare()` at expected sequence 8.
- Readback confirms the next PREPARED record has expected=8 and next=9; this verifies the pure-core callback flow can reuse a successfully finalized journal without discarding the previous tombstone until the new record has passed write/flush/readback.
- Final MSVC x64 `/W4 /WX` build: PASS; 238 assertions per process.
- 25 fresh-process runs: 5,950 assertions PASS.

## Independent review and Windows adapter design handoff (2026-11-10)

- Reviewed the current journal header/source, v1 design, and implementation report.
- Confirmed the current storage callback interface has no open/read/lock/identity/recovery operations. Therefore the mock callbacks cannot support real restart recovery or reconcile uncertain writes by themselves.
- Confirmed TOMBSTONED loads as in-memory EMPTY while retaining tombstone bytes. This must be paired with coordinator-level comparison against local epoch and authenticated Witness before another operation is prepared.
- Confirmed `raya_pending_store_prepare()` accepts the caller's expected sequence; it does not itself enforce continuity with the previous tombstone or local epoch. This must be checked by a transaction coordinator while holding the cross-process issuance lock.
- Added `WINDOWS-PENDING-JOURNAL-ADAPTER-V1-DESIGN-20261110.md` with a two-slot 512-byte layout candidate, inactive-slot write/flush/readback protocol, handle-bound file verification, service SID/DACL requirements, cross-process mutex behavior, recovery API, and crash/fault acceptance matrix.
- No Windows adapter, file, mutex, service, ACL, service SID, or runtime integration was created. The design explicitly does not equate FlushFileBuffers with universal power-loss durability.
- Production remains NO-GO.

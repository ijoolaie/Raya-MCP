# Windows Pending Journal Adapter v1 — Design

Date: 2026-11-10
Status: design only; no Windows storage adapter or runtime integration
Decision: do not implement a single-file overwrite adapter. First implementation candidate is a fixed-size two-slot file with explicit revision selection, a cross-process lock, handle-bound validation, and fail-closed recovery.

## 1. Review findings from the pure-C core

The current pure-C journal and mock callback tests establish codec and transition behavior only. They do not establish persistent storage semantics.

Specific gaps:
- `raya_pending_store_ops` has write/flush/readback callbacks but no open, read, lock, unlock, identity verification, or recovery contract.
- A failed callback marks the in-memory object DEGRADED, but there is no adapter-level reload operation to reconcile an uncertain write after restart.
- A TOMBSTONED record is exposed as in-memory EMPTY on load. This is acceptable only if the caller also obtains the persisted tombstone sequence and reconciles it with both the local epoch ledger and authenticated Witness before preparing a new operation.
- `raya_pending_store_prepare()` accepts an expected sequence from its caller. It does not independently prove that this sequence follows the last tombstone or current local ledger. That validation belongs in a higher-level transaction coordinator under the cross-process issuance lock.
- The mock flush callback cannot prove that Windows, the filesystem, storage controller, or device has persisted data across power loss.
- CRC32 detects accidental corruption only. It does not authenticate records or prevent rollback.

No code changes to the pure-C core are implied by this review. Production remains NO-GO.

## 2. Storage layout candidate

Use one pre-provisioned file containing two fixed 256-byte slots (512 bytes total). Do not store a native C struct. Each slot is encoded field-by-field in a fixed byte order.

Proposed slot:
- 0..3: magic `RYPS`
- 4..7: format version (1)
- 8..15: monotonically increasing record revision (nonzero uint64)
- 16..23: bitwise complement of revision
- 24..151: 128-byte RYPI journal record
- 152..183: SHA-256 of bytes 0..151 (corruption consistency only; not an authentication tag)
- 184..187: CRC32 of bytes 0..183
- 188..191: CRC32 complement
- 192..255: reserved zero bytes

The two slots are at fixed offsets 0 and 256. A slot is valid only when every invariant, reserved byte, digest, CRC, and embedded RYPI record validates.

Selection rules:
1. Both slots valid with distinct revisions: select the larger revision.
2. One valid slot and one entirely zero/uninitialized slot: select the valid slot.
3. Both slots entirely zero: report UNPROVISIONED only during explicit provisioning; runtime load fails closed.
4. Same revision with different slot bytes, revision overflow, malformed nonzero slot, unreadable slot, or ambiguous ordering: RECOVERY_REQUIRED.
5. Never select a lower revision merely because its state looks more convenient.
6. A valid TOMBSTONED embedded record is terminal evidence, not an instruction to erase the slot.

This layout is a candidate, not a claim of atomic 256-byte writes. It relies on retaining the previous valid slot while writing the inactive slot. The adapter must test torn writes and must not assume sector/page atomicity.

## 3. Transaction write protocol

All operations occur while holding the named cross-process issuance mutex.

For a new revision:
1. Read both slots through the already-open file handle.
2. Validate and select the current highest valid revision.
3. Choose the other slot as inactive; calculate revision+1 with overflow rejection.
4. Construct the entire new slot in memory and validate it locally.
5. Write the full slot at its fixed offset using a loop that handles short writes. Treat zero-byte progress and all errors as failure.
6. Call FlushFileBuffers on the same file handle.
7. Read the exact slot bytes back through the same handle.
8. Validate the complete slot and compare all bytes to the intended image.
9. Only then publish the new revision/state in memory.
10. Retain the previous slot unchanged until a later successful revision supersedes it.

Any write, flush, readback, or comparison uncertainty leaves the coordinator DEGRADED. It must close/reopen through the controlled recovery path, re-read both slots, and reconcile; it must not retry by blindly overwriting the same slot or create a new operation ID.

FlushFileBuffers is a requested durability barrier for the file handle. It is not by itself proof against every controller cache, firmware defect, remote filesystem behavior, VM snapshot rollback, backup restore, or storage-stack failure. Supported filesystems and hardware assumptions must be explicitly bounded and tested.

## 4. File opening and handle-bound security

Provision the journal file out-of-band under a dedicated service-owned directory. Runtime must never silently create, replace, or repair a missing file.

Open requirements:
- Open the pre-existing file with CreateFileW using OPEN_EXISTING.
- Use FILE_FLAG_OPEN_REPARSE_POINT and reject reparse-point attributes.
- Require a regular disk file; reject directories, devices, pipes, and unexpected file types.
- Use restrictive sharing (prefer no sharing where compatible with lifecycle) so another process cannot replace or mutate the file while the adapter operates.
- Verify final path, volume identity, file ID, owner and DACL from the opened handle; do not validate a path and then reopen it.
- Verify the effective service token and service SID at startup; ACL must grant only the dedicated service SID plus explicitly approved recovery principals (for example SYSTEM/Administrators under controlled maintenance).
- Do not accept a caller-supplied arbitrary path after provisioning.
- Revalidate file identity on each reopen and fail closed if volume/file ID/owner/DACL changes.
- Do not follow reparse points or use path-based delete/rename to recover.

The adapter must not depend on a user-writable parent directory. Provisioning and ACL construction are separate, explicitly reviewed work; no service SID or directory is created by this design task.

## 5. Cross-process serialization

Use a named mutex with a stable namespace derived from the provisioned journal identity. Apply a restrictive security descriptor to the mutex and verify the service identity before use.

- Acquire with a bounded wait.
- WAIT_ABANDONED means the previous owner died; it is not permission to continue normally. Re-read and fully validate both slots, local epoch ledger, and authenticated Witness state before any transition.
- WAIT_FAILED, timeout, unexpected ownership, or mutex ACL/identity mismatch is fail-closed.
- Hold the lock across the entire local sequence: read slots, Witness status/advance decision, durable PREPARED write, remote operation, durable WITNESS_COMMITTED write, local epoch ledger commit/readback, durable LOCAL_COMMITTED write, generation publication policy, and tombstone finalization.
- Do not hold the lock across unbounded network waits. The protocol coordinator must use bounded network timeouts and preserve PREPARED state on timeout. If the design later releases the mutex during network I/O, it must add an explicit lease/fencing protocol; a process mutex alone cannot serialize a distributed transaction after lock release.
- A process-local critical section is not a substitute.

## 6. Read and recovery API required before adapter implementation

The storage abstraction must evolve beyond write/flush/readback. Proposed operations:
- open_existing_and_verify(identity_policy)
- acquire_exclusive(timeout)
- read_both_slots(raw[2][256])
- write_inactive_slot(slot_index, bytes[256])
- flush_file()
- read_slot(slot_index, bytes[256])
- verify_open_handle_identity()
- release_exclusive()
- close_handle()

Recovery result must distinguish:
- UNPROVISIONED (only for explicit first-install flow)
- EMPTY_TOMBSTONE (valid terminal record; return its exact sequence and revision)
- ACTIVE_PREPARED
- ACTIVE_WITNESS_COMMITTED
- ACTIVE_LOCAL_COMMITTED
- DEGRADED_IO
- CORRUPT_OR_AMBIGUOUS
- IDENTITY_OR_ACL_CHANGED

The coordinator, not the storage adapter, authenticates Witness responses and compares operation_id, namespace, request digest, and sequence. Storage must never turn a missing/corrupt journal into EMPTY.

## 7. Required crash/fault matrix

At minimum, test process termination and controlled fault injection:
- before inactive-slot write
- after partial slot write
- after full write but before FlushFileBuffers
- FlushFileBuffers failure
- after flush but before readback
- readback I/O failure
- readback mismatch
- process termination after readback but before in-memory publish
- mutex abandonment at every transaction boundary
- one valid slot plus one torn slot
- both slots valid with ordered revisions
- same revision with different contents
- malformed nonzero slot / bad embedded RYPI / bad CRC / nonzero reserved bytes
- revision UINT64_MAX
- file replaced between process lifetimes
- reparse-point substitution attempt
- DACL/owner/service SID drift
- local epoch ahead of journal
- journal ahead of local epoch
- tombstone with Witness unavailable
- tombstone with Witness sequence mismatch
- host crash/VM snapshot restore/backup rollback
- concurrent independent processes attempting issuance

For every injected failure, prove that no new operation ID is issued and no generation is published until recovery has an authenticated, internally consistent outcome.

## 8. Acceptance gates

The adapter is not acceptable until:
1. The exact supported Windows versions/filesystems/storage assumptions are documented.
2. The two-slot codec has independent byte-level tests and fuzz/property tests.
3. Real Windows integration tests validate handle identity, reparse rejection, ACL enforcement, mutex security, and cross-process contention.
4. Power-loss claims are limited to evidence actually obtained; process-kill tests are not power-loss tests.
5. Flush/readback fault injection and restart recovery pass on the supported storage stack.
6. Witness response authentication and operation idempotency are integrated in a separate coordinator test.
7. Restore/clone/VM rollback is handled by the independent monotonic Witness, not inferred from local CRC or revision.
8. No runtime auto-provisioning or automatic reset exists.
9. Security review and explicit approval precede service/root creation or Production wiring.

## 9. Explicit non-goals and status

This document does not create a service, directory, journal file, mutex, ACL, service SID, credential, network endpoint, or Production integration. It does not claim that FlushFileBuffers guarantees power-loss durability on every storage stack. The two-slot design is a testable candidate only.

Production status: NO-GO.


## Slot codec implementation — 2026-11-10

Implemented the pure Windows CNG-backed two-slot codec and selector in `windows-pending-slot-v1.h/.c`, with a separate native test harness and build script. The 256-byte byte-level layout follows the adapter design. SHA-256 is computed using BCrypt; CRC32/complement and reserved-zero checks are also enforced. Embedded RYPI records are validated by the existing journal decoder. Selection fails closed for malformed nonzero slots, same-revision conflicts, and UINT64_MAX revision exhaustion; only byte-identical equal-revision slots are accepted. Both zero slots return UNPROVISIONED, which runtime must reject outside explicit provisioning. MSVC x64 `/W4 /WX` build passed; 300 assertions per fresh process ×25 fresh processes = 7,500 PASS. Implementation report: `WINDOWS-PENDING-JOURNAL-SLOT-CODEC-V1-IMPLEMENTATION-20261110.md`. This is codec-only: no filesystem, FlushFileBuffers, mutex, ACL, service, identity, network, or Production integration. SHA-256/CRC do not authenticate or prevent rollback. Production remains NO-GO.


## Two-slot in-memory fault model — 2026-11-10

Implemented `pending-slot-store-model-v1.h/.c` and tests. The model keeps separate volatile/durable copies and simulates write failure, volatile partial write, partial write reaching durable storage, Flush failure with either persistence outcome, readback failure and mismatch. Commit returns UNCERTAIN and leaves publication outputs unchanged on every uncertain path. Recovery examines only the durable image using the real slot selector. MSVC x64 `/W4 /WX` passed; 65 assertions ×25 fresh processes = 1,625 PASS. Key finding: if a torn partial inactive slot reached durable storage, the prior valid slot remains byte-intact but the selector fails closed because the other slot is malformed/nonzero. This preserves fail-closed policy but requires a separately authorized recovery path; no automatic fallback to lower revision. Flush/readback failures can leave either old or new revision durable, so authenticated Witness/local-ledger reconciliation is mandatory. Report: `PENDING-JOURNAL-TWO-SLOT-FAULT-MODEL-V1-IMPLEMENTATION-20261110.md`. This remains an in-memory model only; no NTFS/FlushFileBuffers/power-loss/ACL/mutex/service/Production test or change. Production remains NO-GO.

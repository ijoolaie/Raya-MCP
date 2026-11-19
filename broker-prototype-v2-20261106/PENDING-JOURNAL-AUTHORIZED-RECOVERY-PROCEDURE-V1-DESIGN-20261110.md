# Pending Journal Authorized Recovery Procedure v1 — Design

Date: 2026-11-10
Status: design only; no recovery code, persistent adapter, operator tool, or runtime integration
Decision: a valid lower-revision slot is evidence, not an automatically selectable fallback, when its peer slot is malformed and nonzero. Recovery requires evidence preservation, independent authority reconciliation, and separately authenticated repair authorization.

## 1. Purpose and invariants

The two-slot selector intentionally returns ERROR when either slot is nonzero but invalid. The in-memory fault model demonstrated that a partial inactive-slot write may reach durable storage while the previously valid slot remains byte-intact. Automatically selecting that lower slot would discard evidence that a newer state may have committed.

Hard invariants:
1. Normal runtime selection remains fail-closed. No fallback flag or permissive selector mode.
2. A malformed slot is never treated as empty; partially decoded fields are never trusted.
3. The valid peer slot is only a candidate record, not proof of latest state.
4. The authenticated Witness is authoritative for namespace sequence and operation outcome. The local ledger is a consistency participant, not an authority over Witness.
5. No generation, new operation ID, or issuance permission is published during investigation or repair.
6. Raw evidence is preserved before any write can overwrite the malformed slot.
7. Repair requires independent authenticated authorization bound to exact evidence and action. Human acknowledgement alone is insufficient.
8. Any ambiguity, unavailable authority, identity drift, or failed I/O leaves the namespace blocked in RECOVERY_REQUIRED.

## 2. Slot observations

Classify each 256-byte observation independently:
- ZERO: all bytes zero. This is an observation, not proof the slot was never used.
- VALID: complete slot decoder validates magic, version, revision/complement, embedded RYPI, SHA-256 consistency digest, CRC/complement, and reserved bytes.
- MALFORMED_NONZERO: at least one byte is nonzero but complete decoder rejects it.
- IO_UNKNOWN: complete bytes unavailable, short/error read, or handle identity changed.

MALFORMED_NONZERO and IO_UNKNOWN must never be downgraded to ZERO. A forensic parser may report candidate revision bytes as UNTRUSTED_HINTS only; they cannot affect ordering or authority decisions.

Pairs:
- VALID+VALID, VALID+ZERO, ZERO+ZERO: existing selector rules only.
- VALID+MALFORMED_NONZERO or reverse: this procedure's recovery case.
- Any IO_UNKNOWN, MALFORMED_NONZERO+ZERO, or MALFORMED_NONZERO+MALFORMED_NONZERO: RECOVERY_REQUIRED; separate recovery design required.

## 3. Entry and containment

Recovery is explicitly invoked maintenance, never boot-time auto-repair.

Before interpreting bytes:
1. Block generation issuance and new pending operations for the namespace.
2. Acquire the approved cross-process exclusion mechanism. Abandoned/uncertain lock requires full reconciliation.
3. Verify effective service identity, namespace binding, opened file identity, volume/file ID, owner, DACL, and non-reparse status from the same handle.
4. Confirm no second Supervisor/clone is authorized to issue. Remote CAS remains mandatory.
5. Record incident ID and operator/change-control reference.

If any identity or handle check fails, stop. Do not open an alternate path, create a replacement file, or use path-based rename/delete.

## 4. Evidence capture before repair

Before any slot write:
- Capture both exact 256-byte images from the verified handle.
- Capture namespace ID, volume/file identity, file size, diagnostic timestamps, service identity/SID, owner/DACL fingerprints, build identity, incident ID, and read status.
- Compute SHA-256 of each slot and the full 512-byte image. Hashes identify evidence; they do not authenticate it.
- Preserve raw images and metadata in an approved append-only/tamper-evident store outside the host backup/VM rollback domain.
- Obtain and independently verify a durable receipt binding incident, namespace, hashes, lengths, and evidence object IDs.

If evidence cannot be durably preserved and independently verified, no repair is permitted. The original malformed bytes must remain available after repair.

## 5. Independent authority reconciliation

Obtain fresh authenticated Witness state and verify TLS/mTLS identity, pinned witness identity/key, signature where required, namespace, nonce/request binding, operation ID, digest, result, and committed sequence.

Load the local epoch ledger without auto-initialization; its own handle identity must be checked under a separately reviewed adapter contract.

Compare namespace, operation ID/digest, expected/next sequence, Witness status/sequence, local ledger epoch, valid slot revision/state, and prior recovery receipts.

Witness:
- COMMITTED is usable only for the exact namespace, operation ID, digest, and next sequence.
- NOT_FOUND does not prove the torn slot was never written or no request is in flight. Resolve only through the exact original operation and Witness idempotency/status contract.
- UNKNOWN, UNAVAILABLE, CONFLICT, invalid signature/binding, or retired/locked namespace: block.
- Different committed operation or sequence mismatch: security incident; preserve evidence.

Ledger:
- Exact expected value for the independently established journal/Witness state: potentially consistent.
- Behind Witness: ROLLBACK_SUSPECTED; no issuance. Catch-up requires separate reconcile API/design and authorization.
- Ahead of Witness: RECOVERY_REQUIRED; local state cannot overrule Witness.
- Unreadable, malformed, ambiguous, wrong namespace, or identity changed: block.

A valid peer's embedded record is not automatically the operation that caused the torn peer. If the intended transition cannot be established from authenticated Witness status/history and durable journal facts, no repair is authorized.

## 6. Single-use repair authorization

After evidence capture and reconciliation, a separate recovery authority may issue a signed, single-use authorization binding:
- policy/protocol version, incident and namespace IDs;
- exact file volume/file ID and owner/DACL fingerprints;
- both raw slot hashes and evidence receipt IDs;
- valid slot index, revision, and exact hash;
- malformed slot index and exact raw hash;
- authenticated Witness identity/key, sequence, operation ID and digest (or explicit no-pending proof);
- local ledger epoch and verified status;
- exact replacement RYPI hash/state, target slot, and new revision;
- permitted action, expiry, unique authorization ID and anti-replay nonce;
- operator approvals and separation-of-duties evidence.

Reject unknown/noncanonical fields, expired/replayed authorization, wrong file/namespace, changed slot hashes, changed Witness/ledger facts, or an action not exactly covered. Signer/key and authorization ledger must be outside the host rollback domain. A local Boolean, command-line switch, or unsigned operator note is insufficient.


## 7. Permitted reconstruction cases

Reconstruct a new complete slot from independently justified state; never patch malformed bytes.

A. Valid peer is PREPARED; Witness confirms exact operation COMMITTED; ledger is exactly at expected_sequence.
- Candidate replacement is canonical WITNESS_COMMITTED for the same namespace, operation ID, digest, and next sequence.
- Coordinator must separately commit/verify local ledger and persist LOCAL_COMMITTED before issuance.
- Timeout or NOT_FOUND is insufficient.

B. Valid peer is WITNESS_COMMITTED; Witness confirms exact operation COMMITTED; ledger is at next_sequence.
- Candidate replacement is LOCAL_COMMITTED only if ledger commit/readback provenance is independently proven.
- A matching number alone is insufficient if namespace/transaction provenance is uncertain.

C. Valid peer is LOCAL_COMMITTED; Witness confirms exact operation COMMITTED; ledger is at next_sequence.
- TOMBSTONED reconstruction requires proven generation publication/audit policy and terminal evidence.
- If publication/audit outcome is unknown, retain active evidence and escalate.

D. Valid peer is TOMBSTONED; Witness and ledger independently confirm the same terminal sequence/namespace.
- A tombstone-equivalent peer may be reconstructed solely to restore redundancy, with exact evidence and signed authorization. It does not authorize a new operation.

No case permits lowering Witness/local epoch, changing digest for an operation ID, replacing an unresolved operation ID, or publishing a generation during recovery.

Valid peer EMPTY/legacy-empty, PREPARED with NOT_FOUND/UNKNOWN, namespace mismatch, or any inconsistent state is outside this procedure: remain RECOVERY_REQUIRED.

## 8. Revision and write protocol

Malformed slot revision bytes are untrusted. Candidate repair revision is valid_peer_revision+1 with overflow rejection. This is local ordering only, not Witness sequence or anti-rollback. Authorization binds both revisions and exact hashes.

Under the recovery lock and same verified handle:
1. Re-read both slots and compare to evidence hashes.
2. Re-verify handle identity and ACL.
3. Re-verify authorization freshness/single-use status and current Witness/ledger facts.
4. Construct the complete replacement with the strict slot encoder.
5. Validate it with the normal decoder before writing.
6. Write full slot using bounded short-write loop; zero progress/error is uncertain.
7. Flush same handle; read back all 256 bytes; validate and compare exact bytes.
8. Re-read both slots and run the unchanged normal selector. It must select the authorized revision/state.
9. Persist repair audit and consume authorization in independent authorization ledger.
10. Release lock only after final state is durably evidenced.

Any write/flush/readback/audit/authorization uncertainty blocks issuance. Re-enter recovery by recapturing exact bytes and resolving the same incident idempotently; never blindly retry the same write.

A crash after durable replacement but before authorization consumption is ambiguous. The independent authorization service must resolve status by authorization ID and exact action digest; do not issue a new authorization until resolved.

## 9. Decision matrix

| Observation / authority | Required result |
|---|---|
| VALID + MALFORMED; evidence store unavailable | RECOVERY_REQUIRED; no write |
| Witness unavailable/UNKNOWN | RECOVERY_REQUIRED; no write |
| Witness signature/binding invalid | SECURITY_FAILURE; no write |
| Witness operation/digest/sequence differs | SECURITY_FAILURE; no write |
| Ledger ahead of Witness | RECOVERY_REQUIRED; no write |
| Ledger behind Witness | ROLLBACK_SUSPECTED; separate catch-up design |
| Valid slot namespace differs | SECURITY_FAILURE; no write |
| Slot bytes changed after evidence capture | Authorization invalid; recapture/re-review |
| Authorization expired/replayed/wrong file ID | SECURITY_FAILURE; no write |
| Exact authority + valid signed authorization | Candidate repair may proceed |
| Partial write/crash/flush/readback uncertainty | RECOVERY_REQUIRED; preserve image and resolve authorization |
| Audit/authorization receipt uncertain | No issuance; resolve idempotently |
| Same revision, different valid bytes | Selector ERROR; no issuance |
| Normal selector does not choose new slot | Repair failed; no issuance |

## 10. Required tests before implementation

Classifier:
- every pair of ZERO, VALID, MALFORMED_NONZERO, IO_UNKNOWN;
- malformed bytes with plausible lower/equal/higher/max/zero/random revision hints;
- valid peer revisions 1 and UINT64_MAX-1;
- every embedded journal state and malformed record;
- exact/mismatched namespace, operation, digest, sequence;
- Witness COMMITTED, NOT_FOUND, UNKNOWN, UNAVAILABLE, CONFLICT, retired, bad signature/binding;
- ledger equal/expected/next/behind/ahead/corrupt/unavailable/wrong namespace;
- missing, stale, replayed, wrong-file/hash/action authorization;
- expiry and single-use races;
- no mutation and no issuance-ready result for malformed input.

Repair transaction:
- partial writes at byte boundaries 0, 1, 3, 4, 7, 8, 23, 24, 127, 128, 151, 152, 183, 184, 191, 192, 255;
- write/flush/readback outcomes with old/new durability possibilities;
- crash around evidence receipt, authorization, write, flush, readback, selector, audit, and authorization consumption;
- duplicate/concurrent recovery; lost authorization status;
- slot change or Witness sequence change between approval and execution;
- evidence-store outage/receipt mismatch;
- file identity, owner, DACL, reparse, service SID, mutex abandonment.

Every test asserts: no silent fallback, no new operation ID, no generation publication, no evidence loss, no sequence downgrade, and no READY on uncertainty.

## 11. Implementation boundary and open dependencies

This design does not authorize a Windows file adapter or recovery executable. Before even a pure-C classifier:
- freeze authorization schema and signature profile;
- define independent evidence receipt contract;
- define Witness proof needed for each journal state;
- define ledger namespace/provenance and dedicated reconcile API;
- prove idempotent resolution of repair vs authorization-consumption crash;
- approve operator separation-of-duties and incident policy.

A future pure-C classifier may consume already-verified facts and return only a conservative decision. It must not claim to authenticate Witness, validate Windows identity, or authorize writes. Windows adapter, ACL, mutex, service identity, and real storage tests remain separate gates.

## 12. Status

Design only. No source code, persistent journal, evidence store, authorization service, ACL, mutex, service SID, network endpoint, or Production integration was created or changed by this work.

Production status: NO-GO.

# Pending Operation Journal v1 — Design

Date: 2026-11-09
Status: design only; no persistent adapter, service, ACL, or Production integration
Decision: introduce a separate write-ahead intent journal for one unresolved Witness ADVANCE per namespace. It is recovery metadata, never an anti-rollback authority.

## 1. Problem statement

The remote Witness protocol requires the client to persist operation_id, expected_sequence, next_sequence, and canonical request digest before sending ADVANCE. The current generation ledger stores only epoch. If the process crashes after transmission but before receiving the response, the restarted client must know the exact operation identity and payload to resolve the outcome. Creating a new operation_id can double-advance; guessing from the local epoch is unsafe.

The journal therefore records one outstanding issuance transaction. It is separate from the epoch ledger because the two records have different semantics and recovery rules. The Witness remains the monotonic authority. A valid local journal can be restored, cloned, tampered with, or rolled back and cannot prove whether the Witness committed.

## 2. Scope and explicit non-goals

In scope:
- fixed-format intent record codec;
- strict state transitions;
- storage callback contract for write, flush, readback, and clear/tombstone;
- one unresolved operation per namespace;
- deterministic recovery decisions after restart;
- pure-C model and fault-injection tests before any Windows adapter.

Out of scope:
- real file/registry/database storage;
- Windows service identity, SID, DACL, handle-relative path safety, or cross-process lock implementation;
- TLS/mTLS, network transport, Witness provider, credential provisioning;
- anti-rollback protection, backup restore detection, or cryptographic authenticity of local records;
- offline issuance and automatic recovery;
- Production integration.

## 3. Record format

Version 1 uses a fixed 128-byte record, encoded field-by-field in big-endian order. Never persist a native C struct.

| Offset | Size | Field | Rule |
|---:|---:|---|---|
| 0 | 4 | magic | ASCII `RYPI` |
| 4 | 4 | format_version | uint32 = 1 |
| 8 | 4 | record_state | PREPARED=1, WITNESS_COMMITTED=2, LOCAL_COMMITTED=3, ABORTED=4 |
| 12 | 4 | flags | zero; reject nonzero |
| 16 | 16 | namespace_id | nonzero opaque 128-bit ID |
| 32 | 16 | operation_id | nonzero 128-bit ID |
| 48 | 8 | expected_sequence | uint64 |
| 56 | 8 | next_sequence | uint64; exactly expected + 1 |
| 64 | 32 | request_digest | SHA-256 of canonical ADVANCE body and immutable operation fields |
| 96 | 8 | witness_sequence | zero before confirmed commit; otherwise exactly next_sequence |
| 104 | 8 | reserved | all zero |
| 112 | 4 | CRC32 | CRC-32 over bytes 0..111 |
| 116 | 4 | CRC32 complement | bitwise complement |
| 120 | 8 | reserved_tail | all zero |

CRC is only accidental-corruption/torn-write detection. It is not authentication, tamper protection, or rollback protection. A privileged attacker can rewrite the record and recompute CRC. Namespace and operation identifiers are not secrets.

PREPARED has witness_sequence=0. WITNESS_COMMITTED and LOCAL_COMMITTED require witness_sequence=next_sequence. ABORTED is permitted only when the client has authoritative proof that the exact operation did not commit and the state transition is explicitly recorded; uncertainty is never ABORTED. In v1, automatic abort is deliberately not implemented.

## 4. Storage contract

The pure core receives callbacks; it performs no filesystem I/O:
- read(record): EMPTY, VALID bytes, or ERROR;
- write(record): exact record bytes;
- flush(): durable persistence barrier for the adapter's documented contract;
- readback(record): exact persisted bytes;
- clear/tombstone(): durable terminal marker, never blind deletion.

Required adapter semantics:
1. Single fixed record or two-slot journal with a monotonically increasing local record revision. Selection and torn-write recovery must be specified before implementation.
2. Write is not considered committed until flush and readback verify exact bytes.
3. Any uncertain write/flush/readback/clear outcome makes the in-memory journal DEGRADED. No new operation can be prepared until explicit reload/reconciliation.
4. Adapter must bind validation to the opened handle, reject reparse points and unexpected file types, enforce restrictive DACL and owner, and use a dedicated service identity.
5. Cross-process issuance lock is required; a process-local mutex is insufficient.
6. The adapter must document atomicity, FlushFileBuffers behavior, metadata durability, sharing, power-loss, and backup/restore behavior on every supported storage stack.
7. Never auto-create or silently reset a missing/corrupt journal during runtime.

A single overwrite-in-place file is not accepted without a proven torn-write recovery design. Candidate implementations include a two-slot fixed record with revision/CRC or an append-only write-ahead log with bounded compaction; selection is deferred pending adapter-level durability evidence.

## 5. State machine and legal transitions

States:
- EMPTY: no outstanding transaction.
- PREPARED: immutable operation tuple durably recorded; request may or may not have been sent.
- WITNESS_COMMITTED: authenticated Witness confirms the exact operation_id/digest committed next_sequence; local transition durably recorded.
- LOCAL_COMMITTED: epoch ledger durably stores next_sequence and readback matches; generation may now be composed/published subject to CNG and audit policy.
- ABORTED: terminal only with authoritative non-commit proof and explicit policy; not automatically used in v1.
- DEGRADED: storage or protocol outcome uncertain; sticky until restart/recovery procedure.
- RECOVERY_REQUIRED: records inconsistent, malformed, namespace mismatch, or Witness unavailable/ambiguous.

Legal flow:
EMPTY -> PREPARED -> WITNESS_COMMITTED -> LOCAL_COMMITTED -> EMPTY (after a durable terminal/tombstone protocol).
PREPARED -> RECOVERY_REQUIRED on restart until Witness OPERATION_STATUS/identical ADVANCE resolves it.
WITNESS_COMMITTED -> RECOVERY_REQUIRED if local ledger write fails; epoch is burned remotely and must never be decremented.
Any uncertain storage operation -> DEGRADED.
Any impossible transition, digest/namespace/sequence mismatch, or malformed record -> RECOVERY_REQUIRED.

The journal must never transition backward. A record from a later state cannot be replaced with an earlier state. A new PREPARED operation is forbidden until the prior operation reaches a durably finalized terminal state.

## 6. Transaction protocol

### Prepare
1. Acquire cross-process issuance lock and lifecycle lock.
2. Verify configured namespace and authenticated Witness identity.
3. Load journal and epoch ledger; do not initialize either automatically.
4. Obtain authoritative Witness READ and reconcile namespace/sequence. Local epoch must equal Witness sequence before a new operation is prepared.
5. Generate operation_id with CNG once. Construct canonical ADVANCE body and SHA-256 digest.
6. Persist PREPARED (namespace, operation_id, expected, next, digest) with write -> flush -> readback exact verification.
7. Only after successful verification may the client transmit ADVANCE.

### Resolve remote outcome
- Authenticated ADVANCED or OPERATION_STATUS COMMITTED must match namespace, operation_id, digest, expected/next sequence and witness identity.
- Persist WITNESS_COMMITTED with witness_sequence=next_sequence, flush, and read back.
- On timeout, disconnect, process crash, malformed response, or lost response, retain PREPARED. On restart, query OPERATION_STATUS for the same operation_id/digest. If COMMITTED, continue. If NOT_FOUND, retry the exact ADVANCE body with the same operation_id. If UNKNOWN/UNAVAILABLE/CONFLICT, stop in RECOVERY_REQUIRED.
- Never replace an unresolved operation_id or digest with a fresh one.

### Commit local epoch
1. Call the existing local epoch ledger only after WITNESS_COMMITTED is durable.
2. Require the ledger's prior epoch equals expected_sequence and resulting epoch equals next_sequence.
3. Require flush and readback verification from the storage adapter.
4. Persist LOCAL_COMMITTED in the pending journal and verify it.
5. Only then compose generation (epoch prefix + CNG suffix). CNG failure burns the sequence and does not roll back either authority.
6. Emit required audit record. If mandatory audit persistence fails, apply the explicitly approved fail-closed policy.
7. Durably finalize/retire the journal record before allowing another issuance. Finalization must not erase evidence before its terminal state is durable.

Important: if the remote Witness commits but local journal or epoch ledger persistence fails, the remote sequence is consumed. The next startup must reconcile to the Witness; it must not reissue or decrement the epoch. If local epoch is behind Witness, classify ROLLBACK_SUSPECTED/RECOVERY_REQUIRED and use an audited recovery operation to catch up only after the exact operation is resolved.

## 7. Restart recovery decision table

| Journal | Witness status | Local epoch | Required action |
|---|---|---:|---|
| EMPTY | known namespace | equals witness | READY candidate; policy checks still required |
| EMPTY | known namespace | below witness | ROLLBACK_SUSPECTED; no issuance |
| EMPTY | known namespace | above witness | RECOVERY_REQUIRED; local cannot overrule |
| PREPARED | COMMITTED exact digest | expected | persist WITNESS_COMMITTED, then commit local |
| PREPARED | NOT_FOUND | expected | retry exact ADVANCE with same operation_id |
| PREPARED | UNKNOWN/unavailable | any | RECOVERY_REQUIRED; retain evidence |
| PREPARED | CONFLICT/digest mismatch | any | SECURITY FAILURE; retain evidence |
| WITNESS_COMMITTED | COMMITTED exact | expected | commit local next_sequence |
| WITNESS_COMMITTED | COMMITTED exact | next | persist LOCAL_COMMITTED |
| WITNESS_COMMITTED | mismatch | any | SECURITY FAILURE |
| LOCAL_COMMITTED | COMMITTED exact | next | finalize journal; generation may already have been emitted, audit evidence required |
| malformed/corrupt | any | any | RECOVERY_REQUIRED; preserve raw bytes |
| namespace mismatch | any | any | SECURITY FAILURE; never auto-rebind |

A PREPARED record does not prove that ADVANCE was sent. A missing response does not prove non-commit. A local CRC-valid record does not prove freshness. Witness status and idempotent retry resolve operation outcome.

## 8. Integrity, rollback, and privacy

- CRC/complement detect accidental damage only.
- Do not store credentials, private keys, TLS session material, client certificate private data, or secrets.
- Namespace and operation IDs plus digest may be logged as operational metadata under access control.
- Store raw corrupt bytes and metadata for incident evidence; do not overwrite during recovery.
- Local backup restore can restore an older valid journal. The Witness remains authoritative; mismatch causes fail-closed recovery.
- A clone must not reuse the source namespace or credential. Local journal duplication is expected to be detected through remote CAS and deployment identity policy, not local CRC.
- The journal is not an authentication factor and must not be used to authorize a Worker or pipe peer.

## 9. Pure-core API proposal

The isolated C core should expose operations conceptually equivalent to:
- encode/decode fixed record with strict validation;
- initialize empty in-memory journal only under explicit caller authorization;
- load record and reject malformed/ambiguous states;
- prepare(namespace, operation_id, expected, digest);
- mark_witness_committed(operation_id, digest, committed_sequence);
- mark_local_committed(operation_id, digest, local_sequence);
- finalize(operation_id) through a durable terminal marker;
- recovery_classify(record, witness_status, local_epoch).

The core must not perform network, cryptography, Windows I/O, process, token, ACL, or service operations. SHA-256 and CNG operation-ID generation are injected by the caller and separately tested. The core compares supplied digest bytes but does not claim to compute or authenticate them.

## 10. Acceptance test matrix

1. Fixed byte-vector encode/decode and endian correctness.
2. Reject zero namespace, zero operation ID, zero digest, expected UINT64_MAX, next != expected+1.
3. Reject bad magic/version/state/flags/reserved/CRC/complement and every truncated/oversized length.
4. PREPARED requires witness_sequence zero; committed states require witness_sequence=next.
5. Duplicate prepare while pending is rejected.
6. Prepare with a different namespace while pending is rejected.
7. Wrong operation_id/digest/namespace/sequence cannot mark committed.
8. Witness sequence mismatch cannot advance state.
9. WITNESS_COMMITTED cannot be marked before PREPARED.
10. LOCAL_COMMITTED cannot be marked before WITNESS_COMMITTED.
11. State regression and repeated terminal transitions are rejected.
12. Simulated write failure leaves no success result.
13. Flush failure makes instance DEGRADED.
14. Readback mismatch makes instance DEGRADED.
15. Restart from PREPARED + COMMITTED resolves same operation.
16. Restart from PREPARED + NOT_FOUND retries same operation only.
17. PREPARED + UNKNOWN/unavailable blocks.
18. Remote committed + local write failure burns sequence and blocks issuance.
19. Local epoch behind witness is rollback-suspected.
20. Local epoch ahead of witness is recovery-required.
21. Corrupt/empty/missing journal never auto-initializes in runtime load.
22. Old valid backup remains subordinate to Witness sequence.
23. Two concurrent issuers are serialized by caller lock and remote CAS.
24. UINT64_MAX cannot wrap.
25. CNG operation ID failure occurs before PREPARED and sends no request.
26. CNG generation suffix failure after commit burns epoch and returns zero output.
27. Crash injection before/after every write, flush, readback, send, response, local commit, and finalize boundary.
28. Witness failover and operation-status consistency tests.
29. Clone and VM restore tests using independent Witness state.
30. Evidence retention and audited recovery tests.

Pure callback tests establish state-machine behavior only. They do not prove storage durability, ACLs, service identity, backup independence, remote linearizability, TLS, or crash/power-loss semantics.

## 11. Open decisions before implementation

- Single-record vs two-slot vs append-only adapter layout and its proven crash semantics.
- Whether the local journal stores one outstanding operation or a bounded history; v1 intentionally permits one outstanding operation only.
- Exact canonical ADVANCE body and SHA-256 digest scope; must be frozen with the Witness wire schema.
- Whether mandatory audit is part of the issuance commit boundary.
- Exact terminal/tombstone retention and evidence policy.
- Whether local epoch ledger can safely catch up from a Witness-committed sequence without violating its existing two-slot invariants; likely requires a dedicated reconcile API and tests.
- Cross-process lock implementation and stale-owner recovery.
- Witness linearizability, operation retention, disaster recovery, and signing-key trust profile.

## 12. No-go gates

No persistent adapter or runtime integration until:
1. Record format and state machine receive independent review.
2. Witness canonical schema and digest scope are frozen.
3. Crash-consistent local adapter design is selected and demonstrated.
4. Cross-process serialization is implemented and tested.
5. Fault injection proves write/flush/readback/finalize ordering.
6. Real restart and power-loss tests pass on supported storage.
7. Backup restore, VM revert, clone, and remote Witness recovery are exercised.
8. Windows service identity, DACL, handle-bound path safety, credential lifecycle, TLS/mTLS, and end-to-end Broker gates pass.

Production remains NO-GO. This document authorizes design and isolated pure-code testing only; it does not authorize creating files for persistent runtime state, service installation, ACL changes, credentials, remote infrastructure, network changes, or Production wiring.

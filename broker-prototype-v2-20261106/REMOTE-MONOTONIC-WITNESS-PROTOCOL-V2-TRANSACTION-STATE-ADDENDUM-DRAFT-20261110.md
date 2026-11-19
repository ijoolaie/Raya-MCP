# Remote Monotonic Witness Protocol v2 — Transaction-State Addendum (Draft)

Date: 2026-11-10
Status: design-only; draft; not frozen; no implementation authorization
Production: NO-GO

## 1. Purpose and compatibility boundary

This addendum addresses the Evidence Service commit lifecycle identified in EVIDENCE-SERVICE-COMMIT-CRASH-RECOVERY-MATRIX-V1-DESIGN-20261110.md.

The existing Remote Monotonic Witness Protocol v1 defines a single ADVANCE that durably increments a sequence and records the operation as committed. That operation is suitable only when sequence advancement itself is the intended terminal fact. It cannot safely mean that an Evidence Service transaction is fully committed before metadata, retention, independent object verification and audit checkpoint are complete.

This addendum proposes a distinct transaction-state protocol profile:
- RESERVE consumes a sequence and creates a durable pending operation.
- FINALIZE changes that exact operation to EVIDENCE_COMMITTED only after all required external facts are verified.
- ABORT records a terminal non-commit outcome while permanently consuming the sequence.
- STATUS and READ resolve exact state.

Do not silently reinterpret v1 ADVANCE. Do not deploy this profile against an existing v1 namespace or client. Either define a separately versioned v2 endpoint/profile and separately provisioned namespace, or perform an explicitly reviewed migration with fencing and an auditable cutover. No migration is designed or authorized here.

The existing generation-ledger use case and the Evidence Service transaction use case must not share a namespace unless a future specification proves their sequence semantics compatible. Default: separate namespaces and purpose-bound client identities.

## 2. Security properties and invariants

1. Sequence allocation is monotonic and never reused, including after ABORT.
2. At most one unresolved reservation exists per namespace in v2 profile 1. This deliberately trades concurrency for simple crash recovery and contiguous transaction ordering.
3. Each operation_id identifies one immutable request digest for the lifetime of the namespace.
4. Same operation_id + same digest is idempotent; same ID + different digest is a hard conflict.
5. FINALIZE is possible only from the exact RESERVED state and binds the final evidence, retention and audit digests.
6. ABORT is terminal, audited and sequence-burning; it never rolls the namespace back.
7. A lost response is resolved using the same operation_id/digest; no replacement operation is created.
8. UNKNOWN, unavailable, stale, conflicting or unauthenticated state never authorizes issuance or receipt.
9. Witness state is authoritative for sequence allocation and operation terminal status, but does not independently prove object bytes, metadata durability, retention or audit correctness.
10. A Witness response cannot authorize journal repair, Broker start, generation issuance or Evidence Service receipt signing by itself.
11. All transitions are authenticated, linearizable, durable before positive response, and fenced against stale leaders.
12. Witness backup/restore must preserve monotonic continuity through an independent anti-rollback anchor; otherwise namespace is RECOVERY_LOCKED or RETIRED.

## 3. Namespace state model

Per namespace store:
- namespace_id and immutable purpose/profile identifier;
- lifecycle ACTIVE, RECOVERY_LOCKED or RETIRED;
- namespace_generation and fencing_epoch;
- allocated_sequence (highest sequence ever reserved; never decreases);
- last_terminal_sequence (highest reservation resolved FINALIZED or ABORTED);
- last_evidence_committed_sequence (highest sequence finalized as EVIDENCE_COMMITTED);
- at most one active_operation_id and active_request_digest;
- active_expected_sequence and active_reserved_sequence;
- active operation state RESERVED, EVIDENCE_COMMITTED or ABORTED;
- immutable operation record for every operation_id, including terminal result and exact digests;
- trust/key generation and audit correlation/checkpoint metadata as required by deployment profile.

Initial provisioning establishes all sequence values at zero and no active operation. Runtime READ/RESERVE must never create a namespace. Unknown namespace is an explicit error.

The namespace is ACTIVE only after fresh reconciliation of Witness, metadata, object storage, retention, audit and trust state by the Evidence Service coordinator. Witness ACTIVE alone is not overall service readiness.

## 4. Operation record

A reservation binds the following immutable fields:
- protocol/profile version;
- namespace_id and namespace_generation;
- operation_id (16-byte nonzero);
- expected_allocated_sequence;
- reserved_sequence = expected_allocated_sequence + 1;
- descriptor_digest;
- package_digest and manifest_digest;
- canonical object_manifest_digest;
- retention_policy_id and retention_policy_generation;
- service_generation and fencing_epoch;
- client identity/key ID and creation request digest.

The finalization record additionally binds:
- evidence_commit_digest;
- metadata_commit_digest and metadata revision/generation;
- retention_verification_digest;
- object_readback_verification_digest;
- audit_checkpoint_sequence and audit_checkpoint_digest;
- finalization request digest and witness commit timestamp metadata.

The abort record binds:
- fixed reason code;
- exact operation and descriptor digest;
- evidence/audit reference digest where available;
- separately authorized abort decision reference where policy requires it;
- abort request digest.

Do not place credentials, private keys, bearer tokens, file paths, raw evidence bytes or arbitrary RPC arguments in Witness records. Exact canonical encoding, field numbers, maximum sizes, digest preimages and signature profile are freeze gates.

## 5. RESERVE operation

Request:
- common authenticated v1-style envelope under a new v2 protocol/profile identifier;
- namespace_id, operation_id, client_nonce, request_id, client_key_id;
- expected_allocated_sequence;
- reserved_sequence (must equal expected + 1);
- descriptor_digest and all immutable reservation fields;
- fencing_epoch.

Atomic server behavior:
1. Authenticate client and authorize exact purpose, namespace and operation.
2. Verify namespace ACTIVE, profile exact, generation and fencing epoch current.
3. Look up operation_id before comparing sequence.
4. If operation exists with exact request digest, return original state and reserved_sequence.
5. If operation exists with different digest, return OPERATION_ID_CONFLICT without mutation.
6. If a different unresolved reservation exists, return NAMESPACE_BUSY with its opaque operation reference; do not allocate another sequence.
7. Require expected_allocated_sequence == current allocated_sequence.
8. Require reserved_sequence == expected + 1 and reject overflow.
9. Atomically persist allocated_sequence = reserved_sequence and immutable operation state RESERVED.
10. Durably commit before returning RESERVED.

A RESERVED sequence is consumed permanently. It is not yet evidence commit and cannot be presented as a positive Evidence Receipt. A duplicate exact RESERVE returns RESERVED or its later terminal state, never allocates twice.

## 6. FINALIZE operation

Request binds:
- namespace_id, operation_id, descriptor_digest;
- reserved_sequence and fencing_epoch;
- evidence_commit_digest;
- metadata_commit_digest/revision;
- retention_verification_digest and policy generation;
- object_readback_verification_digest;
- audit_checkpoint_sequence and audit_checkpoint_digest;
- canonical finalization request digest.

Atomic server behavior:
1. Authenticate caller and verify current namespace generation/fencing epoch.
2. Look up exact operation and compare every immutable binding.
3. If already EVIDENCE_COMMITTED with identical finalization digest, return original result.
4. If same operation has a different finalization digest, return FINALIZATION_CONFLICT and lock/escalate according to policy.
5. Require operation state RESERVED and exact active reservation.
6. Validate schema and nonzero required digests/sequences. Witness cannot independently validate external object/retention/audit truth; the authorized coordinator must supply authenticated verification artifacts or references under a separately frozen trust contract.
7. Atomically mark operation EVIDENCE_COMMITTED, advance last_terminal_sequence and last_evidence_committed_sequence to reserved_sequence, clear active reservation, and persist immutable finalization digest.
8. Durably commit before returning EVIDENCE_COMMITTED.

FINALIZE is not a generic caller assertion. A future implementation must authenticate the coordinator identity and verify signed/attested fact bundles from the object verifier, metadata DB, retention authority and independent audit checkpoint. A set of caller Booleans is not sufficient. The exact attestation format and trust graph are unresolved.

## 7. ABORT operation

ABORT is a terminal disposition for a RESERVED operation that cannot be completed. It does not mean rollback and does not restore the sequence.

Required behavior:
- exact namespace, operation_id, descriptor_digest, reserved_sequence and fencing epoch;
- fixed allowlisted reason code; no free-form sensitive text;
- authenticated abort authority distinct from ordinary uploader;
- idempotent abort request ID/digest;
- immutable audit/incident reference;
- durable transition RESERVED -> ABORTED;
- last_terminal_sequence advances to reserved_sequence;
- allocated_sequence remains unchanged at reserved_sequence;
- active reservation is cleared only after durable ABORTED record.

ABORT is forbidden if operation is already EVIDENCE_COMMITTED. Repeated identical ABORT returns the original result. ABORT after FINALIZE or with changed digest conflicts. No automatic timeout, lease expiry, process restart, missing object or witness outage may trigger ABORT. Whether ABORT requires a signed single-use Recovery Authorization is a mandatory freeze decision; conservative default is yes for any operation after metadata PREPARED.

An ABORTED operation has no Evidence Receipt and its sequence is permanently burned. Its evidence objects and metadata remain subject to forensic retention; garbage collection is a separate protocol and not authorized.

## 8. STATUS and READ

STATUS request binds namespace, operation_id and original descriptor/request digest. Results:
- NOT_FOUND_LINEARIZABLE: no operation record exists at the linearization point and no request for this ID is in flight under the server's serialization contract;
- RESERVED: exact operation remains active and unresolved;
- EVIDENCE_COMMITTED: exact finalization digest and reserved sequence;
- ABORTED: exact abort digest, reason code and reserved sequence;
- CONFLICT: ID exists with a different digest;
- UNKNOWN/UNAVAILABLE/RECOVERY_LOCKED.

NOT_FOUND is safe only if the server serializes STATUS against RESERVE for the same namespace/operation_id. If this cannot be guaranteed, client must retry exact RESERVE and rely on idempotency rather than interpreting NOT_FOUND as permission to create a new ID.

READ returns allocated_sequence, last_terminal_sequence, last_evidence_committed_sequence, active operation state/reference, namespace lifecycle, namespace_generation, fencing_epoch and profile. It must be linearizable or explicitly versioned with a freshness proof. A stale replica response cannot authorize a write or receipt.

## 9. Sequence and concurrency semantics

V2 profile 1 permits exactly one unresolved reservation per namespace. This prevents:
- reservation N+1 being indefinitely blocked behind unresolved N;
- finalization out of order;
- ambiguity about last terminal sequence;
- concurrent Evidence Service commits racing across metadata/retention/audit.

A later profile may support multiple reservations, but must define ordered finalization, gaps, aborts, fencing and contiguous checkpoint semantics before implementation.

Sequence transitions:
- RESERVE: allocated_sequence increments by exactly one; terminal/evidence-committed sequence unchanged.
- FINALIZE: allocated_sequence unchanged; last_terminal_sequence and last_evidence_committed_sequence become reserved_sequence.
- ABORT: allocated_sequence unchanged; last_terminal_sequence becomes reserved_sequence; last_evidence_committed_sequence unchanged.
- No transition decrements any counter.
- UINT64_MAX prevents further RESERVE and retires/locks namespace under approved policy.

Because one unresolved operation is allowed, terminal sequence advances one at a time. If an operation is ABORTED, the next reservation uses the following sequence; evidence committed sequence may therefore be lower than allocated/terminal sequence. Receipts must explicitly distinguish allocated/reserved sequence from evidence-committed status.

## 10. Failure and recovery rules

- Crash before RESERVE durable: exact STATUS/RESERVE resolves whether reservation exists.
- Crash after RESERVE durable: operation remains RESERVED; no new operation until exact FINALIZE or authorized ABORT.
- Crash during metadata/retention/audit: remain RESERVED; reconcile each external authority; no receipt.
- Crash after FINALIZE durable but response lost: STATUS returns exact EVIDENCE_COMMITTED; do not finalize again with altered digest.
- Crash during ABORT: STATUS resolves exact terminal outcome; no sequence reuse.
- Witness unavailable: RECOVERY_LOCKED; do not infer from local metadata.
- Witness backup rollback, split brain or conflicting same-sequence record: RECOVERY_LOCKED; no automatic selection of larger sequence.
- Local metadata ahead of Witness: preserve and lock; never advance Witness automatically.
- Witness RESERVED while local facts appear complete: still no receipt until FINALIZE accepted.
- Witness EVIDENCE_COMMITTED while local data now fails integrity: preserve receipt/evidence, mark corruption/revocation through a separate audited protocol; never rewrite Witness history.
- Stale fencing epoch: reject all mutations even if credentials otherwise valid.

## 11. Audit, retention and response semantics

Audit every RESERVE attempt/outcome, STATUS resolution, FINALIZE, ABORT, fencing rejection, conflict, lock, restore, key change and namespace retirement. Events bind exact request/finalization/abort digests and monotonic sequence.

Witness operation records must remain queryable for the namespace lifetime or under a rigorously specified anti-replay compaction scheme. Pruning idempotency records while old retries remain possible is prohibited.

Positive FINALIZE response must be signed and bind witness identity/key generation, namespace/profile, operation ID, descriptor digest, reserved sequence, finalization digest, result state, fencing epoch and freshness fields. Transport TLS/mTLS and server signature verification follow a frozen cryptographic profile. Nonces are freshness bindings, not standalone authentication.

Witness does not administer object retention. It records the verified retention digest/policy generation supplied by the trusted coordinator. Retention remains independently enforced by storage.

## 12. Acceptance matrix

1. Unknown namespace cannot auto-provision.
2. v1 client/profile cannot call v2 transaction methods.
3. RESERVE increments allocated sequence once and durably stores operation.
4. Lost RESERVE response resolves to same RESERVED sequence.
5. Same ID/same digest idempotent across process and Witness restart.
6. Same ID/different digest conflicts without mutation.
7. Concurrent different RESERVE while active returns NAMESPACE_BUSY.
8. Wrong expected sequence and overflow fail closed.
9. FINALIZE only from exact RESERVED state.
10. FINALIZE exact retry returns same result; altered evidence/retention/audit digest conflicts.
11. Missing/invalid fact attestation blocks FINALIZE.
12. ABORT requires authorized decision, burns sequence and cannot be reversed.
13. No timeout or outage auto-aborts.
14. FINALIZE and ABORT cannot both win.
15. Crash at each durable write/flush/replication boundary has one queryable outcome.
16. Stale fencing epoch cannot mutate.
17. Witness failover preserves linearizability and operation records.
18. Restore of stale Witness backup is detected by independent anchor or namespace is locked/retired.
19. STATUS NOT_FOUND is linearizable against in-flight RESERVE.
20. One unresolved reservation invariant survives concurrent clients.
21. UINT64_MAX never wraps.
22. ABORTED sequence is never reused.
23. EVIDENCE_COMMITTED sequence is distinct from allocated high-water and terminal sequence.
24. No Witness response alone issues a receipt or authorizes Broker/generation activity.
25. Key rotation, revocation, stale trust bundle and signature mutation reject.
26. Independent security review validates all state transitions and crash recovery.

## 13. Freeze gates

1. Decide separate v2 namespace/profile vs formal migration; no v1 reinterpretation.
2. Freeze canonical wire schema, request/finalization/abort digests and crypto profile.
3. Define authenticated coordinator/fact-attestation trust graph for FINALIZE.
4. Define signed single-use Recovery Authorization for ABORT and its replay/status authority.
5. Prove Witness linearizability, durable atomic state, idempotency retention and backup anti-rollback.
6. Define fencing epoch issuance and mutation-boundary enforcement.
7. Define sequence semantics across aborted operations and receipt schema.
8. Define namespace lifecycle, retirement and disaster recovery.
9. Independent review of privacy, audit, operations and incident response.

## 14. Explicit non-authorization

Design only. No endpoint, provider, namespace, credentials, certificate, key, database, service, storage adapter, network rule, persistent file, C source/header, test harness, ACL, mutex or Production integration is created or authorized. Existing v1 protocol and code remain unchanged. No runtime Witness request is made. Production: NO-GO.

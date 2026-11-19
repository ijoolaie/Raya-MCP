# Evidence Service Commit Crash-Recovery Matrix v1

Date: 2026-11-10
Status: design-only; draft; not frozen; no implementation authorization
Production: NO-GO

## 1. Scope and governing rules

Defines candidate crash recovery for one Evidence Service commit spanning immutable object storage, metadata DB, independent monotonic witness, retention, audit checkpoint, and receipt signing. This is protocol design only; no provider, database, object store, signature profile or witness deployment is selected.

Rules:
1. One immutable operation_id and canonical request_digest identify the transaction across all retries/recovery.
2. Timeout, disconnect, process exit or missing response never proves success or failure.
3. Mutations are idempotent for exact operation_id + digest; same ID/different digest is a security conflict.
4. No receipt until every required durable fact is independently reconciled.
5. Never decrement, reuse, or silently skip an authoritative sequence. A sequence may be burned, never recycled.
6. Never select a "highest-looking" local copy without exact digest and independent authority.
7. Unknown, contradictory, stale or unavailable state is RECOVERY_LOCKED.
8. Recovery grants no journal-write, Broker-start or generation-issuance capability.
9. Required audit failure means remain locked.
10. Existing fail-closed slot selector remains unchanged.

## 2. Immutable commit descriptor

Before any remote mutation, construct and durably persist:
- evidence_namespace_id, service_generation;
- capture_id, incident_id;
- nonzero operation_id and canonical request_digest;
- package_digest;
- ordered immutable object IDs, lengths and hashes;
- manifest and full-image digests;
- expected_witness_sequence and proposed_next_sequence;
- retention_policy_id and policy_generation;
- metadata schema/profile and audit correlation ID.

The descriptor cannot change on retry. If local persistence is uncertain, do not transmit. A changed object ID, policy, sequence or digest is a different operation and must be rejected.

## 3. Candidate verified phases

P0 VALIDATED: descriptor validated; no remote mutation.
P1 OBJECTS_DURABLE: all immutable objects written, flushed under selected durability contract, and read back with exact length/hash.
P2 METADATA_PREPARED: exact PREPARED metadata and object references durable.
P3 WITNESS_RESERVED: independent witness has durably reserved exact sequence for operation ID/descriptor digest.
P4 METADATA_COMMITTED: metadata durably finalized and bound to reservation.
P5 RETENTION_ACTIVE: storage-enforced retention active and independently verified for every object.
P6 DATA_VERIFIED: independent readback confirms all bytes, metadata, references and retention.
P7 AUDIT_CHECKPOINTED: append-only commit event and external checkpoint durable and bound to operation.
P8 WITNESS_FINALIZED: witness confirms evidence commit for exact operation, sequence, descriptor, retention and audit digests.
P9 RECEIPT_ISSUED: exact receipt signed, persisted and independently verified.
P10 STATUS_PUBLISHED: authenticated live status returns COMMITTED for exact receipt/package.

These are verified facts, not merely local phase flags. Recovery queries each authority responsible for a fact.

## 4. Critical protocol correction

A single witness ADVANCE that claims final EVIDENCE_COMMITTED before metadata, retention, readback and audit are complete is semantically wrong. The prior Remote Monotonic Witness v1 currently describes a single ADVANCE; it must be revised before implementation.

Candidate two-stage witness contract:
- RESERVE(expected, next, operation_id, descriptor_digest) -> RESERVED.
- FINALIZE(operation_id, descriptor_digest, evidence_commit_digest, retention_digest, audit_checkpoint) -> EVIDENCE_COMMITTED.
- ABORT(operation_id, descriptor_digest, reason_code) -> ABORTED while preserving/burning the reserved sequence.
- STATUS resolves the exact operation and digest.

All transitions are authenticated, linearizable, idempotent and monotonic. Same ID/different digest conflicts. Sequence is never reused after reservation. If the witness cannot implement this state machine safely, stop and revise the protocol; do not overload ADVANCE semantics.

Candidate commit order:
1. Persist immutable operation descriptor.
2. Write/flush/readback immutable objects.
3. Persist metadata PREPARED.
4. Witness RESERVE.
5. Metadata COMMITTED, bound to exact reservation.
6. Activate and verify retention for every object.
7. Independently read/verify objects, metadata and retention.
8. Append audit event and obtain external checkpoint.
9. Witness FINALIZE binding exact evidence, retention and audit digests.
10. Sign/persist/verify receipt.
11. Publish COMMITTED status.

## 5. Crash-recovery matrix

| Crash boundary | Possible durable state | Required restart behavior | Receipt |
|---|---|---|---|
| Before descriptor | None | Normal validation; no remote mutation | No |
| Descriptor persisted, before upload | Intent only | Query exact operation; resume same descriptor or explicitly abandon with audited proof | No |
| During object upload | Partial immutable objects | Query exact IDs; verify each; upload missing objects idempotently; conflict locks | No |
| Write success before flush | Bytes may not survive | Treat uncertain; independent read/status; retry same immutable ID only if safe | No |
| Flush response lost/failure | Old/new durability uncertain | Verify through provider durability/readback contract; otherwise lock | No |
| P1 before PREPARED | Orphan immutable objects | Preserve objects; resume exact transaction or quarantine; no deletion | No |
| During PREPARED write | Absent/partial/committed metadata | Read by operation ID+digest; malformed/conflict locks; exact CAS retry only | No |
| P2 before RESERVE | Objects + PREPARED | Verify P1/P2; send exact RESERVE | No |
| RESERVE request lost | May be absent or committed | Query exact operation; retry only exact RESERVE after authoritative NOT_FOUND; UNKNOWN locks | No |
| RESERVE committed, response lost | Witness reservation durable | Query exact status; accept only exact RESERVED proof; never allocate another sequence | No |
| During metadata COMMITTED | PREPARED/COMMITTED/uncertain | Read back; verify reservation binding; idempotent CAS only; otherwise lock | No |
| P4 before retention | Metadata committed, retention absent | Activate exact policy idempotently; no receipt | No |
| During retention activation | Some objects may be locked | Query each object and policy generation; any missing/unknown blocks | No |
| P5 before readback | Objects/metadata/retention exist | Re-read every object and metadata; verify exact digest and references | No |
| During P6 | Partial verification | Repeat complete independent verification; partial pass is insufficient | No |
| P6 before audit | Data verified, audit absent | Append same event ID/digest; query checkpoint; no receipt until confirmed | No |
| Audit committed, response lost | Checkpoint may exist | Query exact event/digest; idempotent retry; no duplicate event | No |
| P7 before FINALIZE | All local facts complete, witness reserved | Submit exact FINALIZE binding evidence/retention/audit digests | No |
| FINALIZE committed, response lost | Witness may be finalized | Query exact operation; only authenticated exact proof proceeds | No |
| During signing | Signature may exist or not | Query signer by fixed request ID/digest; no alternate body/key fallback | No |
| Signed, before persistence | Signer may retain exact bytes | Retrieve same signed bytes; verify exact body/profile; persist | No |
| Receipt persisted, before publish | Receipt exists, status may lag | Verify receipt and live authority status; publish exact result only | No |
| Publish response lost | Status may already be committed | Query exact receipt/package; return idempotently | Only after confirmation |
| Witness unavailable at any phase | Outcome unknown | RECOVERY_LOCKED; preserve state; retry later | No |
| Same operation ID, different digest | Conflict | Security incident; preserve evidence; lock namespace | No |
| Witness ahead, local phase incomplete | Reservation/finalization may be ahead | Reconcile exact operation; complete only if every fact verifiable | No |
| Local metadata ahead of witness | Unwitnessed state | Never advance witness automatically; preserve and lock | No |
| Same sequence, different digest | Fork/tamper | Lock namespace and escalate | No |
| Retention unknown or incomplete | Objects may be mutable | No receipt; no downgrade of policy | No |
| Audit checkpoint missing/conflicting | Audit incomplete | Reconcile exact event; never skip/rewrite chain | No |
| Key revoked before publish | Signature may no longer be trusted | Quarantine; check current trust/revocation; approved re-sign policy only | No |
| Corruption after publication | Previously valid evidence now suspect | Retain artifacts; publish revocation/corruption status; recovery blocks | Not usable |

## 6. Normative recovery procedure

1. Enter RECOVERY_LOCKED; disable new commits, receipt issuance and positive status for affected namespace.
2. Preserve metadata, descriptor, raw objects, IDs, witness responses, retention state, audit checkpoints and signer records. Do not repair in place.
3. Strictly validate descriptor encoding, operation ID and digest. Missing/corrupt descriptor requires manual incident recovery.
4. Query witness by namespace + operation ID + digest over fresh authenticated channel.
5. Query metadata by exact operation ID/digest.
6. Retrieve every object by immutable ID; verify exact length/hash and image == slot0 || slot1.
7. Query retention for every object and exact policy generation.
8. Query audit event/checkpoint by fixed event ID/digest.
9. Query signer for exact receipt request if signing may have started; verify returned bytes and current trust.
10. Build authenticated fact set including source, generation, sequence, digest, freshness and status. Caller-supplied Boolean is not proof.
11. If any fact is missing, stale, conflicting or unverifiable, remain locked.
12. Resume only the next idempotent step explicitly permitted by the matching matrix row; never skip a phase.
13. Append and externally checkpoint recovery decision before reopening service.
14. Read/status and new write/receipt gates are separate operational approvals.

## 7. Abandoned reservations and orphan objects

A RESERVED sequence is never returned to the pool. If commit cannot complete, ABORT must preserve operation ID, sequence, reason and evidence digest; the sequence remains burned. Uploaded objects and metadata remain forensic evidence under approved retention. Abort is audited/checkpointed; no receipt is issued. If witness cannot prove whether reservation committed, remain locked; no timeout-based auto-abort.

Objects written before PREPARED may be orphaned. Keep them immutable and discoverable by operation descriptor. Garbage collection must exclude objects referenced by PREPARED/COMMITTED metadata, witness reservations/finalizations, unresolved descriptors, incident/legal holds, or UNKNOWN status. GC requires its own audited anti-rollback mark-and-sweep protocol and is not authorized here.

## 8. Cross-authority issuance invariant

Receipt issuance requires all of the following simultaneously:
- descriptor digest agrees across intent, metadata, witness and audit;
- all object bytes match lengths/hashes; image equals exact slot concatenation;
- metadata is COMMITTED at exact generation/revision;
- witness is EVIDENCE_COMMITTED for exact sequence and digest;
- every object has enforced retention at exact policy generation;
- independent read-after-write verification passed;
- audit checkpoint binds exact operation and relevant digests;
- signing key and trust bundle are currently authorized;
- receipt binds all facts and signature verifies;
- live status returns exact COMMITTED receipt/package binding.

Missing or UNKNOWN fact makes the conjunction false.

## 9. Required dependent-contract revisions

Before implementation:
1. Revise Remote Monotonic Witness protocol for RESERVE/FINALIZE/ABORT/STATUS.
2. Freeze canonical descriptor and digest preimage, including retention/audit binding.
3. Define metadata DB isolation, CAS, WAL durability and restore behavior.
4. Define object-store immutable IDs, durability, read-after-write and retention proof.
5. Define audit event/checkpoint idempotency and independent anchoring.
6. Define signer idempotency and exact signed-object retrieval after lost response.
7. Define trusted time, revocation freshness, service generation and fencing.
8. Define garbage collection separately; default is no deletion.

## 10. Acceptance tests

1. Crash before/after every phase and every write/flush/response boundary.
2. Drop every request and response independently; exact retry resolves one result.
3. Corrupt descriptor, metadata, object, witness, retention marker, audit and receipt independently.
4. Restore older metadata/object/audit backups while witness remains current.
5. Restore witness snapshot and prove independent anti-rollback detects it.
6. Reuse operation ID with changed digest at each subsystem; all reject and lock.
7. Race concurrent RESERVE operations; exactly one wins.
8. Crash after reservation; prove sequence never reused and abort explicit.
9. Fail retention for one object; no receipt.
10. Fail audit checkpoint after data commit; no receipt.
11. Lose signer response; retrieve exact signature without divergent receipt.
12. Revoke key between signing and publication; receipt not published as valid.
13. Stale/forged status or trust bundle blocks.
14. Split-brain/stale fencing token cannot finalize twice.
15. Orphans cannot be garbage-collected while unresolved.
16. Every blocked state has no COMMITTED status, receipt, Broker start, journal write or generation.
17. Independent fault injection across separate service/witness/audit/storage failure domains.
18. Independent review validates each durability claim and transition.

## 11. Freeze gates

Unresolved:
- Extend existing witness or define evidence-specific transaction protocol?
- Reservation sequence semantics and explicit abort representation?
- Exact order of metadata COMMITTED, retention activation and witness FINALIZE?
- Does audit checkpoint precede FINALIZE or is it bound by FINALIZE?
- DB transaction/isolation/WAL durability guarantees?
- Object-store retention activation proof and failure semantics?
- Signer idempotency/retrieval after lost response?
- Trusted time, fencing and recovery authority?
- Garbage-collection policy?

No implementation until each is resolved, tested and approved.

## 12. Explicit non-authorization

Design only. No endpoint, cloud account, database, object store, witness namespace, key, certificate, credential, service, persistent runtime file, adapter, mutex, ACL, source/header, test harness, network change, journal access or Production integration is created or authorized. No service transaction is executed.

Existing fail-closed selector remains unchanged. Recovery classifier remains design-only. Production: NO-GO.


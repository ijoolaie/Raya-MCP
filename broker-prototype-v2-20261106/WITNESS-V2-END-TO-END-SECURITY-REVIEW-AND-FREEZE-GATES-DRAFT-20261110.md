# Witness v2 End-to-End Security Review and Freeze Gates

Date: 2026-11-10
Status: design review; draft; not frozen; no implementation authorization
Production: NO-GO

## 1. Executive decision

The current Witness v2 design is not ready for implementation or production approval.

The RESERVE / FINALIZE / ABORT / STATUS model is directionally safer than interpreting a single sequence ADVANCE as a complete Evidence Service commit. However, the available contracts do not yet establish a complete, independently verifiable transaction across Witness, metadata, object durability, retention, audit, authorization consumption and receipt signing.

Decision: HOLD. Continue design and independent review only. Do not create a v2 endpoint, namespace, key, service, storage adapter, ACL, mutex, persistent runtime state, C implementation or production integration.

## 2. Reviewed contract set

This review considers:
- REMOTE-MONOTONIC-WITNESS-PROTOCOL-V1-DESIGN-20261109.md
- REMOTE-MONOTONIC-WITNESS-PROTOCOL-V2-TRANSACTION-STATE-ADDENDUM-DRAFT-20261110.md
- EVIDENCE-SERVICE-COMMIT-CRASH-RECOVERY-MATRIX-V1-DESIGN-20261110.md
- WITNESS-V2-FINALIZE-FACT-ATTESTATION-V1-CONTRACT-DRAFT-20261110.md
- WITNESS-V2-ABORT-AUTHORIZATION-V1-CONTRACT-DRAFT-20261110.md
- PENDING-OPERATION-JOURNAL-V1-DESIGN-20261109.md
- PENDING-JOURNAL-AUTHORIZED-RECOVERY-PROCEDURE-V1-DESIGN-20261110.md
- RECOVERY-AUTHORIZATION-V1-CANONICAL-SCHEMA-AND-VERIFICATION-CONTRACT-DRAFT-20261110.md
- EVIDENCE-RECEIPT-V1-CAPTURE-INTEGRITY-RETENTION-VERIFICATION-CONTRACT-DRAFT-20261110.md
- EVIDENCE-SERVICE-TRUST-AND-ANTI-ROLLBACK-V1-DESIGN-20261110.md

This is a contract-level review, not a source-code audit or provider assurance report.

## 3. Critical findings

### C1. Cross-system commit is not atomic

Witness can atomically update only its own authoritative state. It cannot make metadata, object storage, retention and audit atomic with that update.

RESERVE / FINALIZE correctly separates sequence consumption from evidence commit, but FINALIZE still depends on external attestations. If one external system later rolls back or loses data, Witness's EVIDENCE_COMMITTED fact remains historically true while the current evidence package may no longer satisfy the receipt claim.

Required decision:
- Define the semantic meaning of EVIDENCE_COMMITTED as a point-in-time verified commit, not a perpetual guarantee of present availability; and
- define post-commit integrity failure, receipt suspension/revocation, retention breach and incident handling as separate append-only states/events.
No transition may rewrite history or decrement sequence.

### C2. Fact attestations risk becoming signed assertions without proof

Signatures authenticate issuers, not the truth of their observations. A coordinator-supplied digest of a claimed verification is not independently meaningful unless the Witness can validate the issuer, role, scope, freshness, policy and underlying proof.

Required:
- frozen issuer trust graph and role constraints;
- provider-specific evidence semantics for each assertion;
- verifier independence and compromise-domain analysis;
- signed attestation envelope with canonical encoding;
- revocation and trust-bundle anti-rollback;
- bounded freshness and trusted-time evidence;
- explicit proof limitations in any receipt.

A boolean or unsigned status field must never satisfy a required fact.

### C3. ABORT authorization consumption has a distributed atomicity gap

The ABORT draft reserves authorization consumption externally and then asks Witness to commit ABORTED. A crash can occur after Witness ABORTED but before the consumption service records CONSUMED, or vice versa. A simple RESERVE/COMMIT token protocol does not make two authorities atomic.

Required:
- define a durable authorization state machine with idempotent operation ID and queryable outcome;
- bind the consumption reservation to the exact Witness abort transaction;
- define the authoritative linearization point;
- prove recovery for every ordering and lost response;
- prevent a RESERVED authorization from being re-targeted or released after uncertain outcome;
- define whether Witness itself atomically records authorization_id/digest and the consumption proof.
No claim of exactly-once across two services without a concrete protocol and failure proof.

### C4. FINALIZE and ABORT serialization is underspecified

The Witness must serialize both terminal transitions at one authoritative per-namespace operation record. An external pre-check is insufficient.

Required:
- one atomic compare-and-transition from RESERVED;
- immutable terminal result;
- exact idempotency digest;
- concurrent FINALIZE/ABORT tests across replicas and failover;
- STATUS linearizable against in-flight terminal transitions.

### C5. Single unresolved reservation limits availability

One unresolved operation per namespace simplifies ordering but can block all future work indefinitely if the operation cannot be finalized or authorized for abort.

Required:
- explicit operational SLO and escalation process;
- no timeout-based implicit abort;
- bounded incident response without unsafe sequence reuse;
- namespace retirement/re-provisioning policy;
- evaluate partitioning into independently governed namespaces only after proving isolation and rollback domains.

### C6. Witness anti-rollback authority remains external and unspecified

A database backup, VM snapshot, replica promotion or operator restore can roll Witness state backward unless an independent monotonic anchor detects it.

Required:
- exact independent anchor design and failure model;
- linearizable quorum/leader semantics;
- fencing enforced at mutation boundaries;
- restore/clone/split-brain tests;
- no automatic selection of numerically largest state;
- namespace lock/retirement when continuity cannot be proven.

### C7. Receipt lifecycle is incomplete

The receipt signer and Evidence Service receipt persistence may fail after Witness FINALIZE. Conversely, a receipt may be delivered and later the object/retention/audit authority may be compromised.

Required:
- deterministic receipt ID and payload;
- signer idempotency/status and lost-response behavior;
- receipt status/revocation/suspension protocol;
- verifier behavior for historical receipts;
- precise statement of point-in-time guarantees vs ongoing retention;
- append-only correction and incident records.

### C8. Audit checkpoint ordering and recursion need definition

The contracts require audit of FINALIZE and ABORT, but a pre-FINALIZE checkpoint cannot contain the final Witness outcome that has not yet happened. A post-FINALIZE checkpoint can fail after the Witness has committed.

Required:
- distinguish precondition checkpoint (RESERVE, object, metadata, retention facts) from post-transition checkpoint (FINALIZE/ABORT result);
- define whether receipt requires both checkpoints;
- define audit failure after terminal Witness transition and its recovery;
- avoid circular requirement that a Witness commit wait for an audit event that itself records the Witness commit.

## 4. Required end-to-end state model

The following dimensions must remain distinct; do not compress them into one transaction status:

1. Witness operation: ABSENT, RESERVED, EVIDENCE_COMMITTED, ABORTED, UNKNOWN.
2. Evidence transaction: ABSENT, PREPARED, COMMITTED, ABORTED, CONFLICT, UNKNOWN.
3. Objects: NOT_PRESENT, PARTIAL, VERIFIED, CORRUPT, UNKNOWN.
4. Retention: NOT_ACTIVE, ACTIVE_EXACT, BREACHED, UNKNOWN.
5. Audit: PRECHECKPOINTED, TERMINAL_EVENT_PENDING, TERMINAL_CHECKPOINTED, GAP, UNKNOWN.
6. Authorization: UNUSED, RESERVED_FOR_TRANSACTION, CONSUMED, REVOKED, EXPIRED, UNKNOWN.
7. Receipt: NOT_CREATED, SIGNED, PERSISTED, DELIVERED, SUSPENDED, REVOKED, UNKNOWN.
8. Leadership: CURRENT_FENCED, STALE, UNKNOWN.
9. Namespace: ACTIVE, RECOVERY_LOCKED, RETIRED.

Every transition needs an owner, durable linearization point, exact request digest, idempotency key, status query, fencing rule and crash-recovery action.

## 5. Proposed lifecycle (candidate, not frozen)

### Phase A — Prepare evidence

1. Reserve the Evidence Service operation ID and immutable descriptor.
2. Upload immutable objects and independently verify bytes and provider durability.
3. Persist metadata PREPARED.
4. Activate and independently verify retention.
5. Create and independently checkpoint a precondition audit event set.

### Phase B — Reserve Witness sequence

6. Call Witness RESERVE with exact descriptor digest, expected sequence, operation ID and current fencing epoch.
7. Persist the returned signed RESERVED result in Evidence Service metadata and local pending journal.
8. If response is lost, query exact STATUS; do not allocate a new ID.

### Phase C — Finalize evidence commit

9. Obtain independent object, metadata, retention and precondition-audit attestations.
10. Coordinator verifies the complete set and constructs one canonical finalization digest.
11. Witness atomically transitions RESERVED -> EVIDENCE_COMMITTED.
12. Persist the signed Witness result in metadata and append a terminal audit event.
13. Obtain independent post-transition audit checkpoint.

This candidate ordering means Witness EVIDENCE_COMMITTED is a point-in-time assertion that the preconditions were independently attested. It does not imply that the post-transition audit checkpoint or receipt is already complete.

### Phase D — Issue receipt

14. Verify Witness terminal result, current object/retention status, metadata, both audit checkpoints, signer trust and namespace state.
15. Sign deterministic receipt payload.
16. Persist receipt and issuance audit event.
17. Obtain required issuance checkpoint.
18. Deliver the exact persisted receipt bytes; lost response is resolved by receipt ID/status.

Whether post-transition and issuance checkpoints are both mandatory before delivery is an unresolved policy decision. Conservative default: yes.

### Phase E — Abort

If evidence cannot be completed:
19. Keep operation RESERVED and namespace recovery-locked while facts are uncertain.
20. Obtain exact fresh status and preserve evidence.
21. Obtain independent signed ABORT authorization with separation of duties.
22. Serialize ABORT against FINALIZE at Witness.
23. Persist ABORTED result, append and checkpoint terminal audit, reconcile Evidence Service metadata.
24. Keep sequence consumed and preserve evidence/retention.

No automatic abort or sequence release.

## 6. ABORT consumption protocol: unresolved alternatives

### Option A — Witness-native authorization consumption

Witness verifies signed authorization and atomically stores authorization_id/digest with the ABORTED transition. This gives one Witness linearization point for both use and terminal operation state. The external Recovery Authority still needs anti-replay issuance records and revocation, but a separate online consumption service may not be required for the terminal transition.

Risks/questions:
- Witness must validate current issuer trust and authorization status;
- revocation freshness and emergency key compromise handling;
- whether authorization can be revoked after issuance but before use;
- replay behavior across Witness restore;
- independent anti-rollback of Witness authorization records.

### Option B — External consumption service reservation + Witness transition

Consumption service reserves authorization for exact recovery_transaction_id; Witness validates proof and commits ABORT; consumption service finalizes consumption afterward.

Risks:
- crash after Witness ABORT but before consumption finalize;
- consumption service unavailable after Witness transition;
- reservation status may remain uncertain;
- exactly-once semantics require idempotent cross-service reconciliation, not assumed atomicity.

### Option C — Two-phase authorization coordinator

A dedicated independent coordinator serializes authorization consumption and Witness terminal transition using a durable intent log and explicit recovery protocol.

Risks:
- introduces another highly privileged monotonic authority;
- coordinator itself needs anti-rollback, fencing, audit and disaster recovery;
- more complexity and a larger trust boundary.

Decision: no option selected. Evaluate A first for reduced cross-system atomicity, but only if Witness trust/revocation and anti-rollback properties can be demonstrated. This is not an implementation recommendation or approval.

## 7. Required canonical objects

Before implementation freeze, define and publish deterministic schemas and test vectors for:
- reservation descriptor;
- RESERVE request and response;
- object integrity/durability attestation;
- metadata commit attestation;
- retention enforcement attestation;
- audit precondition checkpoint attestation;
- coordinator aggregation;
- FINALIZE request and response;
- ABORT authorization and request/response;
- STATUS and READ response;
- terminal audit event and checkpoint;
- Evidence Receipt, receipt status and revocation event;
- fencing token and leader generation.

For each: encoding, field IDs, maximum lengths, unknown-field behavior, canonical digest preimage, domain separation, signature profile, issuer roles, freshness and replay rules.

## 8. Consolidated freeze gates

### G1 — Protocol and state semantics
- v2 profile and namespace separation from v1.
- Exact state machine, legal transitions and sequence counters.
- One unresolved reservation policy and retirement.
- Linearizable STATUS and terminal transition semantics.

### G2 — Canonical data and cryptography
- Deterministic encoding and schema evolution.
- Signature algorithms/providers and Windows CNG compatibility.
- Domain-separated digest test vectors.
- Trusted time, expiry and trust-bundle anti-rollback.

### G3 — Independent authorities
- Object verifier, metadata verifier, retention verifier, audit authority and coordinator trust graph.
- Compromise-domain and separation-of-duties proof.
- Provider-specific durability/retention semantics.
- Key custody, rotation, revocation and disaster recovery.

### G4 — Anti-rollback and fencing
- Independent Witness monotonic anchor.
- Quorum, leader fencing and split-brain behavior.
- Backup/restore/clone/VM rollback acceptance.
- Mutation-boundary enforcement by every subsystem.

### G5 — Crash recovery
- Durable intent and status semantics for each operation.
- Lost request/response and uncertain outcome handling.
- ABORT authorization consumption linearization.
- Metadata/Witness divergence and terminal audit failure recovery.

### G6 — Receipt lifecycle
- Point-in-time meaning and limitations.
- Deterministic receipt ID/payload and signer idempotency.
- Persistence/delivery recovery.
- Suspension/revocation and historical verification.

### G7 — Operations and governance
- Incident triggers, approvers and separation of duties.
- SLO for blocked reservation and namespace retirement.
- Audit retention, privacy and evidence access.
- Independent security, legal and operational approval.

No gate is considered passed by document existence alone. Each requires evidence, owner, test result and independent sign-off.

## 9. Minimum integrated acceptance campaign

1. Model-check all per-namespace state transitions and invariants.
2. Concurrent RESERVE, FINALIZE, ABORT and STATUS across multiple replicas.
3. Crash/failover after every durable write, flush, quorum commit and response boundary.
4. Lost request and lost response for every external subsystem.
5. Stale leader writes after fencing, including partitioned old leader.
6. Restore stale Witness, metadata, object index, retention config, audit DB and signer trust independently and in combinations.
7. Clone/VM snapshot rollback and namespace duplication.
8. Same ID/same digest and same ID/different digest for every operation.
9. Authorization replay, revocation, expiry, trusted-time loss and consumption-service outage.
10. Partial/corrupt object, stale replica, retention weakening, audit gap and checkpoint fork.
11. Signer duplicate, lost response, key compromise and receipt revocation.
12. Verify no receipt or Broker startup in every UNKNOWN/conflict/degraded state.
13. Verify no sequence reuse, no history rewrite, no silent lower-revision fallback.
14. Independent red-team review and operator recovery exercise.

## 10. Implementation entry criteria

Implementation planning may begin only after:
- all G1-G7 decisions are explicitly approved;
- canonical schemas and vectors are frozen;
- provider and platform primitives are proven on the target Windows build;
- threat model and trust graph have independent sign-off;
- crash matrix has no unresolved state with an unsafe action;
- test harness and fault-injection plan are reviewed;
- rollback/restore and incident runbooks are approved.

Even then, first implementation must be isolated prototype code with synthetic authorities and no service installation, persistent privileged state, production credentials or network exposure. Production remains separately gated.

## 11. Current decision register

| ID | Decision | Current status |
|---|---|---|
| D1 | v2 separate namespace/profile vs migration | OPEN; default separate |
| D2 | EVIDENCE_COMMITTED point-in-time semantics | PROPOSED; needs approval |
| D3 | ABORT authorization consumption A/B/C | OPEN |
| D4 | Pre/post audit checkpoints and receipt gate | OPEN |
| D5 | Witness anti-rollback anchor | OPEN |
| D6 | FINALIZE fact attestation trust graph | OPEN |
| D7 | Receipt suspension/revocation lifecycle | OPEN |
| D8 | One unresolved reservation per namespace | PROPOSED; needs operational review |
| D9 | Canonical encoding/signature/digest profile | OPEN |
| D10 | Provider-specific durability/retention proof | OPEN |

## 12. Explicit non-authorization

This document is a design review only. No code, endpoint, namespace, service, database, object store, key, certificate, credential, HSM, network rule, persistent runtime file, adapter, mutex, ACL, service identity, recovery authorization or Production integration is created or authorized.

Existing v1 protocol, slot selector, journal and Broker runtime remain unchanged. No service is installed or started; no journal or Witness data is read or written by this design task. IranPriceScout and its independent tunnel remain untouched.

Production: NO-GO.

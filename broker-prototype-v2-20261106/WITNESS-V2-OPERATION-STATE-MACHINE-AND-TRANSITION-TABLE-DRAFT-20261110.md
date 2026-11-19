# Witness v2 Operation State Machine and Transition Table — Draft

Date: 2026-11-10
Status: design-only; draft; not frozen; no implementation authorization
Production: NO-GO

## 1. Purpose and authority boundary

This document defines a candidate per-operation Witness state machine and the required transition contract for RESERVE, FINALIZE, ABORT and STATUS. It is derived from the end-to-end security review dated 2026-11-10.

This is not a complete Evidence Service state machine. Object storage, metadata, retention, audit, authorization, receipt, leadership and namespace states remain independent dimensions. Witness state must never be treated as proof that those external systems are currently healthy.

No runtime, endpoint, namespace, service, key, storage adapter, ACL, mutex, C code, persistent state or Production integration is authorized by this document.

## 2. Core invariants

I1. A namespace sequence is strictly monotonic and is never reused, including after ABORT.

I2. A reservation has one immutable operation_id, namespace_id, sequence, descriptor_digest and canonical request digest.

I3. For one operation, RESERVED can transition to exactly one terminal state: EVIDENCE_COMMITTED or ABORTED. No transition exists between terminal states.

I4. FINALIZE and ABORT are serialized by one authoritative linearizable compare-and-transition at Witness. External pre-checks do not establish exclusivity.

I5. A repeated request with the same operation identity and same canonical digest returns the recorded outcome. Same identity with a different digest is a conflict/security event, never a retry.

I6. A timeout, disconnect, stale replica response, missing response or UNKNOWN result is not evidence of failure or success. Resolve using exact STATUS against the authoritative consistency boundary.

I7. No lease expiration, clock timeout, operator timeout or service restart implicitly aborts a reservation or releases its sequence.

I8. An uncertain write or failed quorum leaves the namespace/operation recovery-locked until authoritative outcome is established.

I9. STATUS must not return a stale NOT_FOUND that can race with a committed RESERVE. NOT_FOUND must be linearizable with respect to the authoritative namespace log.

I10. Fencing is checked at the mutation boundary. A client-side lease check alone is insufficient.

I11. Witness EVIDENCE_COMMITTED is a point-in-time statement that the accepted finalization preconditions were validated and recorded. It is not a perpetual guarantee of object availability, retention enforcement or future service health.

I12. Witness ABORTED means the sequence is consumed and this operation will not be finalized. It does not authorize deletion, retention shortening or destruction of evidence.

I13. No receipt is issued solely because Witness reports EVIDENCE_COMMITTED. Receipt issuance has independent gates.

I14. No Broker startup or privileged operation is permitted from a Witness state-machine result alone.

## 3. State definitions

### 3.1 Namespace state

- UNPROVISIONED: no authorized namespace genesis record exists.
- ACTIVE: namespace accepts operations subject to sequence and concurrency policy.
- RECOVERY_LOCKED: mutations are blocked while state continuity or an operation outcome is uncertain.
- RETIRED: terminal administrative state; no new operations. Historical STATUS/read-only verification may remain available.

Candidate policy: one unresolved RESERVED operation per namespace. This policy is conservative but has availability costs and remains subject to approval.

### 3.2 Operation state

- ABSENT: no operation record exists at the authoritative consistency boundary.
- RESERVED: sequence is consumed and operation may proceed to FINALIZE or authorized ABORT.
- EVIDENCE_COMMITTED: terminal success state; immutable.
- ABORTED: terminal abort state; immutable; sequence remains consumed.
- UNKNOWN: caller cannot establish the authoritative state. UNKNOWN is an observer/recovery condition, not a persisted success/failure state.

### 3.3 Request outcome state

- NOT_SUBMITTED: caller has not transmitted the request.
- SUBMITTED_UNKNOWN: request may have reached Witness; response not authoritative/available.
- CONFIRMED: signed/ authenticated authoritative result has been verified.
- CONFLICT: same identity was presented with a different digest or incompatible transition.
- RECOVERY_REQUIRED: continuity, fencing, quorum or durable outcome cannot be proven.

## 4. Candidate transition table

| Current state | Request | Required guards | Atomic Witness action | Result |
|---|---|---|---|---|
| ABSENT | RESERVE | Namespace ACTIVE; current fencing token; expected sequence exact; operation ID unused; descriptor canonical; no unresolved reservation | Append one reservation record and consume next sequence | RESERVED |
| ABSENT | RESERVE duplicate | Exact same operation ID and exact same canonical digest already committed | No mutation; return original reservation | RESERVED |
| ABSENT | RESERVE conflict | Operation ID exists with different digest, or expected sequence stale/future | No mutation; return conflict/stale-sequence result | ABSENT or existing state unchanged |
| RESERVED | RESERVE same | Exact identity/digest match | No mutation; return existing reservation | RESERVED |
| RESERVED | FINALIZE | Fresh current fencing; exact operation/sequence/digest; all required signed fact attestations valid; operation still RESERVED | Atomic compare-and-transition plus durable quorum commit | EVIDENCE_COMMITTED |
| RESERVED | ABORT | Fresh current fencing; exact operation/sequence/digest; valid single-use authorization; exact ABORT reason; operation still RESERVED | Atomic compare-and-transition recording authorization identity/digest | ABORTED |
| EVIDENCE_COMMITTED | FINALIZE same | Exact original finalization digest | No mutation; return terminal result | EVIDENCE_COMMITTED |
| ABORTED | ABORT same | Exact original abort digest and authorization identity | No mutation; return terminal result | ABORTED |
| EVIDENCE_COMMITTED | ABORT | Any | No mutation; terminal conflict | EVIDENCE_COMMITTED |
| ABORTED | FINALIZE | Any | No mutation; terminal conflict | ABORTED |
| Any known state | STATUS | Exact namespace and operation identity; authoritative linearizable read | No mutation | Current state or authoritative NOT_FOUND |
| Any state | Mutating request with stale fencing | Fencing validation fails at mutation boundary | No mutation | STALE_FENCE |
| Any state | Same identity, different digest | Canonical identity/digest mismatch | No mutation; record security/audit event per policy | CONFLICT |
| Any state | Uncertain quorum/commit outcome | Durable outcome cannot be established | Do not guess or retry under a new identity; lock namespace if necessary | RECOVERY_REQUIRED |
| RETIRED namespace | RESERVE | Always denied | No mutation | RETIRED |
| RECOVERY_LOCKED namespace | Any mutation | Always denied pending authorized recovery | No mutation | RECOVERY_REQUIRED |

The table does not authorize any transition until schemas, attestation profiles, fencing, durable commit semantics and recovery behavior are frozen.

## 5. RESERVE contract

### Input identity

A RESERVE request must bind at minimum:
- protocol profile/version;
- namespace_id and namespace generation;
- operation_id;
- expected previous sequence and requested next sequence;
- immutable descriptor digest;
- canonical request digest;
- current fencing token/generation;
- authenticated caller identity and role.

The exact schema and canonical encoding remain OPEN.

### Preconditions

- Namespace is ACTIVE and not RECOVERY_LOCKED or RETIRED.
- Caller and leader are authenticated.
- Fencing token is current at the authoritative mutation boundary.
- Sequence is exactly the next permitted value.
- No unresolved reservation exists under the selected one-in-flight policy.
- Operation ID has never been bound to a different request.
- Namespace anti-rollback continuity is proven.

### Linearization and response

The reservation becomes effective only at the Witness authoritative durable commit point (exact quorum/log semantics OPEN). The response must bind operation ID, sequence, descriptor digest, request digest, namespace generation, Witness commit index/term or equivalent, and signer identity.

If the response is lost, caller queries STATUS using the same exact identity. It must not issue a new operation ID to bypass uncertainty.

## 6. FINALIZE contract

### Preconditions

- Operation is RESERVED at the authoritative boundary.
- Exact operation ID, namespace, sequence and descriptor digest match.
- Current fencing token is valid at mutation.
- All required attestations are signed, fresh, non-revoked, role-authorized and bound to the same transaction.
- Attestation issuers satisfy the frozen independence/compromise-domain policy.
- Canonical finalization digest is deterministic.
- Required precondition audit checkpoint is verified.
- No ABORT terminal transition has committed.

### Linearization

One atomic compare-and-transition from RESERVED to EVIDENCE_COMMITTED. The committed record must retain the exact finalization digest and attestation-set digest. A concurrent ABORT can win or FINALIZE can win, but never both.

### Post-commit

- Persist the signed terminal result in Evidence Service metadata.
- Append a terminal audit event and obtain the required post-transition checkpoint.
- Reconcile local pending journal and external metadata.
- Receipt issuance remains blocked until all receipt-specific checks pass.

If Witness committed but the response is lost, STATUS must return the exact committed result. If external metadata/audit is behind, reconcile; do not undo Witness history.

## 7. ABORT contract

### Preconditions

- Operation is RESERVED at the authoritative boundary.
- Exact operation identity and digest match.
- Current fencing token is valid at mutation.
- Authorization is valid, single-use, not revoked/expired, and bound to exact namespace, operation, sequence, reason and recovery transaction.
- Required separation of duties is proven.
- Exact ABORT reason is allowlisted and canonical.
- Evidence and audit preservation obligations are active.
- No FINALIZE terminal transition has committed.

### Linearization

One atomic compare-and-transition from RESERVED to ABORTED. The Witness record must include authorization ID, authorization digest, issuer/key identity, reason code and authorization-consumption semantics. Whether Witness-native consumption is selected remains OPEN.

### Post-commit

- Sequence remains consumed.
- Evidence is preserved; ABORT does not authorize deletion or retention reduction.
- Persist terminal result in metadata and audit.
- Reconcile authorization consumption outcome.
- A lost response is resolved by exact STATUS; never reuse the authorization for another operation.

If authorization consumption cannot be reconciled, keep the namespace recovery-locked even if ABORTED is confirmed, until authorization ledger consistency is restored.

## 8. STATUS contract

STATUS is a security-critical read operation.

Required:
- authoritative linearizable read or a proof that the read is at least as fresh as a specified commit index;
- exact namespace generation and operation identity;
- authenticated response binding requested identity, observed state, sequence, descriptor/request digest, log index/term, leader/fencing generation and freshness proof;
- explicit distinction among NOT_FOUND, RESERVED, EVIDENCE_COMMITTED, ABORTED, RECOVERY_REQUIRED and STALE/UNAVAILABLE;
- no stale replica may return authoritative NOT_FOUND;
- if linearizability cannot be established, return UNKNOWN/UNAVAILABLE, never NOT_FOUND.

STATUS must be safe to retry and must not mutate state.

## 9. Idempotency and conflict rules

| Case | Required behavior |
|---|---|
| Same operation ID + same request digest | Return exact recorded result |
| Same operation ID + different request digest | CONFLICT; security/audit event |
| Same terminal operation + same terminal digest | Return exact terminal result |
| Same terminal operation + different terminal digest | CONFLICT |
| Different operation ID + already-consumed sequence | Reject |
| Old expected sequence | Reject as stale; do not repair automatically |
| Future expected sequence | Reject; no gaps unless explicit protocol rule is frozen |
| Unknown request outcome | STATUS exact identity; no new ID |
| Duplicate ABORT authorization | Reject reuse; return original outcome only for exact same transaction |

Canonical digest construction, operation-ID uniqueness scope and authorization consumption remain freeze decisions.

## 10. Failure and recovery decision table

| Failure observation | Safe interpretation | Required action | Forbidden action |
|---|---|---|---|
| RESERVE timeout | SUBMITTED_UNKNOWN | Exact STATUS; lock if unavailable | Allocate replacement ID |
| FINALIZE timeout | SUBMITTED_UNKNOWN | Exact STATUS; reconcile metadata/audit | Assume failed and ABORT |
| ABORT timeout | SUBMITTED_UNKNOWN | Exact STATUS and authorization status | Reuse authorization |
| STATUS stale/unavailable | UNKNOWN | Retry authoritative route or recovery escalation | Treat NOT_FOUND as fact |
| Quorum/leader loss during mutation | UNKNOWN | Fence old leader; recover from monotonic authority | Accept local disk state as authority |
| Conflicting terminal results | Security failure | RECOVERY_LOCKED; preserve evidence | Choose latest timestamp or highest local revision |
| Witness backup restore detected | ROLLBACK_SUSPECTED | RECOVERY_LOCKED; independent witness reconciliation | Auto-restore or continue |
| Attestation issuer unavailable | AUTHORITY_UNAVAILABLE | Keep RESERVED; escalate | Substitute unsigned boolean |
| Trusted time unavailable | UNKNOWN | Block expiry/revocation decisions | Treat authorization as valid |
| Post-FINALIZE audit failure | EVIDENCE_COMMITTED + AUDIT_GAP | Block receipt; repair audit through append-only process | Reverse FINALIZE |
| Receipt signer/persistence failure | No receipt / outcome unknown | Query signer/store by deterministic receipt ID | Create a new unrelated receipt ID |
| ABORT consumption unresolved | ABORTED or UNKNOWN + AUTHORIZATION_GAP | Recovery-lock; reconcile exact authorization transaction | Reuse/release token |

## 11. Cross-system state constraints

The following are candidate safety constraints, not automatic transitions:

- Witness RESERVED + metadata PREPARED + objects VERIFIED + retention ACTIVE_EXACT + pre-audit checkpoint valid: FINALIZE may be considered, subject to complete attestations.
- Witness EVIDENCE_COMMITTED + metadata PREPARED: no new terminal transition; reconcile metadata and terminal audit.
- Witness EVIDENCE_COMMITTED + audit GAP: receipt blocked.
- Witness EVIDENCE_COMMITTED + objects CORRUPT or retention BREACHED: preserve historical commit; mark incident, suspend/revoke receipt as policy dictates.
- Witness ABORTED + metadata PREPARED: reconcile metadata to terminal ABORTED; preserve objects and retention.
- Witness ABORTED + authorization consumption UNKNOWN: namespace recovery-locked; authorization cannot be reused.
- Any UNKNOWN, CONFLICT, ROLLBACK_SUSPECTED, STALE_FENCE or RECOVERY_REQUIRED: no receipt, no new reservation, no Broker startup.
- Witness state alone never proves current evidence availability or retention.

## 12. Model-checking properties

A future executable model must prove at minimum:

P1. Never both EVIDENCE_COMMITTED and ABORTED for one operation.
P2. Sequence never decreases or repeats.
P3. A sequence is consumed at most once.
P4. No terminal transition from ABSENT.
P5. No terminal transition except from RESERVED.
P6. No mutation accepted with stale fencing.
P7. Same identity/different digest never mutates state.
P8. No implicit ABORT due to timeout, lease expiry or restart.
P9. Unknown outcomes never produce a new operation identity automatically.
P10. Stale STATUS never produces authoritative NOT_FOUND.
P11. Receipt cannot be issued without independent receipt gate.
P12. No state with uncertainty permits Broker startup.
P13. Restore/clone cannot reduce authoritative sequence or replay terminal authorization.
P14. FINALIZE/ABORT races produce exactly one terminal winner.
P15. Repeated exact requests converge to the same immutable result.

Liveness properties must be separately stated and must not weaken safety. A permanently unavailable independent authority may leave a namespace blocked; that is preferable to unsafe automatic recovery.

## 13. Required test matrix

- All legal and illegal transition pairs.
- RESERVE duplicate, digest mismatch, stale/future sequence, concurrent callers.
- FINALIZE/ABORT race under same and different replicas.
- Crash before and after each durable log/quorum boundary.
- Lost request, lost response, delayed response and duplicated response.
- Stale leader after fencing, network partition and leader failover.
- STATUS linearizability during in-flight mutation and replica lag.
- Backup restore, VM snapshot, clone, split brain and monotonic anchor outage.
- Attestation replay, wrong issuer/role, stale proof, revoked key, malformed canonical digest.
- Authorization replay, expiry, revocation, trusted-time failure and uncertain consumption.
- Post-commit audit gap, receipt signer failure, persistence failure and revocation.
- Fuzzing of all parsers and canonical encoders.
- Independent model review and operator recovery drill.

## 14. Open decisions before freeze

D1. Exact quorum/linearizability and durable commit semantics.
D2. Independent anti-rollback anchor and witness-of-witness design.
D3. Namespace generation, sequence width, overflow and retirement.
D4. Operation ID uniqueness scope and canonical digest schema.
D5. One-in-flight reservation policy and operational SLO.
D6. Fencing token issuer, epoch persistence and enforcement at mutation.
D7. FINALIZE attestation schema, issuer graph and proof validation.
D8. ABORT authorization consumption model (Witness-native vs external).
D9. STATUS freshness proof and authoritative NOT_FOUND semantics.
D10. Pre/post audit checkpoint and receipt policy.
D11. Receipt suspension/revocation and historical verification.
D12. Recovery procedures for every uncertain outcome.
D13. Trusted time and key/trust-bundle anti-rollback.
D14. Independent security and operations approval.

## 15. Freeze and implementation gate

This state machine remains DRAFT. It must not be translated into C or wired into runtime until all D1-D14 are resolved, canonical test vectors are frozen, an executable model passes, and independent reviewers approve the threat model and crash-recovery proof.

Any future implementation starts as isolated prototype code with synthetic authorities only. No service installation, persistent privileged state, production credentials, network exposure or Production integration is implied.

## 16. Explicit non-authorization

No code, endpoint, service, namespace, database, object store, key, certificate, credential, HSM, network rule, persistent runtime file, adapter, mutex, ACL, service identity, recovery action or Production integration is created or authorized.

Existing v1 protocol, Broker prototype and all existing files remain unchanged. No service is installed or started; no runtime journal or Witness state is read or written. IranPriceScout and its independent tunnel remain untouched.

Production: NO-GO.

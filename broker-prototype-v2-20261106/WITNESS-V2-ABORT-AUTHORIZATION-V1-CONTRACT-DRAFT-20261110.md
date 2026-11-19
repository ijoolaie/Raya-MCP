# Witness v2 ABORT Authorization v1 Contract (Draft)

Date: 2026-11-10
Status: design-only; draft; not frozen; no implementation authorization
Production: NO-GO

## 1. Purpose and limits

Define a separately authorized, single-use decision to transition one exact Witness v2 RESERVED operation to ABORTED. ABORT permanently consumes the reserved sequence. It is not rollback, deletion, evidence erasure, sequence release, or proof that the transaction never existed.

This is a transaction-specific authorization profile, not a general administrator credential and not a substitute for the Recovery Authorization v1 contract. The existing Recovery Authorization draft is scoped to journal recovery and cannot be silently reused for Witness ABORT. A future shared envelope may be considered only after explicit schema/profile separation and review.

No automatic ABORT is permitted because of timeout, missing objects, process exit, lease expiry, service outage, expired attestation, operator convenience or age of a reservation.

## 2. Default policy

Conservative default:
- ABORT requires a signed, single-use authorization from an independent Recovery Authority.
- Requester and approver must be distinct identities.
- Witness namespace administrator, uploader, Evidence Service operator and ordinary coordinator cannot self-authorize ABORT.
- Any operation that has reached metadata PREPARED requires incident-level approval.
- Any operation with a Witness FINALIZE already committed is ineligible for ABORT.
- Unknown status, unavailable Witness, stale trust, stale time or uncertain prior outcome blocks.
- Sequence remains burned even when ABORT succeeds.
- Objects and metadata are preserved under forensic retention; deletion/garbage collection is a different separately authorized workflow.

## 3. Authorization envelope — candidate fields

A future canonical signed envelope binds exactly:
- schema_version and authorization_profile = WITNESS_V2_ABORT;
- authorization_id (16-byte nonzero unique ID);
- authorization_nonce (32-byte CSPRNG);
- incident_id and evidence_namespace_id;
- namespace_generation and current fencing_epoch;
- operation_id and descriptor_digest;
- reserved_sequence and expected operation state RESERVED;
- original RESERVE request digest;
- exact abort_request_digest and allowlisted reason_code;
- evidence/metadata/audit status snapshot digests;
- latest linearizable Witness STATUS response digest and observed version;
- evidence preservation/retention reference digest;
- requester principal/key ID and approver principal/key ID;
- approval policy version and separation-of-duties result;
- issued_at, not_before, expires_at, trusted-time evidence;
- recovery authority key ID and trust-bundle generation;
- optional external incident decision reference.

Do not include private keys, credentials, access tokens, raw evidence, arbitrary paths, free-form sensitive narratives or general-purpose permissions. Human-readable incident detail remains in the incident system and is bound by digest/reference.

## 4. Allowed reason codes

Initial allowlist proposal:
- EVIDENCE_CAPTURE_PERMANENTLY_INCOMPLETE;
- OBJECT_INTEGRITY_FAILURE_CONFIRMED;
- RETENTION_REQUIREMENT_UNSATISFIABLE;
- AUDIT_INTEGRITY_FAILURE_CONFIRMED;
- METADATA_CONFLICT_CONFIRMED;
- POLICY_OR_LEGAL_PROHIBITION;
- NAMESPACE_RETIREMENT;
- SECURITY_INCIDENT_CONTAINMENT.

Reason codes are not self-proving. Each requires a typed evidence bundle and policy-specific approvals. "TIMEOUT", "SERVICE_UNAVAILABLE", "OPERATOR_REQUEST", "OLD_RESERVATION" and generic "ERROR" are explicitly not sufficient reasons. Unknown codes reject.

## 5. Preconditions before authorization may be issued

The independent Recovery Authority must verify and bind:
1. Exact Witness STATUS is RESERVED for the specified namespace, operation ID, descriptor digest and sequence.
2. STATUS is fresh, authenticated and linearizable; no concurrent FINALIZE/ABORT is in flight under the serialization contract.
3. Namespace generation and fencing epoch are current; stale leaders are fenced.
4. Exact metadata, object, retention and audit states have been captured and independently evidenced, including UNKNOWN states.
5. Raw evidence and relevant logs are preserved; no destructive cleanup is underway.
6. Incident reason is one of the allowlisted codes and supported by evidence.
7. Requester and approver are distinct and authorized for this namespace/purpose.
8. Required legal/security/operations approvals are present.
9. No Witness EVIDENCE_COMMITTED result exists. If status is unknown or conflicting, ABORT authorization is not issued.
10. Evidence objects and metadata will remain preserved after ABORT under approved retention/hold.
11. The exact abort request digest, including reason and evidence references, is frozen.

The authorization issuer verifies facts independently; it must not rely solely on a caller-provided Boolean or screenshot.

## 6. Verification and single-use consumption

Signature verification and replay protection are separate checks.

A future verifier must:
- strictly parse the frozen canonical encoding;
- verify profile, namespace, generation, operation, descriptor, sequence and reason;
- verify issuer key, trust-bundle anti-rollback, revocation and signature;
- verify trusted time, validity window and bounded lifetime;
- verify requester/approver identities and separation of duties;
- verify incident and evidence references remain valid;
- obtain a fresh Witness STATUS and compare exact digest/version;
- verify no FINALIZE/ABORT race has invalidated the decision;
- reserve authorization_id + complete authorization digest in an independent linearizable consumption service;
- bind the reservation to one recovery_transaction_id and exact abort_request_digest.

Conceptual consumption states: UNUSED, RESERVED, CONSUMED, REVOKED, EXPIRED, CONFLICT, UNKNOWN.

Rules:
- Same ID/digest/transaction retry is idempotent.
- Same ID with different digest is CONFLICT.
- RESERVED is never silently released on timeout or disconnect.
- Unknown consumption outcome blocks; resolve by STATUS using same identifiers.
- CONSUMED, REVOKED, EXPIRED, CONFLICT and UNKNOWN never authorize another attempt.
- Consumption service must be outside the Witness host and Evidence Service host rollback domains.
- A signed token without independent consumption tracking is replayable and unacceptable.

## 7. Witness ABORT request and atomic transition

Request binds:
- namespace_id, namespace_generation, fencing_epoch;
- operation_id, descriptor_digest, reserved_sequence;
- original RESERVE digest;
- authorization_id and authorization_digest;
- recovery_transaction_id;
- abort_request_digest and reason_code;
- evidence/incident reference digest;
- consumption reservation proof.

Witness behavior:
1. Authenticate client and authorize exact ABORT purpose.
2. Validate current namespace lifecycle/generation/fencing epoch.
3. Look up operation by ID and compare every immutable field.
4. If already ABORTED with exact same abort digest, return original result idempotently.
5. If already EVIDENCE_COMMITTED, reject permanently; never reverse FINALIZE.
6. If operation is absent, different digest/sequence, or not exact RESERVED, reject.
7. Verify authorization issuer/profile/signature/trust/time and exact operation binding.
8. Verify independent consumption service reports the exact authorization reserved for this transaction.
9. Serialize against FINALIZE; exactly one terminal transition wins.
10. Durably persist RESERVED -> ABORTED, burn sequence, advance last_terminal_sequence, retain allocated_sequence, clear active reservation.
11. Return signed ABORTED result only after durable commit.

If durable outcome is uncertain, client must query STATUS for the same operation and abort digest. Never issue a new authorization merely because the response was lost. Consumption is committed only after exact Witness outcome is independently established; if consumption commit response is lost, resolve it with STATUS.

## 8. Race and ordering semantics

- FINALIZE and ABORT are mutually exclusive terminal transitions under one linearizable per-namespace serialization point.
- If FINALIZE wins, ABORT is permanently rejected and its authorization must be revoked/consumed as failed-attempt evidence under policy.
- If ABORT wins, FINALIZE is permanently rejected.
- If outcome is UNKNOWN, neither side retries with a new ID; both resolve exact status.
- An authorization issued against RESERVED status becomes stale if operation changes before use.
- Authorization cannot be re-targeted to a later sequence, different namespace, operation, reason, digest or recovery transaction.
- No lease expiry or wall-clock timeout resolves the race.
- Concurrent replay from two hosts yields one successful consumption reservation and one ABORT transition at most.

## 9. After ABORT

Witness records ABORTED as immutable terminal history. The sequence is permanently consumed and never reused. The operation is not evidence-committed and no positive Evidence Receipt may be issued for it.

Required follow-up:
- preserve original objects, metadata, journal image and all attestations;
- append independently checkpointed ABORT audit event;
- persist Witness signed result and consumption result in incident evidence;
- mark Evidence Service transaction ABORTED/RECOVERY_REQUIRED through its own idempotent metadata protocol;
- maintain retention/legal hold under independent policy;
- keep namespace blocked until coordinator reconciles Witness, metadata, object store, retention and audit;
- any later retry is a new capture/operation with a new ID and sequence, explicitly linked to the prior aborted incident. It must not overwrite or erase prior evidence.

Garbage collection, retention shortening, namespace reset and re-provisioning are not part of ABORT.

## 10. Failure taxonomy

BLOCK_ABORT_NOT_RESERVED; BLOCK_ALREADY_EVIDENCE_COMMITTED; BLOCK_WITNESS_STATUS_UNKNOWN; BLOCK_WITNESS_STATUS_STALE; BLOCK_OPERATION_BINDING_MISMATCH; BLOCK_DESCRIPTOR_MISMATCH; BLOCK_NAMESPACE_GENERATION_MISMATCH; BLOCK_FENCING_STALE; BLOCK_REASON_NOT_ALLOWED; BLOCK_EVIDENCE_NOT_PRESERVED; BLOCK_APPROVAL_MISSING; BLOCK_SEPARATION_OF_DUTIES_FAILED; BLOCK_AUTHORIZATION_SIGNATURE_INVALID; BLOCK_AUTHORIZATION_EXPIRED; BLOCK_AUTHORIZATION_REVOKED; BLOCK_TRUST_ROLLBACK; BLOCK_AUTHORIZATION_REPLAYED; BLOCK_CONSUMPTION_UNKNOWN; BLOCK_FINALIZE_ABORT_RACE_UNKNOWN; BLOCK_WITNESS_UNAVAILABLE; BLOCK_ABORT_OUTCOME_UNKNOWN.

No error implies ABORT or retries with a new authorization.

## 11. Acceptance matrix

1. Exact RESERVED operation with valid authorization can transition once to ABORTED.
2. EVIDENCE_COMMITTED cannot transition to ABORTED.
3. Unknown/unavailable/stale STATUS cannot issue or consume authorization.
4. Same ID/same digest/transaction is idempotent; same ID/different digest conflicts.
5. Authorization altered in any field/signature rejects.
6. Wrong namespace, generation, operation, descriptor, sequence or reserve digest rejects.
7. Unapproved reason code and timeout-only reason reject.
8. Same requester and approver reject.
9. Revoked/expired key, stale trust bundle or untrusted time rejects.
10. Authorization replay across host restore/VM revert/clone is blocked by independent consumption authority.
11. Lost consumption RESERVE/COMMIT response resolves by STATUS.
12. FINALIZE/ABORT concurrent race yields exactly one terminal state.
13. Crash at each Witness durable boundary leaves one queryable result.
14. ABORT sequence never reused and counters never decrease.
15. ABORT never deletes objects or shortens retention.
16. No positive receipt issued for ABORTED operation.
17. No automatic abort on timeout, process exit, outage or reservation age.
18. Stale fencing epoch cannot mutate.
19. Audit event and incident evidence remain independently verifiable.
20. Independent security/legal/operations review approves reason policy and role separation.

## 12. Freeze gates

1. Canonical schema, deterministic encoding, signature algorithm/provider and vectors.
2. Recovery Authority identity, key custody, HSM/non-exportability and disaster recovery.
3. Independent consumption service linearizability, idempotency, anti-rollback and retention.
4. Trusted time and trust-bundle distribution/revocation/anti-rollback.
5. Requester/approver identity, role policy and separation of duties.
6. Exact evidence bundle and status proof requirements per reason code.
7. FINALIZE/ABORT serialization and race resolution in Witness.
8. Audit event/checkpoint semantics and retention after ABORT.
9. Evidence Service reconciliation after Witness ABORT.
10. Legal hold, retention and namespace retirement policy.
11. Independent red-team and operational review.

## 13. Explicit non-authorization

Design only. No key, certificate, endpoint, consumption service, database, namespace, Witness request, cloud resource, credential, HSM, persistent file, adapter, mutex, ACL, service, C source/header, test harness or Production integration is created or authorized. No ABORT is issued. Existing Witness v1 and all runtime code remain unchanged. Production: NO-GO.

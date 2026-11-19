# Witness v2 FINALIZE Fact-Attestation v1 Contract (Draft)

Date: 2026-11-10
Status: design-only; draft; not frozen; no implementation authorization
Production: NO-GO

## 1. Purpose and authority boundary

Define the evidence a Witness v2 service must require before transitioning a reserved operation to EVIDENCE_COMMITTED. The Witness is authoritative only for its own namespace sequence and operation state. It cannot directly observe whether object bytes, metadata, retention, or audit facts are true.

A FINALIZE caller MUST NOT be able to turn arbitrary caller-supplied hashes or Boolean flags into an accepted commit. The future design therefore requires independently authenticated fact attestations from distinct authorities, bound to the same immutable transaction descriptor and reservation.

This contract does not define a production wire encoding, cryptographic algorithm, endpoint, trust store, keys, or implementation. It is a draft for review only.

## 2. Transaction binding

Every attestation and FINALIZE request binds the exact tuple:
- protocol/profile and schema version;
- Witness namespace_id, namespace_generation and reserved_sequence;
- operation_id and descriptor_digest;
- package_digest, manifest_digest and object_manifest_digest;
- evidence_service transaction/capture ID;
- metadata transaction ID and committed revision;
- retention policy ID and generation;
- service_generation and fencing_epoch;
- attestation issuer identity, issuer key ID and trust-bundle generation;
- issued-at/expiry evidence and unique attestation_id.

The canonical descriptor digest must be computed from a frozen deterministic encoding and domain-separated preimage. Every issuer independently recomputes or verifies it. A mismatch is a hard conflict. Attestations for different namespaces, reservations, operation IDs, package digests, retention generations or service generations cannot be combined.

## 3. Required independent attestation classes

### A. Object integrity and durability attestation

Issuer: Evidence Object Verifier identity, separate from uploader workload.

Must attest:
- exact immutable object IDs, lengths and SHA-256 hashes;
- manifest and package digest;
- independent read-after-write verification result and method;
- documented durability/replication profile and relevant generation;
- object immutability/version identifiers;
- verification time and status freshness;
- no missing, substituted, truncated or conflicting object;
- exact transaction descriptor and reserved sequence.

An upload acknowledgement or client-provided digest is insufficient. If provider durability cannot be independently established, the attestation must state that limitation and FINALIZE policy must reject it.

### B. Metadata commit attestation

Issuer: Metadata Commit Verifier identity, distinct from the writer where feasible.

Must attest:
- exact metadata transaction ID and committed revision/version;
- immutable operation/descriptor/request digest;
- exact object manifest/package digest references;
- exact namespace, operation ID and reserved sequence;
- authoritative read-after-write result;
- no conflicting transaction or same-ID/different-digest record;
- metadata authority generation and freshness.

A database row read from a stale replica or restored snapshot is not sufficient. The attestation must identify the consistency guarantee and authoritative read path.

### C. Retention enforcement attestation

Issuer: Retention Control Plane or independently privileged retention verifier, not the uploader or ordinary metadata writer.

Must attest:
- exact object IDs and retention scope;
- immutable retention policy ID and generation;
- retention mode, retention-until semantics and legal-hold state;
- independently verified active lock state for every required object;
- that shortening/deletion is prevented for the required period under the stated threat model;
- authority generation, verification time and exact transaction binding.

A policy name, configured setting or requested lock is not proof of an active lock. If lock state is UNKNOWN or weaker than required, FINALIZE blocks.

### D. Independent audit checkpoint attestation

Issuer: Audit checkpoint authority outside the mutable Evidence Service metadata/backup domain.

Must attest:
- exact audit event IDs and ordered event digest;
- checkpoint sequence and checkpoint digest;
- inclusion of required events: reservation, object verification, metadata prepared/committed, Witness reservation, retention confirmation and readback verification;
- independent anchor identity and trust generation;
- checkpoint durability/freshness and no known gap/fork;
- exact namespace, operation ID, descriptor digest and reserved sequence.

An append acknowledgement without an independent checkpoint is insufficient. The exact required pre-FINALIZE event set must be frozen.

### E. Coordinator transaction-completeness attestation

Issuer: Evidence Commit Coordinator identity, distinct from the uploader and ideally from each subsystem administrator.

Must attest:
- all required attestation IDs and their exact digests;
- cross-attestation equality for every transaction binding field;
- no UNKNOWN, stale, revoked, contradictory or missing result;
- policy version and decision profile;
- current fencing epoch and service generation;
- exact finalization request digest.

The coordinator attestation is an aggregation statement, not a replacement for underlying signed subsystem attestations. Witness or an independent verifier must validate each underlying issuer and signature.

## 4. Trust graph and separation of duties

Minimum logical roles:
1. Uploader: may submit bytes, cannot attest their own durability or retention.
2. Object verifier: reads immutable objects independently.
3. Metadata writer: commits transaction metadata.
4. Metadata verifier: independently confirms committed revision.
5. Retention authority/verifier: controls and verifies object lock.
6. Audit authority: anchors required events outside the Evidence Service rollback domain.
7. Commit coordinator: aggregates verified attestations and requests FINALIZE.
8. Witness: validates authority, binding and sequence transition.
9. Receipt signer: signs only after Witness EVIDENCE_COMMITTED plus all receipt policy gates.
10. Recovery/security authority: separately authorizes ABORT or exceptional recovery.

A single administrator or compromised host must not be able to mint all required attestations. Exact independence requirements, cloud account boundaries, HSM/key custody and emergency access are freeze gates.

## 5. Freshness, replay and revocation

- Each attestation has a unique nonzero ID, issuer key ID, trust-bundle generation, bounded validity and signed payload digest.
- Attestations are single-transaction and single-reservation scoped.
- Witness stores/recognizes attestation IDs or an equivalent replay-resistant transaction digest.
- Exact FINALIZE retry with identical complete digest is idempotent.
- Same operation ID with different finalization digest is conflict.
- Revoked issuer keys, stale trust bundles, untrusted time, expiry, duplicate attestation IDs, or rollback-suspect authority state block.
- Nonces bind sessions but do not replace signature authentication.
- A signed statement proves issuer endorsement, not that the issuer's underlying observation was correct; independent audit and acceptance testing remain necessary.

## 6. FINALIZE validation algorithm (conceptual)

1. Authenticate coordinator and authorize the exact namespace/profile/purpose.
2. Verify namespace ACTIVE, current generation and fencing epoch.
3. Load exact RESERVED operation by operation_id; compare descriptor digest and sequence.
4. Strictly parse every attestation under the approved schema; reject unknown fields/versions, duplicate IDs, malformed lengths and noncanonical encoding.
5. Verify issuer signatures, key status, trust-bundle monotonicity, expiry and trusted time.
6. Verify issuer role is authorized for that attestation class and is not an impermissible shared identity.
7. Verify every attestation binds exactly the same namespace, generation, operation, descriptor, package, object manifest and reserved sequence.
8. Verify object, metadata, retention and audit assertions meet policy thresholds.
9. Verify coordinator aggregation digest exactly covers the underlying attestation digests and policy version.
10. Atomically transition RESERVED -> EVIDENCE_COMMITTED with the complete attestation-set digest.
11. Persist immutable result before returning signed FINALIZE response.

Any failure, UNKNOWN, timeout, stale state or contradictory assertion returns a blocking result and does not change operation state. If the durable transition outcome is unknown, resolve using STATUS for the same operation and digest.

## 7. What Witness must not infer

- It must not infer object integrity from uploader statements.
- It must not infer retention from metadata policy fields.
- It must not infer audit completeness from append success.
- It must not infer metadata durability from a client-side database ACK.
- It must not infer freshness from a nonce alone.
- It must not accept a generic coordinator Boolean such as verified=true.
- It must not sign Evidence Receipts or authorize Broker/generation operations.
- It must not automatically ABORT when an attestation expires or an authority is unavailable.

## 8. Failure taxonomy

BLOCK_NAMESPACE_OR_PROFILE; BLOCK_RESERVATION_MISMATCH; BLOCK_DESCRIPTOR_MISMATCH; BLOCK_OBJECT_ATTESTATION_MISSING; BLOCK_OBJECT_INTEGRITY_OR_DURABILITY; BLOCK_METADATA_UNCOMMITTED; BLOCK_METADATA_STALE_OR_CONFLICTED; BLOCK_RETENTION_NOT_ACTIVE; BLOCK_RETENTION_POLICY_MISMATCH; BLOCK_AUDIT_UNCHECKPOINTED; BLOCK_AUDIT_GAP_OR_FORK; BLOCK_ISSUER_UNTRUSTED; BLOCK_SIGNATURE_INVALID; BLOCK_KEY_REVOKED; BLOCK_TRUST_ROLLBACK; BLOCK_ATTESTATION_EXPIRED; BLOCK_ATTESTATION_REPLAY; BLOCK_CROSS_ATTESTATION_MISMATCH; BLOCK_COORDINATOR_NOT_AUTHORIZED; BLOCK_FENCING_STALE; BLOCK_TIME_UNTRUSTED; BLOCK_AUTHORITY_UNAVAILABLE; BLOCK_FINALIZE_OUTCOME_UNKNOWN.

No block maps to success or implicit abort.

## 9. Acceptance matrix

1. Valid complete attestation set accepted only for exact RESERVED transaction.
2. Missing each individual class blocks.
3. Wrong issuer role, shared forbidden identity or revoked key blocks.
4. Mutating each bound field/signature/digest blocks.
5. Same attestation replayed across operation, namespace or sequence blocks.
6. Expired, future-dated, stale-trust or untrusted-time attestation blocks.
7. Object upload ACK without independent readback blocks.
8. Stale metadata replica or old backup blocks.
9. Retention requested but not active blocks.
10. Audit append without independent checkpoint blocks.
11. Same operation ID/different finalization digest conflicts.
12. Exact retry after lost response returns same terminal result.
13. Concurrent FINALIZE and ABORT have exactly one serialized winner.
14. Stale fencing epoch blocks every mutation.
15. Witness crash at each durable transition has one queryable outcome.
16. Attestation authority outage or unknown status blocks.
17. No attestation set alone issues a receipt or starts Broker.
18. Independent red-team review validates trust separation and false-attestation resistance.

## 10. Freeze gates

1. Canonical deterministic encoding, field numbers, size limits and digest preimages.
2. Signature algorithm, signature encoding, CNG/provider compatibility and test vectors.
3. Issuer identity provisioning, key custody, HSM/non-exportability and rotation/revocation.
4. Trust-bundle anti-rollback and trusted-time service.
5. Exact independent authority boundaries and separation of duties.
6. Provider-specific object durability and retention proof.
7. Metadata consistency/read-after-write proof.
8. Audit event/checkpoint semantics and independent anchor.
9. Coordinator authorization and aggregation profile.
10. Witness validation and idempotency retention.
11. Incident/recovery behavior for unavailable or compromised attestation authorities.

## 11. Explicit non-authorization

Design only. No endpoint, cloud resource, database, object store, key, certificate, credential, HSM, service, network rule, persistent file, adapter, C source/header, tests, ACL, mutex, namespace or Production integration is created or authorized. Witness v1 remains unchanged; v2 remains draft. Production: NO-GO.

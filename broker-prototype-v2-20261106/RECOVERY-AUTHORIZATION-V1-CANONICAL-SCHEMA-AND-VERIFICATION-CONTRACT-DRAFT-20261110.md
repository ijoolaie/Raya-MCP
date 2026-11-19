# Recovery Authorization v1 — Canonical Schema and Verification Contract (Draft)

Date: 2026-11-10
Status: design draft only; not frozen; no implementation authorization
Production: NO-GO

## 1. Purpose and limits

Defines a signed, narrowly scoped, single-use authorization for one explicitly approved pending-journal recovery action. It is not an administrator token, runtime capability, or substitute for independent evidence, Witness, ledger, file-identity, or approval verification.

A valid signature proves only that the signing key endorsed exact canonical bytes. It does not prove the embedded facts are true, the evidence is durable, the Witness response is current, the ledger is not rolled back, the file/ACL is unchanged, operator approvals are genuine, or the token is unused. Each is a separate trust boundary.

## 2. Mandatory invariants

1. Bind exactly one incident, namespace, file identity, captured two-slot image, source slot, target slot, replacement bytes, action, and recovery transaction.
2. No wildcard, range, "any slot", "latest", or caller-selected replacement.
3. Reject unknown/duplicate fields, noncanonical encodings, unsupported versions, invalid enums, and trailing bytes.
4. One fixed signature profile; no negotiation or fallback.
5. Signing trust and replay/consumption authority are outside the host backup/VM rollback domain.
6. Signature verification is not consumption. No action until independent single-use reservation succeeds.
7. Consumed, revoked, expired, ambiguous, or status-unknown authorization never becomes usable again.
8. Trusted time is required for expiry; clock uncertainty blocks.
9. This authorization never grants service startup, ordinary issuance, epoch advancement, or generation creation.

## 3. Envelope and canonical encoding

### 3.1 Candidate standard

Candidate: deterministic CBOR (RFC 8949) payload in COSE_Sign1 (RFC 9052). This is a proposal, not an implementation approval.

Require definite lengths, shortest integer/length encodings, deterministic map-key ordering, no duplicate keys, no tags/floats/indefinite values, byte strings for fixed identifiers/hashes, and bounded UTF-8 text only for display labels. Decode then deterministically re-encode; bytes must match exactly. Reject all trailing data.

Protected COSE headers carry profile ID, key ID, content type, and algorithm. Unprotected headers cannot carry or override security decisions. Unknown critical headers reject.

### 3.2 Signature profile is a freeze gate

Algorithm, key type, signature encoding, minimum strength, provider, and Windows CNG compatibility remain unresolved. Do not assume support or silently substitute algorithms.

Before selection, document supported Windows versions/providers, CNG key storage modes, signature encoding, FIPS/organizational constraints, HSM/non-exportable availability, key recovery, signing/verification/rotation/revocation, and fixed positive/negative test vectors. Select one versioned profile. Any algorithm change requires a new profile version. Until approved, verifier result is BLOCK_PROFILE_UNAPPROVED.

Signature covers the COSE Sig_structure over exact protected-header and payload bytes. No post-sign normalization or detached payload.

## 4. Payload schema v1

A fixed-schema CBOR map; symbolic names below must receive stable integer keys in a frozen appendix. Unknown keys reject.

### 4.1 Protocol and action
- schema_version = 1; profile_id; policy_version
- authorization_id: 16-byte nonzero unique ID
- authorization_nonce: 32-byte CSPRNG value
- incident_id and namespace_id: 16-byte nonzero IDs
- action: RECONSTRUCT_WITNESS_COMMITTED, RECONSTRUCT_LOCAL_COMMITTED, RECONSTRUCT_TOMBSTONED, or RESTORE_TOMBSTONE_REDUNDANCY
- issued_at and expires_at: unsigned UTC Unix seconds; expires_at > issued_at; bounded lifetime

### 4.2 File identity
- stable volume identity and volume-scoped file ID
- file size exactly 512 bytes for this adapter
- owner SID in binary form
- DACL fingerprint: SHA-256 of a separately specified canonical security descriptor
- NOT_REPARSE assertion from same verified handle
- expected service SID/effective identity profile

Path is diagnostic only, never authorization identity. If file ID is not stable for the selected filesystem, implementation is blocked pending a stronger identity design.

### 4.3 Evidence
- evidence_store_id and opaque evidence_receipt_id (exact length fixed at freeze)
- SHA-256 of slot 0, slot 1, and complete 512-byte image
- digest of independently verified capture receipt
- capture time and evidence policy version

Receipt must bind incident, namespace, file identity, both raw slot images, lengths, and evidence object IDs. Receipt trust is independently verified; a digest alone is not proof.

### 4.4 Source and replacement
- source slot index (0/1), nonzero revision, and exact 32-byte raw slot hash
- malformed target slot index (0/1, distinct) and exact 32-byte raw hash
- replacement slot index must equal malformed slot index
- replacement revision exactly source revision + 1; overflow forbidden
- hash of complete canonical 128-byte RYPI record
- exact permitted replacement state
- hash of complete encoded 256-byte replacement slot

The coordinator independently constructs replacement bytes from approved facts and compares complete bytes/hashes before writing. The token does not carry executable write instructions.

### 4.5 Witness and ledger
- witness_id, witness_key_id, trust-bundle version
- exact committed sequence
- operation_id (16 bytes) and request digest (32 bytes), or an explicit typed NO_PENDING_PROOF variant
- digest of independently verified authenticated Witness proof
- ledger namespace, exact epoch, file-identity digest, and provenance-receipt digest

Bare booleans such as witness_verified or ledger_valid are not evidence.

### 4.6 Approval
- managed requester and approver principal IDs, distinct
- bounded incident/change reference
- approval policy ID/version and independently verifiable approval-evidence digest
- recovery authority ID

Signing service enforces role separation; different strings alone do not prove separation of duties.

## 5. Verification sequence

1. Bound envelope size before parsing.
2. Strictly parse COSE; reject duplicate/unknown critical headers, unprotected overrides, trailing bytes.
3. Require exact approved schema/profile/version.
4. Verify deterministic-CBOR canonical form by byte-identical re-encoding.
5. Resolve key only from independently provisioned trust bundle; reject unknown, expired, revoked, or rollback-suspect bundle.
6. Verify signature with the single approved algorithm/provider.
7. Validate lengths, enums, ranges, reserved values, cross-field equality, time order, and max lifetime.
8. Verify trusted time and expiry; no wall-clock-only fallback.
9. Independently verify evidence receipt, Witness proof, ledger provenance, file identity/DACL, service identity, and approval evidence.
10. Under approved exclusion/fencing and same verified handle, re-read both slots and compare exact hashes immediately before transaction.
11. Reserve the exact authorization with the independent consumption service.
12. Only the separately reviewed recovery coordinator may then consider the exact authorized action. Verifier itself never writes.

Any failed, unknown, unavailable, contradictory, or timed-out check blocks. No offline or best-effort bypass.

## 6. Single-use consumption service

A copied signed token is replayable unless an independent authority tracks use. Require an external durable, linearizable ledger keyed by authorization_id and bound to the complete authorization digest. Conceptual operations:

- RESERVE(id, digest, incident, namespace, file identity, action, recovery_transaction_id)
- COMMIT_CONSUMED(id, digest, recovery_transaction_id, outcome_receipt_digest)
- STATUS(id, digest)

The service is outside host rollback domain; host DPAPI, registry, files, and local clock cannot be its replay authority.

States: UNUSED, RESERVED, CONSUMED, REVOKED, EXPIRED, CONFLICT, UNKNOWN.

Rules:
- Same ID/digest/transaction retry is idempotent.
- Same ID with different digest is CONFLICT.
- Reservation is never silently released on timeout/disconnect.
- RESERVED requires authenticated STATUS and explicit resolution; never auto-return to UNUSED.
- CONSUMED/REVOKED/EXPIRED/CONFLICT/UNKNOWN never authorize use.
- Retain replay records for namespace lifetime plus approved audit retention; pruning needs a separate tombstone/compaction proof.
- Service outage or unknown outcome blocks recovery.

Lost RESERVE/COMMIT response is resolved with STATUS using the same ID/digest/transaction. Never mint a replacement authorization because a response was lost. If journal write may have occurred while commit status is uncertain, preserve evidence and enter RECOVERY_REQUIRED.

## 7. Key and trust-bundle lifecycle

Private signing key exists only in the separate recovery authority, never on Broker host, command line, environment, repository, journal, or evidence bundle.

Define independent provisioning/distribution, monotonic trust-bundle version and anti-rollback anchor, activation/overlap, rotation, revocation, compromise response, emergency disable, unavailable-revocation behavior, disaster recovery, and signing/verification audit. A public-key fingerprint alone does not establish current authorization.

## 8. Fail-closed result taxonomy

BLOCK_MALFORMED_ENVELOPE; BLOCK_NONCANONICAL_ENCODING; BLOCK_UNKNOWN_FIELD_OR_VERSION; BLOCK_PROFILE_UNAPPROVED; BLOCK_UNKNOWN_OR_REVOKED_KEY; BLOCK_SIGNATURE_INVALID; BLOCK_TRUST_BUNDLE_ROLLBACK; BLOCK_EXPIRED_OR_TIME_UNTRUSTED; BLOCK_EVIDENCE_UNVERIFIED; BLOCK_WITNESS_UNVERIFIED; BLOCK_LEDGER_UNVERIFIED; BLOCK_FILE_IDENTITY_OR_ACL_MISMATCH; BLOCK_SLOT_HASH_CHANGED; BLOCK_REPLACEMENT_MISMATCH; BLOCK_APPROVAL_POLICY_FAILED; BLOCK_AUTHORIZATION_REPLAYED; BLOCK_AUTHORIZATION_CONFLICT; BLOCK_CONSUMPTION_SERVICE_UNAVAILABLE; BLOCK_CONSUMPTION_OUTCOME_UNKNOWN; BLOCK_RECOVERY_TRANSACTION_CONFLICT.

No error maps to success. Diagnostics must not expose private keys, credentials, full security descriptors, or sensitive operator data.

## 9. Acceptance tests before freeze

1. Fixed positive vectors for full envelope and every field.
2. Reject non-shortest encodings, indefinite lengths, duplicate/unknown keys, tags, floats, trailing bytes, noncanonical map order, oversize, and length mismatches.
3. Decode/re-encode equality for every accepted vector.
4. Valid signature accepted; bit mutations to payload/header/signature rejected.
5. Wrong, expired, revoked key; stale/rolled-back trust bundle; unsupported algorithm/fallback all rejected.
6. Provider/CNG failure and malformed signature encoding fail closed.
7. Wrong namespace, incident, volume/file ID, owner, DACL, SID, either slot hash, image hash, source/target index, revision, or replacement bytes rejected.
8. Witness identity/operation/digest/sequence/receipt mismatch rejected.
9. Ledger namespace/epoch/file/provenance mismatch rejected.
10. Missing approval, same requester/approver, revoked principal, or wrong policy rejected.
11. Expiry boundaries, future-issued token, clock rollback, and time-service outage block.
12. Concurrent multi-host RESERVE yields one reservation; same ID/digest/transaction is idempotent; same ID/different digest conflicts.
13. Lost RESERVE/COMMIT responses recover by STATUS without reuse.
14. Abandoned RESERVED, authority outage, VM revert, clone, host restore, and replay from another host never reset consumption.
15. Consumed/revoked token remains unusable after service restart/restore.
16. Slot mutation between verification and write is detected under held handle/lock.
17. Crash at every boundary from evidence check through journal readback/consumption commit cannot cause double use.
18. Every block path has no journal write, service start, generation, or epoch advance.
19. Independent review of schema, crypto profile, key custody, replay service, and separation of duties.

## 10. Freeze gates

Do not implement until approved and versioned:
1. Deterministic CBOR/COSE profile and stable integer key assignment.
2. Signature algorithm/encoding/provider and Windows CNG support matrix.
3. Recovery authority hosting, key custody, HSM/non-exportability, disaster recovery.
4. Trust-bundle distribution, monotonicity, revocation, rollback protection.
5. Trusted time source, skew/lifetime, outage policy.
6. Consumption-service protocol, linearizability, idempotency, retention, independent rollback domain.
7. Evidence receipt schema/store guarantees.
8. Witness proof and ledger provenance contracts.
9. File identity/DACL canonicalization and same-handle verification.
10. Operator identity, separation-of-duties and approval evidence.
11. Independent review and test vectors.

## 11. Explicit non-authorization

No keys, certificates, credentials, endpoints, cloud resources, evidence store, replay ledger, persistent file, mutex, ACL, Windows service, C source/header, tests, or Production integration are created or authorized. No journal write or repair is authorized. The fail-closed slot selector remains unchanged; the recovery classifier remains design-only.

Production status: NO-GO.


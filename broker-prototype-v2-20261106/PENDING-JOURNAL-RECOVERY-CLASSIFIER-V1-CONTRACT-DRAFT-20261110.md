# Pending Journal Recovery Classifier v1 — Contract Draft

Date: 2026-11-10
Status: interface/design draft only; not frozen and not approved for implementation
Scope: pure decision logic for the narrow observation VALID + MALFORMED_NONZERO
Production status: NO-GO

## 1. Purpose

This draft defines a future pure-C classifier that consumes a fixed snapshot of facts already acquired and independently verified by trusted adapters. It returns a conservative recovery disposition. It does not read files, call the Witness, authenticate signatures, validate Windows tokens/ACLs, acquire locks, preserve evidence, authorize writes, or publish generations.

The classifier is not a recovery engine. It cannot turn an untrusted caller assertion into trusted evidence. Until the authorization, evidence receipt, Witness proof, and ledger provenance contracts are frozen, this interface remains a draft and must not be implemented as a production gate.

## 2. Non-goals and mandatory invariants

- Never change the existing two-slot selector or its fail-closed behavior.
- Never select the lower valid revision as an ordinary fallback.
- Never parse revision/order hints from MALFORMED_NONZERO bytes for decisions.
- Never mutate caller input, slot bytes, journal state, ledger state, or authorization state.
- Never return READY, ISSUE, GENERATE, RETRY_WITH_NEW_ID, or WRITE.
- Never perform I/O, allocate memory, use global state, read clocks, or call platform APIs.
- Never infer Witness authentication, file identity, DACL correctness, evidence durability, or operator approval from a bare Boolean.
- Unknown, missing, contradictory, or out-of-range facts always produce BLOCK.

The only positive disposition is CANDIDATE_RECONSTRUCTION_REQUIRES_EXTERNAL_AUTHORIZATION. It is not permission to write and must not be consumed directly by runtime issuance code.

## 3. Trusted-input boundary

The caller must construct the input only after these separate subsystems have completed their own checks:
1. Slot reader: exact two 256-byte snapshots, stable same-handle identity, complete read status.
2. Slot codec: normal decoder validation of the one VALID slot; raw classification of the peer as MALFORMED_NONZERO.
3. Evidence subsystem: independently verified durable receipt covering both exact raw slot hashes and incident/namespace/file identity.
4. Witness client: authenticated response and exact operation/status binding, including namespace, operation ID, request digest, committed sequence, and witness identity.
5. Ledger adapter: independently verified namespace, file identity, record validity, and epoch provenance.
6. Authorization verifier: signature, canonical encoding, expiry, replay state, exact file/slot/evidence/Witness/ledger/action binding.
7. Recovery coordinator: namespace issuance fence, cross-process exclusion, and same-handle file identity/ACL checks.

These are trust boundaries, not fields the classifier can validate. A future implementation must document which concrete verified type or opaque capability each subsystem supplies. A caller-controlled integer verified=1 is not an acceptable substitute.

## 4. Proposed input model

All integer fields use fixed-width unsigned types. Enums must reject unknown numeric values. Byte arrays have exact fixed lengths. The classifier receives no pointers to mutable global state.

Slot observations:
- Each slot kind: ZERO, VALID, MALFORMED_NONZERO, IO_UNKNOWN.
- For VALID only: decoded revision, pending state, namespace ID, operation ID, request digest, expected sequence, next sequence, Witness sequence, and SHA-256 of the exact raw 256 bytes.
- For MALFORMED_NONZERO: exact raw SHA-256 and evidence reference only. No decoded revision or embedded fields are decision inputs.
- Both raw slot hashes and the 512-byte image hash must match the verified evidence receipt.

Authority facts:
- Witness status: COMMITTED_EXACT, NOT_FOUND_EXACT_QUERY, UNKNOWN, UNAVAILABLE, CONFLICT, INVALID_AUTHENTICATION, NAMESPACE_RETIRED.
- Witness namespace, operation ID, request digest, committed sequence, witness identity/key fingerprint, and response freshness/binding result.
- Ledger status: VALID, UNAVAILABLE, CORRUPT, IDENTITY_MISMATCH; namespace, epoch, and verified provenance.
- Evidence status: VERIFIED_RECEIPT, MISSING, INVALID, MISMATCH.
- File status: VERIFIED_SAME_HANDLE, IDENTITY_MISMATCH, ACL_MISMATCH, REPARSE, IO_UNKNOWN.
- Exclusion status: HELD_AND_VERIFIED, NOT_HELD, ABANDONED, UNKNOWN.
- Authorization status: VERIFIED_EXACT_SINGLE_USE, MISSING, INVALID, EXPIRED, REPLAYED, MISMATCH.

A future header must define these as explicit enums and fixed structs, with reserved fields required to be zero. It must not expose an API that accepts an arbitrary caller-supplied replacement record without matching it to the authorization's exact digest.

## 5. Proposed output model

Disposition enum:
- BLOCK_RECOVERY_REQUIRED
- BLOCK_SECURITY_FAILURE
- BLOCK_ROLLBACK_SUSPECTED
- BLOCK_AUTHORITY_UNAVAILABLE
- CANDIDATE_RECONSTRUCT_WITNESS_COMMITTED
- CANDIDATE_RECONSTRUCT_LOCAL_COMMITTED
- CANDIDATE_RECONSTRUCT_TOMBSTONED

Output fields, only for candidate dispositions:
- selected valid slot index;
- target malformed slot index;
- source revision and proposed revision = source revision + 1;
- exact source slot hash;
- exact malformed slot hash;
- exact authorized replacement record hash/state;
- incident ID and authorization ID.

No output field is a write instruction or a capability. The coordinator must independently re-check every fact immediately before a separately authorized transaction. The classifier output must never authorize normal service startup or generation issuance.

## 6. Conservative decision order

Apply checks in this order; first failure wins:
1. Null/invalid input, unknown enum, nonzero reserved field, inconsistent slot pair, invalid index, or revision overflow -> BLOCK_RECOVERY_REQUIRED.
2. Any IO_UNKNOWN, malformed evidence receipt, raw hash mismatch, or missing evidence -> BLOCK_RECOVERY_REQUIRED.
3. File identity/ACL/reparse/handle mismatch or invalid Witness authentication/binding -> BLOCK_SECURITY_FAILURE.
4. Exclusion not held, abandoned without completed reconciliation, or unknown -> BLOCK_RECOVERY_REQUIRED.
5. Witness unavailable/unknown/conflict/retired -> BLOCK_AUTHORITY_UNAVAILABLE or BLOCK_SECURITY_FAILURE as appropriate; never candidate.
6. Namespace mismatch among slot, Witness, ledger, evidence, and authorization -> BLOCK_SECURITY_FAILURE.
7. Ledger unavailable/corrupt/identity mismatch -> BLOCK_RECOVERY_REQUIRED.
8. Ledger ahead of Witness -> BLOCK_SECURITY_FAILURE. Ledger behind Witness -> BLOCK_ROLLBACK_SUSPECTED.
9. Witness operation/digest/sequence does not exactly match the active journal operation -> BLOCK_SECURITY_FAILURE.
10. Authorization missing/invalid/expired/replayed/mismatched -> BLOCK_RECOVERY_REQUIRED or BLOCK_SECURITY_FAILURE; never candidate.
11. Re-evaluate only the four explicitly permitted state cases in the Authorized Recovery Procedure. All other states -> BLOCK_RECOVERY_REQUIRED.
12. Only exact state, sequence, hash, file, evidence, Witness, ledger, and authorization matches yield a CANDIDATE disposition.

The implementation must not collapse distinct block reasons into a permissive default. No unknown value may be interpreted as success.

## 7. State-specific candidate rules

- PREPARED + exact Witness COMMITTED + ledger exactly at expected_sequence may produce CANDIDATE_RECONSTRUCT_WITNESS_COMMITTED. It must not itself advance the ledger.
- WITNESS_COMMITTED + exact Witness COMMITTED + ledger at next_sequence may produce CANDIDATE_RECONSTRUCT_LOCAL_COMMITTED only if the caller supplies separately verified ledger-commit provenance for this exact operation.
- LOCAL_COMMITTED + exact Witness COMMITTED + ledger at next_sequence may produce CANDIDATE_RECONSTRUCT_TOMBSTONED only if terminal publication/audit evidence is verified and bound to the same operation.
- TOMBSTONED + exact terminal Witness/ledger sequence may produce CANDIDATE_RECONSTRUCT_TOMBSTONED solely for redundancy restoration.
- EMPTY, ABORTED, DEGRADED, RECOVERY_REQUIRED, NOT_FOUND, UNKNOWN, or any ambiguous terminal/publication state never yields a candidate under this contract.

The authorization must bind the exact canonical replacement record. The classifier may compare its digest but does not construct or sign that record.

## 8. API shape proposal (not frozen)

A future C interface may resemble:

    int raya_recovery_classify_v1(
        const raya_recovery_facts_v1 *facts,
        raya_recovery_decision_v1 *decision);

Contract:
- Return 1 only when the input was structurally processed; decision still may be BLOCK.
- Return 0 for invalid pointers/structural contract violation; decision is initialized to BLOCK_RECOVERY_REQUIRED before any validation.
- No heap allocation, I/O, OS calls, global state, or mutable static storage.
- Same input bytes always produce the same output bytes.
- Input is const; output is fully zero-initialized before fields are assigned.
- No partial candidate output on any block path.
- No function in this module may call the slot selector with fallback semantics or write/repair APIs.

Names and exact structure layout are placeholders pending contract freeze.

## 9. Test requirements

Pure deterministic tests:
- all 16 ordered pairs of slot observation kinds;
- slot order reversed for every candidate state;
- malformed peer revision hints lower/equal/higher/zero/max/random, proving no output changes;
- all pending states and Witness statuses;
- exact and mismatched namespace, operation ID, digest, expected/next/Witness sequence;
- ledger equal/behind/ahead/corrupt/unavailable/identity mismatch;
- evidence missing/invalid/hash mismatch/receipt mismatch;
- file identity, ACL, reparse, and lock states;
- authorization missing/invalid/expired/replayed/wrong-file/wrong-slot/wrong-state/wrong-hash;
- revision UINT64_MAX and UINT64_MAX-1;
- reserved bytes, unknown enum values, null pointers, aliasing, and output initialization;
- metamorphic test: mutate only malformed-slot contents while preserving MALFORMED_NONZERO classification and update corresponding evidence hash/authorization consistently; the decision must remain independent of apparent revision bytes and may change only because exact authorization/evidence binding changed;
- fuzzing of all fixed-size input structures under MSVC /W4 /WX and memory-safety tooling where available.

Every block path must prove: no input mutation, no candidate output leakage, no READY/ISSUE result, no generation creation, and no filesystem side effect.

## 10. Freeze gates before implementation

Do not implement until all are approved and versioned:
1. Signed recovery-authorization canonical schema, signature algorithm/profile, key rotation, expiry, replay ledger, and idempotent status query.
2. Independent evidence-store receipt format, trust roots, append-only guarantees, retention, and restore-domain separation.
3. Witness proof contract for each journal state, including exact operation status and namespace sequence semantics.
4. Local ledger namespace/provenance and dedicated reconcile API; numeric epoch equality alone is insufficient.
5. Durable journal recovery provenance: how intended replacement state is derived and audited.
6. Cross-process lock/fencing and clone/VM rollback handling.
7. Operator separation-of-duties, incident approval, and audit policy.

Until then this is a reviewable interface proposal only. Do not create a C header or test harness from this draft.

## 11. Status

Design draft only. No C source/header, tests, Windows adapter, persistent file, mutex, ACL, service identity, Witness endpoint, evidence store, or Production integration was created.

Production status: NO-GO.


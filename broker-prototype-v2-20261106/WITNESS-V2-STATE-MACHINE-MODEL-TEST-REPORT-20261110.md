# Witness v2 State Machine Model — Test Report

Date: 2026-11-10
Status: isolated synthetic executable model; prototype evidence only
Production: NO-GO

## 1. Artifacts

- witness_v2_state_machine_model.py
- test_witness_v2_state_machine_model.py

The model is a single-process Python reference model for one synthetic namespace. It does not connect to Witness, Evidence Service, Broker, Windows services, files, networks, databases or cryptographic providers.

## 2. Executed test

Command:

`python -m unittest -v test_witness_v2_state_machine_model`

Result:
- Exit code: 0
- Tests run: 20
- Passed: 20
- Failed: 0
- Errors: 0

## 3. Covered behaviors

1. RESERVE consumes one sequence and exact duplicate is idempotent.
2. Same operation ID with different request digest conflicts.
3. Single unresolved reservation blocks a second operation.
4. FINALIZE requires the synthetic attestation gate.
5. ABORT requires the synthetic authorization gate.
6. FINALIZE and ABORT are mutually exclusive terminal transitions.
7. Both terminal outcomes are idempotent for exact repeated requests.
8. Terminal digest mismatch conflicts.
9. Sequence is not reused after ABORT.
10. Stale fencing epoch is rejected at the modeled mutation boundary.
11. Non-authoritative STATUS never returns NOT_FOUND.
12. Lost RESERVE, FINALIZE and ABORT responses resolve through exact STATUS.
13. Snapshot/restore of the model preserves its in-memory committed state.
14. RECOVERY_LOCKED blocks modeled mutations.
15. Both serialized FINALIZE/ABORT orderings produce exactly one terminal winner.

## 4. Important model assumptions

- Method execution is serialized by the Python interpreter/model; this is not a distributed consensus implementation.
- `attestations_verified` and `authorization_verified` are synthetic test inputs. They are not cryptographic verification and do not establish issuer independence.
- ABORT authorization consumption is modeled as atomic with the Witness terminal transition solely to test the Option A shape. This does not select or approve Option A.
- Snapshot/restore copies Python memory state. It does not test durable storage, power loss, backup rollback or anti-rollback.
- A lost response is simulated after the state mutation has completed. A true uncertain disk/quorum commit requires a storage/consensus fault model.
- Fencing is a scalar epoch check in one process; it does not prove enforcement by every distributed mutation target.
- FINALIZE/ABORT race tests cover both serialized orders, not real thread scheduling, network partitions or multi-replica races.

## 5. Not tested / not proven

- Linearizability or consensus across replicas.
- Durable quorum commit, leader election, split brain or stale leader fencing.
- Actual crash/power-loss boundaries and persistent log recovery.
- Witness anti-rollback anchor, VM snapshot rollback or clone detection.
- Cryptographic canonical encoding, signatures, key revocation or trusted time.
- External metadata, object-store durability, retention enforcement and audit checkpoints.
- Authorization service atomicity and revocation races.
- Receipt signing, persistence, delivery, suspension or revocation.
- Windows CNG, NTFS, service identity, ACL, named pipe or Broker runtime behavior.
- Production safety or readiness.

## 6. Findings and next gates

The synthetic state machine is internally consistent for the tested single-process transition rules. It demonstrates the intended mutual exclusion of terminal outcomes and exact STATUS recovery after simulated lost responses.

It does not resolve the outstanding protocol decisions D1-D14 in the state-machine draft. In particular, authoritative quorum semantics, anti-rollback, STATUS freshness, canonical request schemas, fact-attestation trust, ABORT authorization consumption, and cross-system crash recovery remain OPEN.

Before expanding this model, freeze:
1. authoritative commit/linearizability semantics;
2. exact canonical operation and digest schemas;
3. STATUS freshness and NOT_FOUND proof;
4. fencing issuer and mutation-boundary enforcement;
5. ABORT authorization consumption model;
6. durable intent and uncertain-commit recovery rules.

Only after those decisions should a richer fault model or formal model-checking specification be built. Do not wire this model into runtime or treat its tests as a security certification.

## 7. Explicit non-authorization

No service, endpoint, namespace, key, credential, database, object store, persistent runtime file, ACL, mutex, service identity, network rule, Windows adapter or Production integration was created or authorized.

IranPriceScout and its independent tunnel were not accessed or changed.

Production: NO-GO.

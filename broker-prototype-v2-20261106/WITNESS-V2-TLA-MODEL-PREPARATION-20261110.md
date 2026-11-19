# Witness v2 TLA+ Model — Preparation Note

Date: 2026-11-10
Status: formal specification draft; TLC execution not yet verified
Production: NO-GO

## Files
- WitnessV2StateMachine.tla — finite-state transition specification.
- WitnessV2StateMachine.cfg — TLC finite model configuration.

## Modeled state
One namespace, finite operation identifiers, monotonic bounded sequence, one unresolved reservation, terminal EVIDENCE_COMMITTED or ABORTED outcomes, namespace recovery lock/retirement, and fencing epoch.

## Actions
- Reserve: consumes exactly the next sequence and creates RESERVED.
- Finalize: atomically changes the exact unresolved operation to EVIDENCE_COMMITTED when the modeled attestation predicate is true.
- Abort: atomically changes the exact unresolved operation to ABORTED when the modeled authorization predicate is true; records authorization ID and burns the sequence.
- AdvanceFence: advances the fencing epoch.
- LockRecovery: blocks future mutations.
- Retire: retires an idle namespace.

## Invariants configured
TypeOK, AtMostOneReserved, UnresolvedConsistent, SequenceUnique, TerminalIntegrity, AbortHasAuthorization, NoSequenceReuse.

## Abstraction limits
The model treats attestation and authorization verification as Boolean assumptions. It has one atomic state machine and no replicas, quorum, disk, network, signatures, trusted time, metadata, object storage, retention, audit, receipt, or authorization-consumption service. It does not prove implementation correctness, linearizability of a distributed Witness, anti-rollback, or Windows durability.

The configuration is finite (two operation IDs and sequence bound 2) to permit state exploration. It is a bounded model, not a proof for unbounded operation histories.

## TLC availability
Java was detected on the host. A TLC command and a tla2tools.jar were not found in the standard candidate locations checked. Therefore no TLC run is claimed. Before treating this as checked, obtain an approved TLA+ tools distribution through the normal project dependency process and run TLC against this configuration. Do not download binaries or modify machine-wide tools as part of this design step.

## Known modeling caveat
The model encodes FINALIZE and ABORT as mutually exclusive atomic actions in one abstract authority. It intentionally does not model the distributed authorization-consumption gap or external fact attestation protocol. Those require separate refinement models after the relevant contracts are frozen.

## Non-authorization
No code is wired into runtime. No service, endpoint, namespace, key, credential, persistent privileged state, ACL, mutex, network rule, or Production integration is created or authorized. Existing project changes are preserved. IranPriceScout and its independent tunnel remain untouched.

Production: NO-GO.

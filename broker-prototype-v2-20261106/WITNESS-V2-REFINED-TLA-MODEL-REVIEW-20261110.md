# Witness v2 Refined TLA+ Model — Design Review

Date: 2026-11-10
Status: draft; static review only; TLC not run
Production: NO-GO

## Artifacts
- WitnessV2StateMachineRefined.tla
- WitnessV2StateMachineRefined.cfg

## Refinements over the first draft
1. Global authorization-ID consumption set; an ABORT cannot consume an authorization ID already used by another operation.
2. STATUS observer state distinguishes authoritative results from UNKNOWN.
3. NOT_FOUND is permitted only for an authoritative query whose exact operation ID is ABSENT.
4. A known STATUS result is generated from the operation state observed atomically by the modeled authority; the returned result is a historical snapshot and can become old after later mutations.
5. Terminal operation states remain immutable.
6. The bounded model retains sequence uniqueness, one unresolved reservation, exact descriptor binding, and mutation-boundary fencing.

## Static review corrections
A manual transition-coverage pass found and corrected two draft defects before reporting:
- RESERVE and ABORT now increment logIndex without simultaneously declaring it UNCHANGED.
- Terminal immutability is expressed as a temporal property (TerminalStatesNeverReopen), not as a state invariant using a primed variable.
- STATUS result variables represent the last observation. A previously authoritative NOT_FOUND may remain visible after a later RESERVE; it is a historical observation, not a claim about current state. The QueryStatus action itself only emits NOT_FOUND when the modeled authority sees ABSENT at that action's linearization point.

## Important limitations and defects intentionally not hidden
- Authorization verification is still an abstract Boolean; this model does not verify signatures, expiry, revocation, role separation, or atomic consumption in an independent service.
- The model has one abstract authority. It does not model replicas, quorum, consensus, split brain, durable log replication, leader election, or anti-rollback.
- STATUS is a single atomic observation; it does not model concurrent request/response transport, stale replicas, or a query racing with a commit.
- Request/response loss is not modeled as transport state. A retry action and canonical response cache still need a refinement.
- Fencing is a monotonically increasing scalar; authority to advance it and proof that every replica enforces it are not modeled.
- Crash, restart, backup restore, VM clone, and storage durability are not modeled.
- TerminalStatesNeverReopen is a temporal property of this abstract transition system; it does not establish correctness of a concrete implementation.

## Bounded TLC configuration
Two operation IDs and MaxSequence=2. AuthIds has two values so the checker can explore attempts to reuse one authorization ID. The model is intentionally small and finite.

## Checker availability
Java exists on the host, but no TLC executable or tla2tools.jar was found in the standard locations checked. No TLC execution result is claimed. No dependency was installed or downloaded.

## Next formal refinement
Model a replicated Witness with a finite set of replicas, leader term, quorum-committed log index, stale leader writes, election/fencing, and a client request/response channel with loss/retry. Then add crash/restart and durable recovery. These should be separate refinements so safety failures are attributable.

## Non-authorization
No runtime code, service, endpoint, namespace, credentials, key, storage adapter, ACL, mutex, network change, or Production integration was created. Existing files are preserved. IranPriceScout and its independent tunnel remain untouched.

Production: NO-GO.

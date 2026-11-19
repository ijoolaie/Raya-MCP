# AppendEntries response handling at an offline destination — guard witness (2026-11-13)

## Purpose

Isolate the destination-online precondition in the abstract `HandleAppendResponse` action.

Scenario:
1. A is a leader in term 3, and an old failure response from F to A is queued (term 3, requestId 4).
2. A goes offline while the response remains queued.
3. F remains online; the response still matches A's current term and last requestId.
4. The response must not be processed by an offline destination.

The response's requestId is deliberately equal to lastRequestId (4), and its term equals currentTerm (3), so those guards cannot independently reject it.

## Positive model

- Model: `WitnessV2AppendResponseDestinationOfflineGuard.tla`
- Config: `WitnessV2AppendResponseDestinationOfflineGuard.cfg`
- TLC version: 2026.11.06.222357, revision d8fb231; Java 25.0.2.
- Exit code 0; no invariant violation.
- 5 states generated, 4 distinct; 0 states left on queue.
- Complete graph depth 3; maximum outdegree 2.
- Invariants: `TypeOK`, `CursorLowerBound`, `OfflineDestinationResponseNotAccepted`.

In the positive action, the phase condition requires A to be offline while the handler's destination-online precondition requires A to be online. Therefore the response handler is disabled in the offline phase; the response can be dropped.

## Negative control

- Mutant: `WitnessV2AppendResponseDestinationOfflineGuardWithoutDestinationOnlineGuard.tla`
- Config: `WitnessV2AppendResponseDestinationOfflineGuardWithoutDestinationOnlineGuard.cfg`
- Only the destination-online check was removed; the offline phase, source-online, leader-role, term and requestId conditions remain.
- TLC found the expected violation of `OfflineDestinationResponseNotAccepted` (exit code 1).
- 4 states generated, 4 distinct; counterexample depth 3.

Trace:
- `WitnessV2AppendResponseDestinationOfflineGuardWithoutDestinationOnlineGuard_TTrace_1794574782.tla`
- `WitnessV2AppendResponseDestinationOfflineGuardWithoutDestinationOnlineGuard_TTrace_1794574782.bin`

Counterexample:
1. A and F are online; old failure response is queued; nextIndex=6.
2. `TakeDestinationOffline`: A goes offline, F remains online, response remains queued.
3. Mutant handles the response at offline A; nextIndex changes 6→5 and acceptedFailure becomes TRUE.

## Scope caveat

This is an intentionally narrow state-machine witness for the destination-online guard, not a claim that an offline process can physically execute code. The mutant represents precisely the forbidden transition that would exist if the handler's online precondition were omitted from the abstract transition relation. Role A remains recorded as Leader during the offline state solely to isolate the online guard; this does not model crash/restart role transitions, transport delivery, or complete Raft behavior.

## Change boundary

Created only the two model/config pairs, TLC metadata/traces, and this report. No base model, Production source, README, running service, tunnel, or archive was changed. No deletion, move, commit, reset, or cleanup was performed.

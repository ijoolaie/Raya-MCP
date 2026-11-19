# Combined AppendEntries response guards — ordering model (2026-11-13)

## Purpose

Exercise the three independent preconditions of the abstract `HandleAppendResponse` transition in one finite model:

- A must still have the Leader role.
- Response source F must be online.
- Response destination A must be online.

The queued message is a term-3 failure response from F to A with requestId 4. The requestId matches A's last request ID for F, and the response term matches A's current term. This deliberately prevents term and request-ID checks from masking the three guards under examination.

## Model and result

- Model: `WitnessV2AppendResponseCombinedGuards.tla`
- Config: `WitnessV2AppendResponseCombinedGuards.cfg`
- TLC: 2026.11.06.222357 (revision d8fb231), Java 25.0.2
- Result: PASS; TLC completed with no error.
- State space: 46 generated, 24 distinct, 0 states left on queue.
- Complete graph depth: 5.
- Outdegree: minimum 0, maximum 5, 95th percentile 3.
- Invariants: `TypeOK`, `CursorLowerBound`, `AcceptanceSnapshotValid`.

The model independently permits A's demotion, F going offline, and A going offline while the response remains queued. TLC explores the available action orders, including each invalidating transition before response handling. It also permits the response to be handled first, followed by subsequent role/availability changes. Three ghost snapshot booleans record the conditions at the instant the response is accepted; later changes to role or availability therefore do not incorrectly invalidate a previously legitimate acceptance.

When one or more guards are false, the response cannot be handled; it can be dropped. When all guards are true, it can be handled and nextIndex decreases from 6 to 5.

## Diagnostic run retained

The first run used TLC's default deadlock check. It reached a terminal state after the queued response was dropped and all three invalidating transitions had occurred. TLC therefore reported a deadlock even though no invariant was violated. This was a configuration issue, not a model invariant failure. The diagnostic trace is retained:

- `WitnessV2AppendResponseCombinedGuards_TTrace_1794575929.tla`
- `WitnessV2AppendResponseCombinedGuards_TTrace_1794575929.bin`

The final run sets `CHECK_DEADLOCK FALSE`, because this finite witness intentionally has terminal states. The successful result above is from that rerun.

## SHA-256

| Artifact | SHA-256 |
|---|---|
| Model | `146DFDBDD49A5E9C01368D00C2FFD30F9340B688331E347931FA037F334ECA73` |
| Config | `626A6CE23D20471BF3260EF6953CCB56D032E5D47D36AE59892EE5B771AA809D` |

## Scope and limitations

This is a small, bounded, abstract guard-ordering witness. It does not establish full Raft correctness, transport delivery guarantees, liveness, persistence, election safety, or production behavior. The online/offline and role transitions are deliberately abstracted to isolate the handler preconditions. This run checks the combined positive model; it does not yet include three independent guard-removal mutant runs.

## Change boundary

Created the combined model and config and retained TLC's diagnostic trace. No source/base model, Production code, README, service, tunnel, archive, or historical state was changed. No files were deleted, moved, committed, or reset.

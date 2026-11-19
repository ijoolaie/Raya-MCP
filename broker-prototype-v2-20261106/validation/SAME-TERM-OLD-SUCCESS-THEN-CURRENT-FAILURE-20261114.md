# Same-term delayed success followed by current failure — bounded witness (2026-11-14)

## Scope and environment
- Working copy only: `D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`
- TLC: `2026.11.06.222357`, revision `d8fb231`
- Java: Oracle JDK `25.0.2`
- One TLC worker, 1 GiB heap
- No base model, production code, source project, archive, service, or tunnel was changed.

## Scenario
Two same-term AppendEntries responses are simultaneously pending:
- delayed success: requestId 1, matchIndex 3
- current failure: requestId 2, equal to lastRequestId
- leader cursor before handling: nextIndex 4, matchIndex 1

The model permits either response to be handled first. The relevant safety property is:
`CursorLowerBound == nextIndex >= matchIndex + 1`.

## Baseline result
Model: `WitnessV2OldSuccessThenCurrentFailureSameTerm.tla`

TLC exit 12 — expected counterexample:
- 4 states generated, 4 distinct, depth 3.
- State 1: nextIndex=4, matchIndex=1.
- Handle delayed success: nextIndex=4, matchIndex=3.
- Handle current failure (requestId 2): nextIndex=3, matchIndex=3.
- `CursorLowerBound` is violated because 3 < 3+1.

This reproduces the ordering hazard in the current abstract failure update, which decrements nextIndex without clamping it to matchIndex+1.

## Guarded comparison result
Model: `WitnessV2OldSuccessThenCurrentFailureSameTermGuarded.tla`

Failure update uses:
`nextIndex' = IF nextIndex - 1 > matchIndex + 1 THEN nextIndex - 1 ELSE matchIndex + 1`

TLC exit 0 — no error found:
- 5 states generated, 4 distinct, 0 queued, complete graph depth 3.
- Both response orders are explored.
- `TypeOK` and `CursorLowerBound` pass.

## Interpretation and limits
This is a small finite witness isolating one response-order interaction. It demonstrates a concrete invariant violation in the abstract baseline transition and that clamping the failure update prevents this violation in this witness.

It does not establish that this is the only safe retry policy, nor prove full Raft correctness, liveness, persistence, crash recovery, or Production behavior. In particular, the candidate clamp is not applied to the base model or Production. A broader integration check is still required before changing the prototype.

## Artifacts
- `WitnessV2OldSuccessThenCurrentFailureSameTerm.tla/.cfg`
- `WitnessV2OldSuccessThenCurrentFailureSameTermGuarded.tla/.cfg`
- Baseline metadata: `validation\old-success-current-failure-same-term-final-20261114`
- Guarded metadata: `validation\old-success-current-failure-same-term-guarded-final-20261114`
- Counterexample trace: `WitnessV2OldSuccessThenCurrentFailureSameTerm_TTrace_1794633826.tla/.bin`

Earlier diagnostic attempts and their metadata were retained; they are not used as final results. No files were deleted.

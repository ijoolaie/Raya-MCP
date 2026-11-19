# Delayed old-success response monotonicity after a newer failure (2026-11-14)

## Scope
- Working copy only: \`D:\Work\project\Raya-MCP\broker-prototype-v2-20261106\`.
- Two bounded synthetic TLA+ checks; no edits to the base incremental model, source project, or Production.
- TLC 2026.11.06.222357 (revision d8fb231), Java 25.0.2, one worker, 512 MiB heap.
- Goal: verify that an older success response arriving late cannot regress \`matchIndex\` or \`nextIndex\`, including after a newer failure has already reduced the retry cursor.

## Model A — full interleaving with a current failure
Pending responses:
- Old success: requestId 1, matchedThrough 2.
- Newer success: requestId 2, matchedThrough 4.
- Current failure: requestId 3 (the latest request).

The model explores all reachable interleavings of the two successes and the current failure. Failure decrement is clamped at \`matchIndex + 1\`. Success handling is tested in two modes:
- Preserved: both \`matchIndex\` and \`nextIndex\` only move upward on success.
- Mutant: success overwrites both cursors with the response's values.

Preserved result:
- PASS, exit 0.
- 13 states generated, 8 distinct, 0 queued; complete graph depth 4.
- \`TypeOK\`, \`CursorLowerBound\`, and \`MatchIndexDominatesObservedSuccess\` passed.

Mutant result:
- Expected invariant violation, exit 12.
- 7 states generated, 7 distinct; depth 3.
- Trace: \`WitnessV2StaleSuccessMonotonicity_TTrace_1794637060.tla/.bin\`.
- TLC delivers newer success (request 2, matchedThrough 4), then older success (request 1, matchedThrough 2); the mutant regresses \`matchIndex\` from 4 to 2.

## Model B — specifically delayed old success after failure
Initial state represents an already accepted current failure:
- \`nextIndex=4\`, \`matchIndex=1\` (failure has reduced the retry cursor from 5 to 4).
- Both old success (request 1, matchedThrough 2) and newer success (request 2, matchedThrough 4) remain pending.

Preserved result:
- PASS, exit 0.
- 5 states generated, 4 distinct, 0 queued; complete graph depth 3.
- \`TypeOK\`, \`CursorLowerBound\`, and \`MatchIndexDominatesObservedSuccess\` passed.

Mutant result:
- Expected invariant violation, exit 12.
- 5 states generated, 5 distinct; depth 3.
- Trace: \`WitnessV2DelayedOldSuccessAfterFailure_TTrace_1794637103.tla/.bin\`.
- Counterexample: newer success advances \`matchIndex\` to 4 and \`nextIndex\` to 5; delayed older success then overwrites them to 2 and 3. This violates the observed-success high-water invariant.

## Interpretation
- The monotonic-success update used in the current abstract model is supported by these bounded checks: a late older success does not undo higher success evidence.
- The cursor lower bound remains satisfied in the preserved models.
- The failure decrement may legitimately lower \`nextIndex\`; therefore no global monotonicity invariant is imposed on \`nextIndex\`. The monotonicity requirement applies to success-driven updates and the observed \`matchIndex\` evidence.
- This does not establish that every success response should be accepted regardless of request ID, nor does it prove full Raft correctness, persistence, crash recovery, or production behavior.
- No base-model or Production integration was performed. The cursor clamp remains an experimental candidate only.

## Artifacts
- \`WitnessV2StaleSuccessMonotonicity.tla\`
- \`WitnessV2StaleSuccessMonotonicity-Preserved.cfg\`
- \`WitnessV2StaleSuccessMonotonicity-Mutant.cfg\`
- \`WitnessV2DelayedOldSuccessAfterFailure.tla\`
- \`WitnessV2DelayedOldSuccessAfterFailure-Preserved.cfg\`
- \`WitnessV2DelayedOldSuccessAfterFailure-Mutant.cfg\`
- Two mutant counterexample trace pairs listed above
- This report

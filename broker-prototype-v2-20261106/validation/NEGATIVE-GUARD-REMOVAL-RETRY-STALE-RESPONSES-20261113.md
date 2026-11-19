# Negative guard-removal diagnostics — retry/stale responses (2026-11-13)

## Purpose
Two deliberately broken copies of the bounded retry witness were checked to verify that the stale-response invariants detect removal of each response-term guard. These are negative controls only; they are not candidate implementation models.

## Test A — remove success-response term guard
Artifact: WitnessV2AppendEntriesRetryMutantWithoutSuccessTermGuard.tla/.cfg
Mutation: replaced the success handler's `m.term = currentTerm` guard with TRUE. All other guards and invariants were retained.
TLC result: expected invariant violation (exit code 12).
- Violated invariant: `OldSuccessNeverAccepted`
- Trace depth: 2 transitions
- 3 states generated, 3 distinct
- The term-1 success response (requestId=2, matchIndex=5) was accepted while currentTerm=2.
- leaderMatch changed from 0 to 5; leaderNext remained 6.
- Trace file: WitnessV2AppendEntriesRetryMutantWithoutSuccessTermGuard_TTrace_1794564711.tla/.bin

## Test B — remove failure-response term guard
Artifact: WitnessV2AppendEntriesRetryMutantWithoutFailureTermGuard.tla/.cfg
Mutation: replaced the failure handler's `m.term = currentTerm` guard with TRUE. The requestId equality guard was deliberately retained.
TLC result: expected invariant violation (exit code 12).
- Violated invariant: `OldFailureNeverAccepted`
- Trace depth: 4 transitions
- 15 states generated, 12 distinct
- The trace sends request IDs 1 and 2 first, making lastRequestId=2; the old term-1 failure response also has requestId=2 and is then incorrectly accepted.
- leaderNext changed from 6 to 5 while leaderMatch remained 0.
- Trace file: WitnessV2AppendEntriesRetryMutantWithoutFailureTermGuard_TTrace_1794564732.tla/.bin

## Interpretation
Both negative controls fail for the intended reason. This demonstrates that the positive witness's stale-response ghost invariants are sensitive to omission of the term check:
- for success, omission permits stale matchIndex to inflate leaderMatch;
- for failure, omission permits a same-requestId stale failure to decrement nextIndex.

The failure test intentionally retains requestId matching, so it isolates the term guard rather than conflating two missing protections. The success test isolates its term guard as modeled in the base response handler.

## Relationship to the positive witness
The corrected positive model remains:
- WitnessV2AppendEntriesRetryWithStaleResponsesAcrossTerms.tla/.cfg
- 125 generated, 52 distinct states; depth 9; 0 queued; exit code 0; all 11 invariants passed.

## Limits and safety
This is a bounded model-level sensitivity test, not a source-code mutation test and not a full Raft proof. It does not establish Production correctness. Both mutant files are kept separate from the positive witness; no base model or Production code was changed. Diagnostic traces are retained. No files were deleted.

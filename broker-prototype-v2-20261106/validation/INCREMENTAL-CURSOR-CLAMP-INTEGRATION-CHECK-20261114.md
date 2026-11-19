# Incremental cursor clamp — integration-level bounded check (2026-11-14)

## Scope
- Working copy only: `D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`
- Derived models copied from `WitnessV2AppendEntriesIncremental.tla`; the base model was not edited.
- TLC `2026.11.06.222357` (`d8fb231`), Java `25.0.2`, one worker, 1 GiB heap.
- Bounds: 3 nodes, `MaxTerm=1`, `MaxLog=1`, `MaxRequestId=2`, node symmetry; no `MessageBound` constraint, so multiple messages can coexist.

## Baseline comparison
Model: `WitnessV2AppendEntriesIncrementalCursorClampBaseline.tla`

TLC exit 12 — expected `CursorLowerBound` counterexample.
- Trace: `WitnessV2AppendEntriesIncrementalCursorClampBaseline_TTrace_1794634223.tla` (and TLC binary companion).
- 155,193 states generated, 17,673 distinct, depth 13 at detection; 3,769 queued when TLC stopped on the invariant violation.
- The reachable sequence includes two requests to the same follower (request IDs 1 and 2), then a success response for request 1 and a failure response for request 2. After the success, `matchIndex=1` and `nextIndex=2`; processing the current failure reduces `nextIndex` to 1 while `matchIndex` remains 1. Thus `nextIndex < matchIndex+1`.

## Guarded candidate
Model: `WitnessV2AppendEntriesIncrementalCursorClampCandidate.tla`

Only the failure update is changed to clamp the new cursor to at least `matchIndex+1`:
`IF @ - 1 > matchIndex[m.dst][m.src] + 1 THEN @ - 1 ELSE matchIndex[m.dst][m.src] + 1`.

TLC exit 0 — no error found.
- 100,066 states generated, 9,024 distinct, 0 queued; complete graph depth 14.
- `TypeOK`, `CommitIndexBound`, `NextIndexBound`, `MatchIndexBound`, `CommittedPrefixAgreement`, and `CursorLowerBound` all passed.
- Multiple in-flight APPEND messages and out-of-order response processing were allowed by the model; this exercises repeated request IDs/retries within the finite bounds.

## Duplicate-response limitation
`messages` is a mathematical SET. Two byte-for-byte identical response records collapse into one element, so this integration run does not model two separately delivered copies of the exact same response. Distinct responses (for example, different request IDs or success/matchIndex fields) can coexist and be reordered. Duplicate-response behavior therefore remains a separate validation item; this run must not be described as a duplicate-delivery proof.

## Interpretation
The broader bounded model reproduces the cursor lower-bound violation in the unmodified failure update and passes with the candidate clamp. This supports the clamp as a candidate for further review, not as an approved base-model change. It does not prove full Raft correctness, liveness, persistence, crash recovery, or Production behavior. No base model or Production code was changed.

## Files
- `WitnessV2AppendEntriesIncrementalCursorClampBaseline.tla/.cfg`
- `WitnessV2AppendEntriesIncrementalCursorClampCandidate.tla/.cfg`
- `WitnessV2AppendEntriesIncrementalCursorClampBaseline_TTrace_1794634223.tla/.bin`
- This report

TLC temporary work directories were outside the project and are runtime scratch. No project evidence, trace, checkpoint, configuration, or source file was deleted.

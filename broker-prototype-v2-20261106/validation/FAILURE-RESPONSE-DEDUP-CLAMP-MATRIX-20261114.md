# Failure-response deduplication and cursor clamp matrix (2026-11-14)

## Scope
- Working copy only: `D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`.
- Focused finite TLA+ model; no edits to the base incremental model, source project, or Production.
- TLC `2026.11.06.222357` (revision `d8fb231`), Java `25.0.2`, one worker, 512 MiB heap.
- Duplicate logical responses use a model-only `copyId` to represent two deliveries in a set-based model.
- `handledFailureIds` records failure request IDs already accepted. Under Dedup, a later duplicate delivery is explicitly discarded from `pending`; it is not left permanently blocking progress.
- The model includes a subsequent request 3 after all request-2 failure copies have been consumed/discarded.

## Four-way matrix

| Configuration | ClampFailures | DeduplicateFailures | Result |
|---|---:|---:|---|
| Baseline | FALSE | FALSE | Expected `CursorLowerBound` violation; exit 12 |
| ClampOnly | TRUE | FALSE | PASS; exit 0 |
| DedupOnly | FALSE | TRUE | Expected `CursorLowerBound` violation; exit 12 |
| ClampAndDedup | TRUE | TRUE | PASS; exit 0 |

### Exact results
- Baseline: 6 generated, 6 distinct, depth 3 at violation. Both identical request-2 failure copies are accepted; `nextIndex` falls 3 -> 2 -> 1 while `matchIndex=1`.
- ClampOnly: 49 generated, 24 distinct, 0 queued, complete graph depth 7. `TypeOK` and `CursorLowerBound` pass.
- DedupOnly: 12 generated, 11 distinct, depth 3 at violation. A stale success raises `matchIndex` to 2; one legitimate request-2 failure then moves `nextIndex` from 3 to 2, violating the lower bound. Dedup does not replace the clamp.
- ClampAndDedup: 49 generated, 24 distinct, 0 queued, complete graph depth 7. `TypeOK` and `CursorLowerBound` pass.

## Newer request acceptance
The combined configuration uses `FairSpec == Spec /\ WF_vars(Next)` and checks temporal property `EventuallyHandlesRequest3` (`<> (3 \in handledFailureIds)`). TLC passed the temporal property over the complete 24-state graph. This confirms, within this model, that after request-2 duplicate suppression the next request ID 3 can be accepted; deduplication does not permanently suppress a newer request.

## Important modeling correction
An initial dedup-only implementation merely disabled handling of an already-seen duplicate but left that copy in `pending`. That blocked the model from starting the next request and caused the temporal acceptance property to fail. The final model adds `DiscardDuplicateFailure`, which consumes a duplicate delivery without changing the cursor. The final matrix and temporal pass use this corrected behavior.

## Interpretation and limits
- Clamp and dedup address separate properties: the clamp enforces `nextIndex >= matchIndex + 1`; dedup suppresses repeated processing of the same failure request ID.
- Request-ID-only dedup is still a candidate policy, not a confirmed protocol design. It assumes at most one meaningful failure decision per request ID; the base protocol may produce distinct responses for retransmitted APPENDs sharing an ID. That semantic question must be resolved before integration.
- The model abstracts away elections, real transport, replication, persistence, crashes, and liveness beyond the single fairness check stated above. It is not a full Raft proof.
- No base model, Production code, source project, or prior evidence was modified.

## Artifacts
- `WitnessV2FailureResponseDedupMatrix.tla`
- `WitnessV2FailureResponseDedupMatrix-Baseline.cfg`
- `WitnessV2FailureResponseDedupMatrix-ClampOnly.cfg`
- `WitnessV2FailureResponseDedupMatrix-DedupOnly.cfg`
- `WitnessV2FailureResponseDedupMatrix-ClampAndDedup.cfg`
- Baseline counterexample trace: `WitnessV2FailureResponseDedupMatrix_TTrace_1794634746.tla/.bin`
- Dedup-only counterexample trace: `WitnessV2FailureResponseDedupMatrix_TTrace_1794634748.tla/.bin`
- This report

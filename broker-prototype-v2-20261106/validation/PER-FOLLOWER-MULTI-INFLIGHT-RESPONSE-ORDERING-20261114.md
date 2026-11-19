# Per-follower multi-inflight response ordering (2026-11-14)

## Scope
- Isolated finite TLA+ model in working copy: D:\Work\project\Raya-MCP\broker-prototype-v2-20261106.
- Two followers (B, D), each with independent nextIndex, matchIndex, and latest request ID 3.
- Three requests per follower are in flight concurrently. For each peer, success and failure responses for request IDs 1, 2, and 3 are pending; TLC explores nondeterministic processing order.
- Success updates are monotonic (max); failure is eligible only for that peer's latest request ID, is deduplicated by (term, leader, peer, requestId), and clamps nextIndex to matchIndex+1.
- No base model, source project, or Production changes.

## Positive run (request-ID guard enabled)
- TLC exit 0; no error found.
- 24,577 states generated, 4,096 distinct, 0 queued; complete graph depth 13.
- Temporal properties checked over 8,192 states (two temporal branches).
- Invariants passed: TypeOK, CursorLowerBound for both followers, NoStaleFailureAcceptance.
- Temporal properties passed: EventuallyBothLatestFailuresHandled and EventuallyAllResponsesDrained.
- Stale failure responses for IDs 1 and 2 are discarded; current ID 3 failure is handled for each peer. Success responses can arrive in any explored order without regressing matchIndex/nextIndex. Per-peer cursor maps ensure a response only updates its source peer.

## Mutation control (request-ID guard disabled)
- Expected invariant violation observed; TLC exit 1.
- 8 states generated, 8 distinct, 6 queued at counterexample discovery; depth 2.
- Trace pair: WitnessV2PerFollowerMultiInflightOrdering_TTrace_1794642724.tla/.bin.
- Counterexample: B's failure response for request ID 1 is accepted although lastRequestId[B]=3. It lowers nextIndex[B] from 5 to 4 and sets NoStaleFailureAcceptance's ghost flag. This confirms that peer scoping alone does not make an older failure response safe.

## Interpretation and limits
- Evidence covers two peers and three in-flight request IDs per peer, all represented as finite message sets with nondeterministic response ordering.
- Success response handling intentionally retains the existing simplified abstraction: no request-ID freshness guard is added to success responses. Monotonic max updates protect against regression in this model, but this is not proof that every success response is semantically valid for a real follower log.
- The model abstracts transport, retries creating new messages, real per-peer implementation maps, elections, quorum/commit behavior, persistence, and concurrent runtime scheduling.
- Bounded model evidence only; not a full Raft proof, crash-persistence validation, or Production acceptance. No experimental logic was merged into the base model.

## Artifacts
- WitnessV2PerFollowerMultiInflightOrdering.tla
- WitnessV2PerFollowerMultiInflightOrdering.cfg
- WitnessV2PerFollowerMultiInflightOrdering-WithoutRequestIdGuard.cfg
- Mutation trace pair: WitnessV2PerFollowerMultiInflightOrdering_TTrace_1794642724.tla/.bin
- This report

## Cleanup and safety
- No files were deleted in this task; the final mutation trace is retained.
- Root temporary *.tmp.cfg count: 0; active TLC Java process count: 0.
- No source project or Production files changed; no commit or reset performed.

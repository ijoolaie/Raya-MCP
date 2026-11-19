# Term Change with Two Peers and Two Request IDs — 2026-11-14

## Objective

Extend the focused term-change scenario from one follower to two independent follower peers (B and C), retaining two request IDs per peer. Exercise delayed old-term successes, current-term successes and failures, and arbitrary response ordering without invoking the full incremental model.

## Model scope and semantics

- One fixed leader A and two peers B and C.
- Terms 1 and 2; request IDs 1 and 2 are reused independently for each peer.
- Old-term success responses for both request IDs remain pending across the term transition for both peers.
- New-term success and failure responses for both request IDs coexist for both peers.
- Each message carries its source peer; each cursor is a function indexed by peer.
- Response handlers select the target peer from the message and update only that peer's cursor. Failure deduplication uses (term, leader, peer, requestId).
- The term transition resets both matchIndex entries but preserves each peer's nextIndex and lastRequestId.
- The model checks cursor lower bounds, bounded per-peer matchIndex values, old-term rejection, stale failure rejection, and eventual handling/draining.

The model is intentionally abstract. It does not model actual AppendEntries delivery or follower logs, leader elections, quorum/commit, network transport, crash/restart, or persistence. The peer-isolation claim is limited to the indexed state and action update structure in this model; it is not an implementation-level proof.

## Positive run

Config: `WitnessV2TermChangeTwoPeersFocused.cfg`

All five invariants and both temporal properties were active:
- TypeOK
- CursorLowerBound
- PeerCursorIsolation
- NoOldTermAcceptance
- NoStaleFailureAcceptance
- EventuallyCurrentFailuresHandled
- EventuallyAllResponsesDrained

Result:
- TLC exit code 0 — **PASS**
- 24,625 states generated
- 4,112 distinct states
- 0 states left on queue
- Complete graph depth 14
- Temporal checking completed for all 3 branches
- No invariant or temporal-property violation

## Mutation controls

### Term guard disabled

Config: `WitnessV2TermChangeTwoPeersFocused-WithoutTermGuard.cfg`

- TLC exit code 12 — expected invariant violation.
- 7 states generated; 7 distinct; depth 3.
- Counterexample accepts a delayed term-1 success for peer B after the term has advanced to 2 and both matchIndex entries have been reset.
- Violated property: `NoOldTermAcceptance`.
- Retained trace: `WitnessV2TermChangeTwoPeersFocused_TTrace_1794664889.tla/.bin`.

### Request-ID guard disabled

Config: `WitnessV2TermChangeTwoPeersFocused-WithoutRequestIdGuard.cfg`

- TLC exit code 12 — expected invariant violation.
- 11 states generated; 11 distinct; depth 3.
- Counterexample accepts a term-2 failure response for peer B with requestId 1 while requestId 2 is the latest. B's nextIndex moves from 5 to 4 while C's nextIndex remains 7 in that transition.
- Violated property: `NoStaleFailureAcceptance`.
- Retained trace: `WitnessV2TermChangeTwoPeersFocused_TTrace_1794664893.tla/.bin`.

Both mutation controls detected their respective missing guard. The RequestId mutation trace also demonstrates that the handler changes the addressed peer's cursor while leaving the other peer's cursor unchanged in that step.

## Findings and limits

1. The positive bounded model passes with two followers and two request IDs per follower across a term transition.
2. The term guard and latest-request-ID guard remain independently necessary in the two-peer scenario.
3. Per-peer cursor updates are indexed by the response source; the mutation trace shows a B response does not change C's cursor in the observed transition.
4. This is a focused finite-state result. It does not establish full Raft correctness or prove behavior of production code. It does not close the previously inconclusive full-model Term2/RequestId2 exploration.

## Artifacts

- `WitnessV2TermChangeTwoPeersFocused.tla`
- `WitnessV2TermChangeTwoPeersFocused.cfg`
- `WitnessV2TermChangeTwoPeersFocused-WithoutTermGuard.cfg`
- `WitnessV2TermChangeTwoPeersFocused-WithoutRequestIdGuard.cfg`
- The two final mutation trace pairs listed above
- This report

## Integrity and cleanup

- One initial TLC invocation failed at parsing because of an invalid function-constructor spelling. It did not execute the model and produced no trace. The syntax was corrected before the successful run.
- Zero TLC processes remained after the tests.
- Zero root-level `*.tmp.cfg` files were present.
- Base model SHA256 remains `C85CF1906D92DF31A62F8DA63392673809D3A314F30012242FB2B6E54B203A57`.
- Final counterexample traces were retained; no test evidence was deleted.
- No base model/config, source project, or Production files changed.
- No commits or resets.

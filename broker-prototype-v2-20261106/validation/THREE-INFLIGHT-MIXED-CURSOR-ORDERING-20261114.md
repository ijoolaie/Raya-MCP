# Three in-flight AppendEntries: mixed response ordering and cursor safety (2026-11-14)

## Scope
- Isolated bounded TLA+ experiment in the working copy only: D:\Work\project\Raya-MCP\broker-prototype-v2-20261106.
- Three outstanding requests to one follower, request IDs 1, 2, 3, with responses allowed in arbitrary order.
- Response set contains successes for all three IDs (matched-through 2, 3, 4) and failures for all three IDs.
- Success handling follows the current abstraction: accept a response for a sent request in the same fixed term/peer context; update matchIndex and nextIndex monotonically with max().
- Failure handling follows the current abstraction: only the latest request ID is eligible; stale failures are discarded; a current failure is handled once and subsequent duplicate failure copies are discarded.
- ClampFailures is a test-only switch. No base model, source project, or Production changes.

## Positive bounded check (clamp enabled)
- TLC exit 0; no error found.
- 193 states generated, 64 distinct states, 0 queued; complete graph depth 7.
- TypeOK and CursorLowerBound passed.
- Temporal properties EventuallyLatestFailureHandled and EventuallyAllResponsesDrained passed (TLC checked 128 distinct states for the temporal-property branches).
- The interleavings include old success responses arriving after newer ones, stale failures, and a latest-request failure after success has advanced matchIndex.
- The max() success update preserves monotonic matchIndex/nextIndex; the clamp prevents a current failure from moving nextIndex below matchIndex+1.

## Negative control (clamp disabled)
- Expected invariant violation; TLC exit 1.
- 20 states generated, 17 distinct states, 12 queued at counterexample discovery; depth 3.
- Counterexample ordering:
  1. Initial nextIndex=5, matchIndex=1, with requests 1..3 outstanding.
  2. Success response for request 3 reports matchIndex=4; monotonic success handling sets matchIndex=4, nextIndex remains 5.
  3. Current failure response for request 3 is accepted and decrements nextIndex to 4.
  4. CursorLowerBound fails: nextIndex=4 but matchIndex+1=5.
- Trace pair: WitnessV2ThreeInflightMixedCursor_TTrace_1794641294.tla/.bin.

## Interpretation and limits
- This test shows the interaction hazard directly: a valid success from a newer in-flight request can raise matchIndex, after which a later failure for that same latest request must not decrement nextIndex below matchIndex+1.
- Stale failure filtering by request ID alone is not enough to preserve CursorLowerBound; the lower-bound clamp is independently necessary in this abstraction.
- Success responses are not required to be the latest request ID in this model, matching the existing simplified success-response handling. This experiment does not establish that such acceptance is correct in every protocol context.
- The model fixes one term, one leader, one follower, and three already-sent requests. It does not model elections, changing terms, actual follower log contents, transport, persistence, or full Raft behavior.
- This is bounded model evidence, not a full Raft proof, crash-persistence validation, liveness proof for the implementation, or Production acceptance.

## Artifacts
- WitnessV2ThreeInflightMixedCursor.tla (3,208 bytes)
- WitnessV2ThreeInflightMixedCursor.cfg (188 bytes)
- WitnessV2ThreeInflightMixedCursor-NoClamp.cfg (113 bytes)
- Counterexample trace pair above
- This report

## Cleanup and safety
- Root temporary *.tmp.cfg count: 0.
- Active TLC Java processes after completion: 0.
- No test-generated files were deleted; the counterexample is retained as evidence.
- No source project or Production files changed; no commit or reset performed.

# Mixed Success/Failure and Duplicate Responses Across Peers — 2026-11-14

## Scope
A bounded finite model for two independent follower cursors (B and D). For each peer, the pending set contains:
- success responses for request IDs 1 and 2, each duplicated; the older success reports matchedThrough=2 and the newer success reports matchedThrough=4;
- failure responses for request IDs 1 and 2, each duplicated;
- request ID 1 failures are stale and discarded; request ID 2 failures are current and eligible.
All success/failure records coexist, and TLC explores arbitrary delivery order. copyId distinguishes logically duplicate transport records in the mathematical set.

Success handling uses monotonic max() updates for matchIndex and nextIndex. Failure handling is guarded by the latest request ID, deduplicated by (term, leader, peer, requestId), and clamps nextIndex to at least matchIndex+1. Each update indexes the cursor map by the response's peer.

## Positive run
- TLC PASS, exit 0
- 524,289 states generated
- 65,536 distinct states
- 0 states left on queue
- complete graph depth 17
- TypeOK, CursorLowerBound, NoMatchRegression, NoFailureDuplicateEffect passed
- EventuallyAllLatestFailuresHandled and EventuallyPendingDrained passed (both temporal branches checked over the complete state space).

## Negative controls
### Failure deduplication disabled
- Expected invariant violation: NoFailureDuplicateEffect
- Exit 12; 146 states generated, 110 distinct; depth 3 at counterexample discovery.
- Trace: WitnessV2MixedDuplicateMultiPeerCursor_TTrace_1794644898.tla/.bin
- The same current failure key is accepted a second time for peer B. The duplicate-effect monitor fires. The cursor can continue decrementing even while respecting the lower bound; therefore duplicate suppression and cursor clamping are separate protections.

### Cursor clamp disabled
- Expected invariant violation: CursorLowerBound
- Exit 12; 55 states generated, 52 distinct; depth 3 at counterexample discovery.
- Trace: WitnessV2MixedDuplicateMultiPeerCursor_TTrace_1794644902.tla/.bin
- A current failure arrives after a success advanced matchIndex to 4; without clamp, nextIndex falls from 5 to 4, violating nextIndex >= matchIndex+1.

## Interpretation and limitations
This is bounded model evidence for the stated two-peer, two-request scenario, not a full Raft proof. The follower match prefix is held stable at 4; crash-induced log loss, leader elections, commit/quorum behavior, transport code, and persistence are not modeled. Success responses intentionally have no request-ID freshness guard; monotonic updates alone do not prove every success semantically current. Duplicate records are represented with model-only copyId so that a set-based message model can contain identical logical deliveries.

The model's NoPeerIsolationViolation/CursorBoundMonitor ghost placeholders are not used as independent evidence; peer indexing is explicit in the actions, while the directly checked cursor safety property is CursorLowerBound.

## Artifacts
- WitnessV2MixedDuplicateMultiPeerCursor.tla
- WitnessV2MixedDuplicateMultiPeerCursor.cfg
- WitnessV2MixedDuplicateMultiPeerCursor-WithoutDedup.cfg
- WitnessV2MixedDuplicateMultiPeerCursor-WithoutClamp.cfg
- both mutant trace pairs listed above
- this report: validation/MIXED-DUPLICATE-MULTIPEER-CURSOR-ORDERING-20261114.md

## Change boundary and cleanup
Only the working copy was modified. No base model, source project, or Production files changed. No commits or resets. An earlier over-large exploratory TLC run was force-stopped before completion and is not counted as validation evidence. No project files were deleted in this task. Root temporary cfg files and active TLC processes were checked after the completed runs.

# Old-term response with two new-term requests — TLC recheck — 2026-11-13

## Scope

A fresh, isolated bounded witness in the D: copy. It starts after an abstract transition to term 2, with a delayed term-1 failure response still in the message set. It then sends two term-2 AppendEntries requests with IDs 1 and 2. The old failure response deliberately has requestId 2, colliding with the second new request.

## Interleavings modeled

- Send request 1, then request 2 (monotonic IDs).
- Deliver request 1 and request 2 independently.
- Handle the two successful term-2 responses in either order.
- Drop the old term-1 failure independently of the new-term request/response actions.
- `HandleOldFailure` is specified with both term equality and request-ID equality guards. Because its term is stale, it is disabled even when requestId matches the latest request. `DropOldFailure` removes it without changing progress cursors.

## TLC result

TLC `2026.11.06.222357` (revision `d8fb231`), Java `25.0.2`:

- PASS, exit code 0
- 50 states generated; 26 distinct states
- 0 states left on queue
- complete graph depth 8; maximum outdegree 3
- 8 invariants checked

The key invariant `OldTermFailureCannotAffectCursor` held for every reachable state. `DeliveryRequiresSend`, `SentIdsMonotonic`, and the cursor bounds also held. The state graph includes the two current-term responses arriving in either order while the old failure is still pending or dropped.

## Files

- `WitnessV2AppendEntriesOldResponseWithTwoNewRequests.tla`
- `WitnessV2AppendEntriesOldResponseWithTwoNewRequests.cfg`
- TLC metadir: `validation/old-response-with-two-new-requests-20261113`

## SHA-256

- Model: `599E7137018CD581B89FA378E22AEEDA5BBE7A270328C1BE075F4F07D3A1E55D`
- Config: `48F6C9014CDDCA5602CB2E97D06715A6431600F4D272A45C3E7F039EF138F0CF`

## Limits

This witness abstracts the election/term transition as an initial term-2 state and abstracts follower log mutation/commit processing. It checks message and response ordering plus the term/request-ID guard, not the full Raft protocol, crash persistence, liveness, or production code. The separate single-request witness is retained; its constant-level invariant warning does not affect this newer run and it is not used as evidence for this two-request result.
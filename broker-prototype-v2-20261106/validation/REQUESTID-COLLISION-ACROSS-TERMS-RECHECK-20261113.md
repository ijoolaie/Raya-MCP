# Request-ID collision across terms — TLC recheck — 2026-11-13

## Question

Can a delayed term-1 failure response decrement the progress cursor of a term-2 leader when the old and current requests reuse the same request ID?

## Bounded witness

The isolated model starts in an abstract term-2 leader state with `lastRequestId = 2`, `leaderNext = 6`, and `leaderMatch = 0`. Its message set contains:

- an old failure response `(term=1, requestId=2)`;
- a current success response `(term=2, requestId=2)`.

Thus the request IDs deliberately collide. `HandleFailure` requires both `resp.term = currentTerm` and `resp.requestId = lastRequestId`; `DropOldTerm` can discard the term-1 response without changing cursors. The current success can advance `leaderMatch` while leaving `leaderNext` at 6.

## Result

TLC `2026.11.06.222357` / Java `25.0.2`:

- PASS, exit code 0
- 5 states generated; 4 distinct
- 0 states left on queue
- depth 3; maximum outdegree 2
- 6 invariants checked

The invariant `OldTermFailureCannotDecrementCursor` held across the explored state graph, despite the matching request ID. The current-term success response remained eligible for handling.

## Files

- `WitnessV2AppendEntriesRequestIdCollisionAcrossTerms.tla`
- `WitnessV2AppendEntriesRequestIdCollisionAcrossTerms.cfg`
- TLC metadir: `validation/requestid-collision-across-terms-20261113`

## SHA-256

- Model: `CCEBC5515BBA9FDAD2C408D2CBEF60CFE8D70A6B018F75251954EB5ABA0F2D9E`
- Config: `5B74ECA26DB34B7748D9BC9C81A22385C32191887D02F713434C9E7ECBF9894E`

## Limits

This is a deliberately minimal response-handler witness. It abstracts away request transmission, follower processing, election mechanics, and persistence. It validates the term guard in the response handler under a request-ID collision; it is not a full integration proof or a claim about production code.
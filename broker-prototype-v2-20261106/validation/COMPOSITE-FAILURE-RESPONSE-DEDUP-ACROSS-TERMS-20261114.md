# Composite-key failure-response deduplication across terms (2026-11-14)

## Scope and setup
- Working copy only: `D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`.
- Focused finite TLA+ model; no edits to the base incremental model, source project, or Production.
- TLC `2026.11.06.222357` (revision `d8fb231`), Java `25.0.2`, one worker, 512 MiB heap.
- Same leader identity (A), same peer (B), and same request ID (1) are deliberately reused after a term change. This isolates the term component of the deduplication key.
- Duplicate logical deliveries are represented by separate `copyId` values because TLC model messages are held in a set.

## Deduplication key
`FailureKey(m) = <<m.term, m.dst, m.src, m.requestId>>`, interpreted as:
1. response term,
2. leader identity (response destination),
3. peer identity (response source),
4. request ID.

The model records accepted keys in `handledKeys`. An already-handled exact duplicate is consumed by `DiscardDuplicate` without changing the cursor. A new term resets `lastRequestId` to zero and then sends request ID 1 again.

## Positive run
Configuration: `ClampFailures=TRUE`, `CheckTermGuard=TRUE`.

- TLC exit: 0 (PASS).
- 23 states generated, 16 distinct states.
- 0 states left on queue; complete graph depth 6.
- Invariants passed: `TypeOK`, `CursorLowerBound`, `NoOldTermResponseAccepted`.
- Temporal property passed: `EventuallyNewTermFailureHandled`.

This establishes in the bounded model that:
- an exact duplicate from term 1 is not processed twice;
- after term advances to 2, the same leader, same peer, and reused request ID 1 produce a distinct dedup key;
- the term-2 response can be accepted;
- an old-term response is not accepted when the term guard is active;
- the cursor lower bound remains intact with the clamp enabled.

## Mutation control: remove term guard
Configuration: `CheckTermGuard=FALSE`.

- Expected invariant violation observed; TLC exit 12.
- Counterexample reaches term 2 while a term-1 response remains pending.
- The old response has the same leader, peer, and request ID as the current request. Without the term guard it is accepted, and `NoOldTermResponseAccepted` fails.
- Trace: `WitnessV2CompositeFailureResponseDedup_TTrace_1794635096.tla/.bin`.

This is a successful mutation control: the monitor detects removal of the term guard.

## Interpretation and limits
- A composite dedup key avoids conflating reused request IDs across terms and peers/leaders.
- The response-term equality guard remains independently necessary; the mutant demonstrates why the dedup key alone must not authorize stale-term responses.
- The model uses one leader identity across the term change to isolate the term field. It does not establish a complete implementation policy for elections, transport retries, multiple concurrent leaders, persistence, crashes, or full Raft behavior.
- Clamp and composite dedup remain experimental candidates. No integration into the base model or Production is authorized or performed.

## Artifacts
- `WitnessV2CompositeFailureResponseDedup.tla`
- `WitnessV2CompositeFailureResponseDedup.cfg`
- `WitnessV2CompositeFailureResponseDedup-WithoutTermGuard.cfg`
- Mutation trace: `WitnessV2CompositeFailureResponseDedup_TTrace_1794635096.tla/.bin`
- This report

# Term-change stale-response recheck — 2026-11-13

## Scope

This is an isolated bounded TLA+ witness in the D: working copy only. It extends the two-request nondeterministic-delivery scenario with a term transition and a deliberately abstract new-leader assumption. It does not modify the base model, application code, services, or production behavior.

## Modeled sequence

- Two AppendEntries requests are issued in term 1 with the same previous-log anchor.
- The follower nondeterministically processes them: one succeeds and the other is rejected by the committed-prefix guard.
- The two term-1 responses may remain pending when the term advances to 2.
- `AdvanceTerm` models observing a higher term and stepping down.
- `AssumeNewLeader` is an environment abstraction, not an election protocol: it models a leader being established in term 2 and resets `nextIndex` to `Len(LeaderLog)+1`, `matchIndex` to 0, and `lastRequestId` to 0.
- Response handlers require the response term to equal the current term. Old-term responses therefore cannot be handled as current responses; `DropOldTermResponse` can discard them without changing the new leader's progress cursors.

## Validation

TLC version `2026.11.06.222357` (revision `d8fb231`), Java `25.0.2`.

- Final run: PASS, exit code 0.
- 50 states generated; 31 distinct states; 0 states left on queue.
- Complete state-graph depth: 7; maximum outdegree: 3.
- 16 invariants checked, including type, committed-prefix, cursor bounds, exactly-one acceptance/rejection, term-2 cursor reset, and `OldTermResponsesDoNotAdvanceNewLeader`.
- No Java process remained after the run.

## Diagnostic history

The first run exited 12 because `AllResponsesHandled` incorrectly constrained cursor values in the intermediate term-2 follower state. The trace showed the model had correctly stepped down after term advancement; the invariant was too broad. The invariant was narrowed so the term-2 cursor condition applies only after the abstract new leader is assumed. The first-run metadir and trace were preserved; no files were deleted.

## Interpretation and limits

The bounded state space supports this narrow claim: in this witness, once the node is in term 2 and the abstract new-leader state is established, responses carrying term 1 cannot advance or decrement its progress cursors. It does not prove a complete Raft election, arbitrary-term behavior, crash/recovery safety, liveness, durable state, or production implementation correctness. The new-leader transition is an explicit environment abstraction.

## Files

- `WitnessV2AppendEntriesTermChangeStaleResponses.tla`
- `WitnessV2AppendEntriesTermChangeStaleResponses.cfg`
- TLC metadir: `validation/term-change-stale-responses-20261113-retry`
- Preserved diagnostic metadir: `validation/term-change-stale-responses-20261113`

## SHA-256

- Model: `B3656173BBEF64E659CE1E0AB16E38E4A291FD180E173960110AC2F6DC2FCED4`
- Config: `C8B80BADF75F2B8D90138DD290D8672CCAB722DB34092D42686D414DD3F97217`
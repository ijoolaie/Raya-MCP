# Same-term AppendEntries demotion followed by an old failure response — 2026-11-13

## Purpose

Check the receiver-role guard in the failure branch of the abstract AppendEntries response handler. The modeled event order is:

1. A is a leader in term 3 and has an in-flight failure response from F (term 3, requestId 4).
2. A receives and delivers a same-term AppendEntries message from B (term 3), and is demoted to follower. The old response remains queued.
3. The old response is then considered by the handler.

The old response deliberately uses requestId 4, equal to A's lastRequestId for F. This prevents the requestId equality guard from independently rejecting it, isolating the role guard. If incorrectly applied, the failure response decrements A's nextIndex from 6 to 5.

## Positive model

- Model: `WitnessV2AppendEntriesDeliverSameTermAppendThenOldFailure.tla`
- Config: `WitnessV2AppendEntriesDeliverSameTermAppendThenOldFailure.cfg`
- TLC: 2026.11.06.222357 (revision d8fb231), Java 25.0.2
- Result: PASS; exit code 0; no invariant violation.
- State space: 5 generated, 4 distinct, 0 queued; complete graph depth 3; maximum outdegree 2.
- Checked invariants: `TypeOK`, `CursorLowerBound`, `OldFailureNotAppliedAfterDemotion`, `DemotionPhaseConsistent`.

The positive model's handler requires both that the same-term AppendEntries message has already been delivered (`SameTermAppend \notin messages`) and that A is still a leader. Thus the old failure cannot be applied after demotion.

## Negative control: role guard removed

- Model: `WitnessV2AppendEntriesDeliverSameTermAppendThenOldFailureWithoutRoleGuard.tla`
- Config: `WitnessV2AppendEntriesDeliverSameTermAppendThenOldFailureWithoutRoleGuard.cfg`
- Expected result observed: invariant violation, exit code 12 (TLC's invariant-failure exit).
- Violated invariant: `OldFailureNotAppliedAfterDemotion`.
- State space at counterexample: 4 generated, 4 distinct; trace depth 3.
- Trace:
  - `WitnessV2AppendEntriesDeliverSameTermAppendThenOldFailureWithoutRoleGuard_TTrace_1794572686.tla`
  - `WitnessV2AppendEntriesDeliverSameTermAppendThenOldFailureWithoutRoleGuard_TTrace_1794572686.bin`

Counterexample:
- State 1: A is Leader, term 3; old failure (F→A, term 3, requestId 4) and same-term AppendEntries (B→A, term 3) are queued; nextIndex is 6.
- State 2: Delivering B's AppendEntries demotes A to Follower. The old failure remains queued; nextIndex remains 6.
- State 3: The mutant accepts the old failure despite A being a Follower. It decrements nextIndex 6→5 and sets `acceptedOldFailure` to TRUE.

The mutant retains the phase constraint and term equality, as well as requestId equality. Only the role precondition is removed.

## SHA-256

| Artifact | SHA-256 |
|---|---|
| Positive model | `BA4F7C98492EC3738BCD95D94FE5854F2F8A4AD9DAE375218D86657D340B21C4` |
| Positive config | `C24BC94F3223DC0D325348D257D476DDEE2A2A67637A04894FD3E57F0C7F9208` |
| Mutant model | `D042545DFF31C0E87BE0C07F396AFA0B3E0D05D280FABB860B771077B1B7DE0F` |
| Mutant config | `C24BC94F3223DC0D325348D257D476DDEE2A2A67637A04894FD3E57F0C7F9208` |
| Counterexample trace (.tla) | `1C5D50B349AE38089DE9704DC65147226DA14FCD48DF2FB59791EC7A830D27BF` |
| Counterexample trace (.bin) | `55E6B96693A5D019747757E82C1EC4FB2614562F721D40C2286DA9CBC58C2173` |

## Scope and limitations

This is a deliberately small abstract witness of the relevant `DeliverAppend` and `HandleAppendResponse` effects. It does not model full Raft, election safety, log replication, durable state, or production code. In particular, the same-term AppendEntries delivery is modeled only as the relevant role demotion and response enqueueing; this is not a claim that a same-term new leader was elected. The model is not evidence of production readiness.

## Change boundary

Only the four new model/config files and this report were created for this check, plus TLC's counterexample trace files. No source/base model, Production code, README, running service, tunnel, or archive was changed. No files were deleted, moved, committed, or reset.

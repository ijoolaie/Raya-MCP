# Nondeterministic AppendEntries delivery and response order — recheck (2026-11-13)

## New isolated model
- Model: `WitnessV2AppendEntriesNondeterministicDeliveryOrder.tla`
- Config: `WitnessV2AppendEntriesNondeterministicDeliveryOrder.cfg`
- The two same-anchor AppendEntries requests are initially both in the message set.
- TLC may choose which request the follower processes first. `DeliverAppend(req)` and `RejectAppend(req)` are enabled by the current follower state, not by a fixed stage sequence.
- TLC may independently choose the order in which the resulting success/failure responses are handled. A stale failure is explicitly dropped; a current failure uses the experimental guarded cursor update.
- The model has a fixed leader/follower, one term, fixed two requests, and no elections or crash actions.

## TLC run
Environment: TLC `2026.11.06.222357` (rev `d8fb231`), Oracle JDK `25.0.2`, one worker, copied JAR on D:.

Command:

```powershell
java -cp 'D:\Work\project\Raya-MCP\TLAPlus\tla2tools.jar' tlc2.TLC -workers 1 -metadir 'validation\nondeterministic-delivery-order-20261113-retry' -config WitnessV2AppendEntriesNondeterministicDeliveryOrder.cfg WitnessV2AppendEntriesNondeterministicDeliveryOrder.tla
```

Result: exit `0`; no error found; 17 states generated, 13 distinct, 0 queued; complete graph depth 5; maximum outdegree 2.

All 14 configured invariants passed: `TypeOK`, `CommittedPrefixPreserved`, `CursorLowerBound`, `MatchWithinLeaderLog`, `NextWithinLeaderLog`, `FollowerCommitBound`, `OutcomeSetsDisjoint`, `AtMostOneAcceptance`, `AtMostOneRejection`, `ExactlyOneAccepted`, `ExactlyOneRejected`, `FollowerStateAfterAcceptance`, `CommitAfterAcceptance`, `AllResponsesHandled`.

## Interpretation
The finite state exploration permits either request to be delivered first and independently permits response handling order. Exactly one request is accepted and the other is rejected by the committed-prefix guard; after all responses are handled the leader cursor reaches `matchIndex + 1` (6). The successful bounded run is evidence for this model only; it is not a general Raft proof and does not validate arbitrary terms, node counts, request contents, retry policies, elections, liveness, persistence, crash recovery or Production behavior.

## Authoring note
The first TLC invocation rejected a UTF-8 BOM in the new `.cfg` file (exit `151`) before parsing the model. Both new source files were rewritten as UTF-8 without BOM, then the run above passed. The first empty diagnostic metadir was retained; nothing was deleted.

## Preservation
Only the new experimental model/config and validation artifacts in the D: working copy were created/updated. The base model and original C: project were not changed. The TLC archive was not copied, moved or deleted. No commit/reset, service change or tunnel change was performed. No TLC/Java process remained after the run.
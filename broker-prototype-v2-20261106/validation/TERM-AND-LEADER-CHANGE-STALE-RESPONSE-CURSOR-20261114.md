# Term and leader change: stale AppendEntries response isolation (2026-11-14)

## Scope
- Isolated finite TLA+ model in the working copy: D:\Work\project\Raya-MCP\broker-prototype-v2-20261106.
- Initial leader A, term 1, peer B; pending success and failure responses carry term 1, destination A, source B, request ID 1.
- Model changes context to term 2 and leader C, retains peer B, resets the request counter, and reuses request ID 1 for new responses.
- The new leader's cursor is reinitialized in this abstraction (nextIndex=3, matchIndex=0).
- Positive response guards require matching term, destination leader identity, and source peer identity. Failure responses additionally require the current request ID and an unhandled composite key.
- No base model, source project, or Production changes.

## Positive run (context guards enabled)
- TLC exit 0; no error found.
- 111 states generated, 40 distinct, 0 queued; complete graph depth 7.
- Invariants passed: TypeOK, CursorLowerBound, NoOldLeaderResponseAccepted.
- Temporal properties passed: EventuallyNewLeaderFailureHandled and EventuallyPendingDrained (80 states checked for temporal branches).
- Old-term responses for leader A are discarded after the transition to term 2 / leader C. New-term responses for leader C are processed despite reusing request ID 1.
- CursorLowerBound remains true while the new leader's success and failure responses are interleaved.

## Mutation control (context guards disabled)
- Expected invariant violation observed; TLC exit 1.
- 6 states generated, 6 distinct, 3 queued at counterexample discovery; depth 3.
- Trace pair: WitnessV2TermAndLeaderChangeCursor_TTrace_1794642214.tla/.bin.
- Counterexample: after term changes to 2 and leader changes from A to C, an old success from term 1 addressed to A is accepted. It reports matchedThrough=4 and raises the new leader's matchIndex from 0 to 4. The ghost monitor NoOldLeaderResponseAccepted then fails.
- This demonstrates that request ID reuse makes term and leader/destination context guards independently important; request ID equality alone cannot identify the response's owner context.

## Interpretation and limits
- The tested safety condition is that an old leader's response cannot mutate the new leader's cursor after a term/leader transition.
- The composite identity is represented by (term, leader destination, peer source, request ID). The positive model enforces context guards before either success or failure handling.
- This model deliberately abstracts the leader transition and cursor reinitialization; it does not implement or validate an election protocol, quorum election, real role transitions, log replication, or production state transfer.
- One peer is retained across the leader change to isolate stale-response context. No claim is made about every multi-peer topology.
- Bounded model evidence only; not a full Raft proof, crash-persistence validation, or Production acceptance. No experimental clamp/dedup logic was integrated into the base model.

## Artifacts
- WitnessV2TermAndLeaderChangeCursor.tla (4,491 bytes)
- WitnessV2TermAndLeaderChangeCursor.cfg (221 bytes)
- WitnessV2TermAndLeaderChangeCursor-WithoutContextGuards.cfg (148 bytes)
- Mutation trace pair: WitnessV2TermAndLeaderChangeCursor_TTrace_1794642214.tla/.bin
- This report

## Cleanup and safety
- One obsolete diagnostic trace from the initial model-construction error was removed: WitnessV2TermAndLeaderChangeCursor_TTrace_1794642088.tla (8,812 bytes). It was not a valid counterexample from a completed model check.
- Final mutant trace retained as evidence.
- Root temporary *.tmp.cfg count: 0; active TLC Java process count: 0.
- No source project or Production files changed; no commit or reset performed.

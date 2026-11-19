# Multi-inflight responses across a term change (2026-11-14)

## Scope
- Isolated finite TLA+ experiment in working copy: D:\Work\project\Raya-MCP\broker-prototype-v2-20261106.
- Two followers (B, D), each with responses for in-flight request ID 1 in both term 1 and term 2.
- The abstract leader identity remains A across the term transition so the reused request ID and destination do not independently reject old responses; the term guard is isolated.
- For each peer, pending messages include old-term success/failure and current-term success/failure. Current-term transition resets both peer cursors; response processing and discarding are nondeterministic.
- Success updates are monotonic per peer. Failure acceptance checks request ID and composite dedup key (term, leader, peer, requestId), and clamps only that peer's nextIndex.
- No base model, source project, or Production changes.

## Positive run (term guard enabled)
- TLC exit 0; no error found.
- 2,161 states generated, 592 distinct, 0 queued; complete graph depth 10.
- Temporal properties checked over 1,184 states (two temporal branches).
- Invariants passed: TypeOK, CursorLowerBound for both peers, NoOldTermAcceptance.
- Temporal properties passed: EventuallyBothCurrentFailuresHandled and EventuallyAllResponsesDrained.
- Old-term responses from both followers are discarded even though destination, source peer, and reused request ID match the new-term context. Current-term responses can update their own peer cursors.

## Mutation control (term guard disabled)
- Expected invariant violation observed; TLC exit 1.
- 7 states generated, 7 distinct, 4 queued at counterexample discovery; depth 3.
- Trace pair: WitnessV2MultiInflightTermChangeIsolation_TTrace_1794642875.tla/.bin.
- Counterexample: after term advances to 2 and both cursors reset, an old term-1 success response from B is accepted because leader A, peer B, and request ID 1 all still match. It raises matchIndex[B] from 0 to 4, setting NoOldTermAcceptance.
- This isolates the term check: matching leader, peer, and request ID do not make a response from an earlier term safe.

## Interpretation and limits
- The experiment covers two peers and a finite set of old/current responses; it exercises arbitrary processing order across a term transition.
- Leader identity deliberately remains A to prevent destination guard from masking the term guard. This is a term-change isolation test, not a leader-election model.
- The model abstracts actual elections, transport, retry generation, follower log state, commit/quorum, persistence, and implementation scheduling.
- Bounded model evidence only; not a full Raft proof, crash-persistence validation, or Production acceptance. No experimental logic was integrated into the base model.

## Artifacts
- WitnessV2MultiInflightTermChangeIsolation.tla (4,431 bytes)
- WitnessV2MultiInflightTermChangeIsolation.cfg (217 bytes)
- WitnessV2MultiInflightTermChangeIsolation-WithoutTermGuard.cfg (136 bytes)
- Mutation trace pair: WitnessV2MultiInflightTermChangeIsolation_TTrace_1794642875.tla/.bin
- This report

## Cleanup and safety
- No test-generated files were deleted; the final mutation trace is retained.
- Root temporary *.tmp.cfg count: 0; active TLC Java process count: 0.
- No source project or Production files changed; no commit or reset performed.

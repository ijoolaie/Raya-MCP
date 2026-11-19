# DeliverAppend Acceptance and Delayed Success Integration Check — 2026-11-14

## Question
Can a delayed success response be tied to an actual AppendEntries acceptance, while a later AppendEntries rejection retreats the leader cursor? Does the reported matchIndex remain within the follower's currently retained log prefix?

## Model scope
`WitnessV2DeliverAppendDelayedSuccessIntegration.tla` is a deliberately small, test-only transition slice based on the inspected `WitnessV2AppendEntriesIncremental.tla` definitions. It reproduces the relevant semantics:
- An APPEND request carries prevIndex/prevTerm/entries and requestId.
- Delivery checks the previous-log term, applies the entries to the follower log, and creates a success response whose matchIndex is prevIndex + Len(entries), as in `DeliverAppend`.
- A later APPEND with prevIndex beyond the follower's log is rejected and creates a failure response, as in `RejectAppend`.
- The current failure retreats nextIndex with the matchIndex+1 lower-bound clamp; the delayed old success is then handled monotonically using max semantics, as in the focused candidate behavior tested earlier.

This is a focused executable slice, not a direct composition of the full base module. It does not model online membership, terms/elections, quorum/commit, multiple peers, persistence, or the complete message schema.

## Stable follower-prefix run
Config: `WitnessV2DeliverAppendDelayedSuccessIntegration-Stable.cfg`
- TLC PASS, exit 0.
- 8 states generated, 7 distinct; 0 queued; complete graph depth 7.
- `TypeOK`, `CursorLowerBound`, `MatchWithinFollowerLog`, `NoBadAckMonitor`, and `EventuallyOldSuccessHandled` passed.
- Trace semantics: request 1 is delivered and accepted at follower index 3; its success response remains delayed. Request 2 probes prevIndex 4 while follower log length is 3 and is rejected. Handling request-2 failure retreats nextIndex 4 -> 3. Handling request-1 success then raises matchIndex 0 -> 3 and nextIndex 3 -> 4. The acknowledged prefix is present in the follower log.

## Follower-prefix rollback negative control
Config: `WitnessV2DeliverAppendDelayedSuccessIntegration-Rollback.cfg`
- Expected invariant violation, TLC exit 12.
- 10 states generated, 9 distinct; 0 queued; complete graph depth 8.
- Counterexample trace: `WitnessV2DeliverAppendDelayedSuccessIntegration_TTrace_1794646196.tla/.bin`.
- After request 1 was genuinely accepted and request 2 rejected/handled, the test-only rollback action truncates the follower log from length 3 to length 2. The delayed success for request 1 is then processed and raises matchIndex to 3. `MatchWithinFollowerLog` fails (3 > 2); the monitor also records the bad acknowledgement.
- This is an explicit durability-loss counterfactual. It demonstrates what the invariant catches; it is not evidence that the real follower loses acknowledged entries.

## Interpretation
The delayed success can be grounded in a real modeled AppendEntries acceptance rather than an invented response. Under a stable follower prefix, processing it after a newer rejection is safe in this scenario and preserves the cursor lower bound. The negative control shows that the relationship depends on the acknowledged prefix still existing. This does not justify dropping all older successes merely because a newer request failed.

## Artifacts
- `WitnessV2DeliverAppendDelayedSuccessIntegration.tla`
- `WitnessV2DeliverAppendDelayedSuccessIntegration-Stable.cfg`
- `WitnessV2DeliverAppendDelayedSuccessIntegration-Rollback.cfg`
- `WitnessV2DeliverAppendDelayedSuccessIntegration_TTrace_1794646196.tla/.bin`
- this report: `validation/DELIVERAPPEND-ACCEPTANCE-DELAYED-SUCCESS-INTEGRATION-20261114.md`

## Cleanup and change boundary
Two superseded deadlock-diagnostic trace pairs from the first test iteration were removed after the terminal-state stutter action fixed the harness:
- `..._TTrace_1794646127.tla/.bin`: 8,838 bytes
- `..._TTrace_1794646128.tla/.bin`: 8,837 bytes
- Total removed: 17,675 bytes across 4 files.

The final rollback counterexample trace is retained. Root temporary `*.tmp.cfg` count is 0 and no TLC Java process remains. Only the working copy was modified. No base model, source project, or Production files changed. No commits or resets.

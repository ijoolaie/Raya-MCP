

### Generated mixed success/failure response ordering (2026-11-12)

Added an independent bounded model:
- WitnessV2AppendEntriesGeneratedMixedResponses.tla
- WitnessV2AppendEntriesGeneratedMixedResponses.cfg
- WitnessV2AppendEntriesGeneratedMixedResponses-Diagnostic.cfg

This model generates two actual AppendEntries requests before either response is handled. It deliberately follows the base SendAppend cursor semantics: nextIndex is not changed on send, so both requests use the same prevIndex = 1, prevTerm = 1, and suffix <<1,2,3,4>>, but have monotonically increasing request IDs 1 and 2. This is not speculative pipelining with an advancing send cursor.

The follower initially has log <<1,9,9,8>> and commitIndex 1. Processing request 1:
- PrevMatches succeeds at index 1.
- AppendResult replaces the divergent uncommitted suffix and produces <<1,1,2,3,4>>.
- leaderCommit = 2 advances follower commitIndex to 2.
- The follower generates a success response for request 1 with matchIndex 5.

Processing request 2 afterward:
- Its previous-log anchor still matches.
- Its prevIndex = 1 is now below follower commitIndex 2.
- The base RejectAppend committed-prefix guard therefore rejects it and generates a failure response for request 2.
- Request 2 is the latest request ID, so its failure response is eligible for leader handling.

At the response stage, both generated response records are independently schedulable. The success handler follows the base monotonic matchIndex/nextIndex update. The failure handler is tested with the guarded candidate max(nextIndex - 1, matchIndex + 1).

Guarded TLC run (UseGuard = TRUE): exit code 0; 9 states generated; 8 distinct; 0 queued; complete depth 7; maximum outdegree 2. All ten configured invariants passed: TypeOK, CommittedPrefixPreserved, FollowerCommitBound, CursorLowerBound, MatchWithinLeaderLog, NextWithinLeaderLog, RequestsUseSameBaseCursor, FollowerAcceptedFirst, FollowerCommitAdvanced, BothResponsesGenerated, and AllResponsesHandled. The two enabled response handlers are both explored, covering success-then-failure and failure-then-success; the terminal states merge.

Unguarded diagnostic (UseGuard = FALSE): expected invariant counterexample, exit code 12. The first BFS witness handles the current failure before the older success: nextIndex 2 -> 1 while matchIndex remains 1, violating nextIndex >= matchIndex + 1. The other response order is present in the guarded state graph; if success is handled first, matchIndex/nextIndex advance to 5/6 and the unguarded current failure decrements nextIndex to 5, again violating the lower bound. The diagnostic is a bounded counterexample to unconditional decrement in this scenario, not proof that this candidate guard is the only valid design.

Alignment and limits:
- Request construction, previous-log matching, AppendResult, follower commit clamping, rejection by prevIndex < commitIndex, response request IDs, and success cursor advancement follow the corresponding base-model semantics for the fixed term/online/role scenario.
- The guarded failure formula is intentionally different from the current base model's unconditional decrement and remains isolated in this experiment.
- One fixed leader/follower pair, one term, one follower suffix repair, two same-anchor in-flight requests, and two response records only. No elections, higher-term transitions, arbitrary request delivery order, crash/restart, persistence, fairness/liveness, or Production integration.
- The second request is rejected specifically because the first request advances the follower commit index; this tests the interaction between generated success and generated current failure, not an arbitrary network failure.

No base model, Production code, README, services, or IranPriceScout tunnel was changed. No commit/reset was performed. Production remains NO-GO.

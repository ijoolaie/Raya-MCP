# Stale-success cursor over-advance and follower-prefix stability check (2026-11-14)

## Scope
- Working copy only: \`D:\Work\project\Raya-MCP\broker-prototype-v2-20261106\`.
- No changes to the base incremental model, source project, or Production.
- TLC 2026.11.06.222357 (revision d8fb231), Java 25.0.2, one worker, 512 MiB heap.
- Question: can accepting an old success response advance \`nextIndex\` beyond what the follower can actually satisfy on the next AppendEntries attempt?

## Abstract scenario
- Initial cursor: \`nextIndex=4\`, \`matchIndex=0\`.
- Delayed success from request 1 reports \`matchedThrough=4\`.
- Existing success handling preserves monotonicity and sets \`matchIndex=4\`, \`nextIndex=5\`.
- The next AppendEntries attempt uses previous index \`nextIndex-1=4\`.
- The model then compares a stable follower prefix (peerMatch remains 4) with a counterfactual follower-log rollback (peerMatch falls from 4 to 2).
- \`RetryAppend\` is weakly fair; property: \`EventuallyRetrySucceeded\`.

## Results

### Stable follower prefix
- PASS, exit 0.
- 5 states generated, 4 distinct, 0 queued; complete graph depth 3.
- \`TypeOK\`, \`CursorLowerBound\`, and \`EventuallyRetrySucceeded\` passed.
- The delayed success advances the cursor to 5; the next attempt's prevIndex is 4, which the stable follower can satisfy.

### Counterfactual follower-log rollback
- Expected temporal-property violation, exit 13.
- 14 states generated, 10 distinct, 0 queued; complete graph depth 4.
- Trace: \`WitnessV2StaleSuccessCursorOverAdvance_TTrace_1794640607.tla/.bin\`.
- Trace: delayed success raises \`matchIndex=4\` and \`nextIndex=5\`; peerMatch then regresses to 2; retry uses prevIndex 4 and fails; under the modeled transition set, retry does not progress, violating \`EventuallyRetrySucceeded\`.

## Interpretation and boundary
- Under the stable-prefix assumption, the delayed success does not over-advance the cursor: it records a prefix the follower still has, and the next AppendEntries prevIndex is satisfiable.
- The counterexample requires the follower's acknowledged prefix to regress after success. That is a durability/crash-loss counterfactual, not a demonstrated behavior of the current implementation or standard Raft's durable-log assumptions.
- This experiment therefore identifies an assumption the cursor reasoning depends on: success evidence must remain valid for the same leader/term and follower log prefix. It does not justify adding request-ID filtering to success responses by itself.
- This is a small synthetic bounded model, not full Raft proof, crash-persistence validation, or Production validation.
- Cursor clamp and composite dedup remain experimental candidates only.

## Artifacts
- \`WitnessV2StaleSuccessCursorOverAdvance.tla\`
- \`WitnessV2StaleSuccessCursorOverAdvance-StablePeer.cfg\`
- \`WitnessV2StaleSuccessCursorOverAdvance-PeerRollback.cfg\`
- Counterexample trace pair above
- This report

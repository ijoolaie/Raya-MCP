# Failure-response deduplication identity-scope tests (2026-11-14)

## Scope
- Working copy only: `D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`.
- Focused finite TLA+ experiment; no edits to the base incremental model, source project, or Production.
- TLC `2026.11.06.222357` (revision `d8fb231`), Java `25.0.2`, one worker, 512 MiB heap.
- `Term=1` and `RequestId=1` are held constant while the peer or leader identity changes.
- Duplicate deliveries are represented as distinct records with `copyId`, since TLC messages are stored in a set.

## Model
The candidate key is:
`<<term, leader identity (dst), peer identity (src), requestId>>`.

The model first accepts one failure response and records its key. An identical pending delivery is then discarded. The identity changes, the request ID is reset and reused as 1, and a response for the new identity is introduced. The temporal property requires that this new key eventually be recorded. A cursor lower-bound invariant is also checked.

## Peer identity scenario
- Initial identity: leader A, peer B.
- New identity: leader A, peer C.
- Positive configuration: `IncludeLeader=TRUE`, `IncludePeer=TRUE`.
- TLC exit 0; PASS.
- 20 states generated, 13 distinct, 0 queued; complete graph depth 6.
- `TypeOK`, `CursorLowerBound`, and `EventuallyNewIdentityFailureHandled` passed.

Mutation: omit the peer field from the dedup key (`IncludePeer=FALSE`).
- Expected temporal-property violation observed; TLC exit 13.
- The new peer-C response is discarded because it collides with the previously handled key whose peer field was omitted.
- Trace: `WitnessV2IdentityScopeFailureDedup_TTrace_1794635506.tla/.bin`.

## Leader identity scenario
- Initial identity: leader A, peer B.
- New identity: leader C, peer B.
- Positive configuration: `IncludeLeader=TRUE`, `IncludePeer=TRUE`.
- TLC exit 0; PASS.
- 20 states generated, 13 distinct, 0 queued; complete graph depth 6.
- `TypeOK`, `CursorLowerBound`, and `EventuallyNewIdentityFailureHandled` passed.

Mutation: omit the leader field from the dedup key (`IncludeLeader=FALSE`).
- Expected temporal-property violation observed; TLC exit 13.
- The new leader-C response is discarded because it collides with the previously handled key whose leader field was omitted.
- Trace: `WitnessV2IdentityScopeFailureDedup_TTrace_1794635509.tla/.bin`.

## Interpretation and limits
- The peer identity must distinguish response keys when request IDs can be reused across peer scopes.
- The leader identity must distinguish response keys when request IDs can be reused across leader scopes.
- The mutation controls show each identity component is independently material to this model's deduplication key.
- The leader-change scenario is a synthetic identity-scope stress test with a constant term. It is not a valid Raft leader-election transition and makes no claim about Raft protocol correctness.
- The model is bounded and abstract. It does not establish transport semantics, persistent deduplication, crash behavior, election safety, or full Raft correctness.
- Clamp and composite dedup remain experimental candidates. No integration into the base model or Production is authorized or performed.

## Artifacts
- `WitnessV2IdentityScopeFailureDedup.tla`
- `WitnessV2IdentityScope-Peer.cfg`
- `WitnessV2IdentityScope-Peer-WithoutPeerKey.cfg`
- `WitnessV2IdentityScope-Leader.cfg`
- `WitnessV2IdentityScope-Leader-WithoutLeaderKey.cfg`
- Mutation traces listed above
- This report

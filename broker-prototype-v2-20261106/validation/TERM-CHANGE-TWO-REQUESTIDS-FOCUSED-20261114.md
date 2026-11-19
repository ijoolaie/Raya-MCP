# Focused Term Change with Two Request IDs — 2026-11-14

## Objective

Isolate the interaction between a term transition and reuse of multiple AppendEntries request IDs, without the state-space cost of the full multi-node model. This is a test-only model in the working copy.

## Model scope

- One fixed leader (A) and one fixed follower peer (B).
- Terms 1 and 2.
- Request IDs 1 and 2 are both reused in term 2.
- Old-term success responses for both IDs remain delayed across the term transition.
- New-term success and failure responses for IDs 1 and 2 coexist and may be processed in arbitrary order.
- Success cursor updates are monotonic; failure handling checks the latest request ID, uses a composite deduplication key (term, leader, peer, requestId), and clamps nextIndex to matchIndex+1.
- The term transition resets matchIndex and makes the new-term responses pending. Leader and peer identities are deliberately unchanged so they cannot mask the term guard.

The model is synthetic and focused on response acceptance/order. It does not model actual AppendEntries delivery, follower log contents, elections, quorum/commit, transport, or persistence.

## Positive run

Config: `WitnessV2TermChangeTwoRequestIdsFocused.cfg`

All four invariants and both temporal properties were active:
- TypeOK
- CursorLowerBound
- NoOldTermAcceptance
- NoStaleFailureAcceptance
- EventuallyCurrentFailureHandled
- EventuallyAllResponsesDrained

Result:
- TLC exit code 0 — **PASS**
- 201 states generated
- 68 distinct states
- 0 states left on queue
- Complete graph depth 8
- Both temporal branches checked successfully
- No invariant or temporal-property violation

## Mutation controls

### Term guard disabled

Config: `WitnessV2TermChangeTwoRequestIdsFocused-WithoutTermGuard.cfg`

- TLC exit code 12 — expected invariant violation.
- 5 states generated; 5 distinct; depth 3.
- Counterexample: after advancing to term 2, a delayed term-1 success response for request ID 1 is accepted because leader and peer still match. matchIndex rises to 4 after the term transition reset it to 0.
- Violated property: `NoOldTermAcceptance`.
- Retained trace: `WitnessV2TermChangeTwoRequestIdsFocused_TTrace_1794664314.tla/.bin`.

### Request-ID guard disabled

Config: `WitnessV2TermChangeTwoRequestIdsFocused-WithoutRequestIdGuard.cfg`

- TLC exit code 12 — expected invariant violation.
- 7 states generated; 7 distinct; depth 3.
- Counterexample: after transition to term 2, failure response requestId 1 is accepted although requestId 2 is latest. nextIndex decreases from 5 to 4.
- Violated property: `NoStaleFailureAcceptance`.
- Retained trace: `WitnessV2TermChangeTwoRequestIdsFocused_TTrace_1794664309.tla/.bin`.

Both mutation controls therefore detect their respective missing guard in this narrow combined scenario.

## Interpretation

This focused state space is tractable and directly exercises two request IDs across a term change. It supports the bounded conclusion that:
1. response-term validation is independently necessary even when leader and peer identity remain unchanged and request IDs are reused;
2. latest-request-ID validation is independently necessary for failure responses in the new term;
3. the positive transition system preserves the cursor lower bound and drains the finite response set under the stated fairness condition.

This does not resolve the full-model MaxTerm=2 / MaxRequestId=2 timeout. It also does not prove that the abstract response rules are sufficient for a real Raft implementation.

## Artifacts

- `WitnessV2TermChangeTwoRequestIdsFocused.tla`
- `WitnessV2TermChangeTwoRequestIdsFocused.cfg`
- `WitnessV2TermChangeTwoRequestIdsFocused-WithoutTermGuard.cfg`
- `WitnessV2TermChangeTwoRequestIdsFocused-WithoutRequestIdGuard.cfg`
- The two final mutation trace pairs listed above
- This report

## Cleanup, integrity, and boundaries

- Two superseded diagnostic trace pairs from earlier model iterations were deleted after the final scenario was corrected: 4 files, 18,979 bytes freed.
- The final term-guard and request-ID-guard counterexample trace pairs were retained.
- Zero TLC processes remained after the tests.
- Zero root-level `*.tmp.cfg` files were present.
- Base model SHA256 remains `C85CF1906D92DF31A62F8DA63392673809D3A314F30012242FB2B6E54B203A57`.
- No base model/config, source project, or Production files changed.
- No commits or resets.

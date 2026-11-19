# Exact duplicate AppendEntries responses — cursor check (2026-11-14)

## Scope
- Working copy only: `D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`.
- Focused finite TLA+ model of the response-handler cursor updates; no base model or Production changes.
- TLC `2026.11.06.222357` (revision `d8fb231`), Java `25.0.2`, one worker, 512 MiB heap.
- The model represents duplicate deliveries as separate records carrying a model-only `copyId`. Their protocol fields are otherwise identical. This avoids the base model's SET semantics collapsing identical records.

## Model
`WitnessV2ExactDuplicateResponseCursor.tla` begins with two copies of the same current-request failure (requestId 2), and two copies of the same older success (requestId 1, matchIndex 2). It explores every order in which these response copies can be handled. The request counter remains unchanged, matching the current handler's behavior. It checks `CursorLowerBound == nextIndex >= matchIndex + 1`.

## Baseline
Config: `WitnessV2ExactDuplicateResponseCursor-Baseline.cfg` (`ClampFailures=FALSE`).

TLC exit 12 — expected counterexample:
- 6 states generated, 6 distinct; depth 3 at violation.
- Initial cursor: `nextIndex=3`, `matchIndex=1`; two identical current failure responses are pending.
- First failure copy reduces `nextIndex` to 2.
- Second copy has the same still-current requestId and is accepted again; it reduces `nextIndex` to 1.
- `CursorLowerBound` is violated because 1 < 1+1.
- Trace: `WitnessV2ExactDuplicateResponseCursor_TTrace_1794634372.tla/.bin`.

## Clamped candidate
Config: `WitnessV2ExactDuplicateResponseCursor-Clamped.cfg` (`ClampFailures=TRUE`).

TLC exit 0 — no error found:
- 33 states generated, 16 distinct, 0 queued; complete graph depth 5.
- Both copies of each response can be handled in all reachable orders.
- `TypeOK` and `CursorLowerBound` passed.

## Interpretation and limitations
- The base model's set representation cannot express two identical simultaneous records. This focused model adds a non-protocol copy identity solely to represent duplicate deliveries.
- The duplicate failure is not idempotent in the baseline: because `lastRequestId` is not advanced when a response is handled, both copies pass the request-ID guard. The clamp preserves the cursor lower bound, but repeated failures can still move the cursor down to the `matchIndex+1` floor. This is a bounded safety result, not proof that duplicate responses have no behavioral effect.
- The model abstracts away elections, log replication, transport, persistence, crashes, and liveness. It is not a full Raft proof.
- No base model, Production code, source project, or existing evidence was modified.

## Artifacts
- `WitnessV2ExactDuplicateResponseCursor.tla`
- `WitnessV2ExactDuplicateResponseCursor-Baseline.cfg`
- `WitnessV2ExactDuplicateResponseCursor-Clamped.cfg`
- Baseline counterexample trace `WitnessV2ExactDuplicateResponseCursor_TTrace_1794634372.tla/.bin`
- This report

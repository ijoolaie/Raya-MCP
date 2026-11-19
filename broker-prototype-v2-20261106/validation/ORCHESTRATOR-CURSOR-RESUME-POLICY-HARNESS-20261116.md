# Orchestrator Cursor Resume Policy Harness — 2026-11-16

## Result
PASS — standalone policy harness only. It does not execute the real Adapter, Submitter, or Worker APIs and is not an integration pass.

## State machine exercised
- Cursor values: Adapter -> Submitter -> (Owned Worker | Done).
- On a component cleanup failure, return immediately and preserve the cursor at that component.
- A later teardown call resumes at that component; already successful components are not called or released again.
- Borrowed Worker is never stopped/released and teardown completes after Submitter.
- Restart is allowed only at Done and only after all orchestrator-owned resources are absent; borrowed Worker remains externally live.

## Failure matrix and evidence
- Both Owned and Borrowed modes: 100 iterations each (200 total).
- Adapter cleanup succeeds once, then Submitter cleanup fails twice before succeeding.
- Owned mode additionally has Worker cleanup fail twice before succeeding.
- Assertions verify cursor position, live-resource state, exact attempt/release counts, no repeat release of successful components, Borrowed Worker preservation, and restart eligibility.
- 4,000 assertions passed.
- MSVC x64 `/W4 /WX /TC` compile and execution succeeded.

## Important limitations
- The harness uses simulated component cleanup callbacks and an in-memory cursor. It does not link or call the actual component implementations.
- It does not validate concurrent teardown owners, API entrant drain, OS Handle failures, component lifecycle invariants, or actual startup/restart of the three components.
- Previous real-component tests cover only normal Owned/Borrowed lifecycle; previous Submitter prototypes cover only its local partial-destroy retry. Those separate results do not combine into end-to-end proof.
- Full three-component failure/retry integration remains OPEN and Phase 1 remains NOT ACCEPTED.

## Integrity
- No working-copy source/header/test/build files modified.
- The disposable harness and compiler outputs were inspected and removed.
- No Production change, Commit, or Reset.

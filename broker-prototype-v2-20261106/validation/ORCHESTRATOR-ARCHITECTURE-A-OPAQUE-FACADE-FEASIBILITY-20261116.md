# Architecture A Feasibility — Orchestrator-Only Public Facade — 2026-11-16

## Decision
Architecture A is feasible as a deliberate API-boundary refactor, but it is NOT a drop-in wrapper and is not currently implementable by simply hiding two headers. Keep it as the preferred direction only if the project accepts an internal/private component API layer and a stable external Orchestrator handle. No implementation change is authorized or made in this audit.

## Evidence from current Working Copy
- There is no checked-in `*orchestrator*.c` or `*orchestrator*.h` file at the project root.
- `integrated-submit-adapter-v2.c` directly calls Submitter start/admit/enqueue/abort/acknowledge and Worker admit/reserve/handoff. These calls are core internal composition, so the Orchestrator-only rule cannot mean removing component APIs from internal code.
- `managed-pipe-io-v2.c` also directly uses Worker admission/reservation/handoff functions. It is another internal integration client that must be moved behind the private boundary or deliberately retained as an internal friend.
- Public component structs are currently fully defined in headers. Adapter and tests directly access component state, including lifecycle/initialized/started/joined fields, handles, locks and retained structures. Replacing those public struct definitions with opaque incomplete types would require allocation/creation APIs or an explicit caller-provided storage ABI, and coordinated changes to callers/tests.
- Root-level tests call Submitter and Worker APIs directly across multiple independent test programs. They can remain internal white-box tests, but should not be mistaken for supported external API consumers.

## Architecture A boundary that can work
1. Introduce a stable, externally owned Orchestrator handle/control block. Its lifetime must be protected by the host/registry/reference mechanism before any component pointer is dereferenced; an embedded entrant count alone cannot protect stale raw pointers.
2. Expose only Orchestrator lifecycle and operation functions to external callers. Do not expose component pointers or component structs in the public facade.
3. Move current component headers to an internal API boundary (or split public opaque declarations from private definitions). Adapter and `managed-pipe-io` remain internal friends and continue to call internal component functions.
4. Keep direct component white-box tests as internal tests; add separate public-facade tests proving external callers cannot bypass the Orchestrator lifecycle contract.
5. Orchestrator owns one cleanup cursor and cleanup-owner token. It closes facade admission, drains facade callers, closes Adapter admission, drains component work, then performs Submitter and Owned Worker teardown. Borrowed Worker is never stopped or destroyed.
6. Component cleanup functions must support explicit retry for every partial state. Architecture A does not remove the known Submitter post-Join partial-Destroy blocker or Worker failed-start close-failure blocker.
7. Startup, restart, callback reentrancy and final free must be explicitly specified. External storage owner prevents new handle acquisitions before final reclamation.

## Main migration risks / non-goals
- Do not make internal functions `static` indiscriminately: Adapter and managed-pipe integration currently depend on them across translation units.
- Do not merely remove prototypes while retaining public struct layouts; that hides names but does not prevent direct state/handle manipulation by code including the headers.
- Do not use `memset`, `DeleteCriticalSection`, or direct handle closes from Orchestrator to bypass component-owned partial cleanup.
- Do not claim that the existing Adapter gate covers direct Submitter/Worker calls or protects storage after free.
- Do not break current tests without an inventory and deliberate conversion to internal tests or facade-level contract tests.

## Suggested implementation sequence (not executed)
A. Define the host-owned stable handle/reference contract and public Orchestrator API.
B. Split public opaque facade declarations from private component definitions/internal headers.
C. Move Adapter and `managed-pipe-io` dependencies to the private boundary; retain existing component unit tests as internal white-box tests.
D. Implement component cleanup retry transitions for failed Worker start and partial Submitter destroy, with explicit state predicates and single cleanup owner.
E. Implement Orchestrator admission/cleanup cursor and Owned/Borrowed policy.
F. Add public facade race/lifetime tests, internal component retry tests, and chained fault matrix.
G. Re-run existing regression suites only after compile dependencies are stable.

## Integrity / scope
- Read-only feasibility audit. No source/header/test/build/model/archive/Production files changed.
- No temporary test copy was created; no tests/builds were run because this question is an API-boundary feasibility assessment, not a behavior change.
- No processes terminated. No Commit or Reset.

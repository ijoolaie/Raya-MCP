# Retained Read-only Validator Feasibility - 2026-11-15

Status: design record with compile-time test-only snapshot and class-aware validator implementation; no Production behavior change.

## Inspected implementation

- `integrated-submit-adapter-v2.c/.h`
- `reaper-managed-worker-v2.c/.h`
- `integrated-submit-adapter-v2-api-test.c`
- `build-integrated-submit-adapter-v2-api-test.cmd`

## Findings that constrain a safe validator

1. `raya_adapter_envelope` is private to `integrated-submit-adapter-v2.c`; the API test sees only an incomplete forward declaration. The test cannot safely traverse adapter nodes, inspect envelope flags, construct corruption fixtures, or validate cross-list identity without a test-only seam inside that translation unit.
2. Worker list nodes are `raya_managed_operation` and `retained_next` is public in `reaper-managed-worker-v2.h`. Worker list and count are protected by `worker.lock`; Adapter list and count are protected by `adapter.lock`.
3. `retain_envelope` acquires/releases the Adapter lock, then separately acquires/releases the Worker lock. There is no atomic cross-list snapshot or declared global lock order. A validator that independently locks both may observe an intermediate state; holding both simultaneously risks introducing a new lock-order dependency with concurrent retain.
4. Dual-list membership is conditional, not universal. On first retention, Adapter membership is always inserted. Worker membership is inserted only if `!worker_owns_operation || worker_terminal_released`. A retained operation still owned by Worker is intentionally absent from Worker Retained list. Therefore `Adapter retained_count == Worker retained_count` is not a valid invariant.
5. `newly_retained` gates the Worker-list insertion. If a later call to `retain_envelope` occurs after the operation transitions to terminal-released, it will not add the Worker node because Adapter membership already exists. This is a state-transition edge that needs explicit contract/testing before any repair or reconciliation implementation; do not assume re-entry is a supported promotion mechanism.
6. Existing API tests assert selected expected states (including dual membership for post-terminal ACK/close failures, and reserved-slot/ownership state for uncertain I/O), but do not provide generic cycle detection, duplicate-node detection, exact traversal/count validation, or concurrency snapshot semantics.
7. Worker destroy checks `retained_count == 0 && retained_head == NULL`; Adapter destroy checks its own retained list/count and active count. Neither performs reconciliation.

## Safe next test architecture

Do not add a validator that reads live lists under independent locks. First add a private, test-only snapshot seam in `integrated-submit-adapter-v2.c`, compiled only for the dedicated API-test target. The seam should:

- require an explicit quiescence token/condition established by test after adapter admission is closed and both executor threads are joined;
- acquire only one list lock at a time and copy node identities plus scalar membership/state fields into caller-owned test buffers;
- never expose mutable envelope pointers for writes and never mutate links/counters/causes/ownership;
- report `BUSY` if the adapter/worker are not joined/quiescent or active/caller/queued/outstanding state is nonzero;
- validate Adapter chain cycles, duplicate envelope addresses and exact count;
- validate Worker chain cycles, duplicate operation addresses and exact count;
- cross-correlate copied identities using the documented retention class, not a blanket equality-of-counts rule.

However, the current Worker/Adapter failure paths can leave a Worker in DEGRADED and unjoinable through ordinary shutdown in some scenarios. For those cases, a joined/quiescent snapshot may be impossible. Do not force-stop or reset the Worker to enable a test. A future validator may need a lock-coordinated concurrent snapshot protocol, but that requires a separately reviewed lock-order design.

## Required contract correction before tests

Document the allowed retention classes:

- **Adapter-only / uncertain-I/O retention:** Adapter node exists; Worker may still own the operation in active registry/reservation state; Worker Retained node is not required.
- **Post-terminal retention:** Adapter node and Worker Retained node are expected when the retention path is entered after Worker terminal ownership has been released.
- **Partial initialization / pre-handoff retention:** Adapter node may exist without a Worker Retained node; validate the exact owner/finalizer and reservation state rather than assuming dual membership.

The `newly_retained` promotion edge described above must be either proven unreachable by call graph or fixed/tested separately. No automatic insertion, unlink, free, reset, or reconciliation is authorized by this feasibility step.

## Verification and workspace safety

- No source or test file modified.
- No build run. The standard API-test runner would delete all its listed `.obj` outputs during cleanup; `reaper-recovery-policy-v2.obj` already exists and was deliberately preserved. Therefore running that runner now would be unsafe.
- No files deleted or overwritten.
- No process terminated; no active compiler/linker/test/TLC process found in the inspection session.
- No commits or resets.

## Decision

A generic read-only validator cannot safely be added as a standalone external test with current encapsulation and concurrency semantics. Next work must first resolve the retention-class contract and the `newly_retained` promotion edge. Then add a compile-time test-only snapshot seam after defining a safe quiescence/snapshot protocol. Retained resources remain fail-closed.

Production remains NO-GO; Phase 1 remains NOT ACCEPTED.

## Call-graph follow-up (2026-11-15)

### All call sites

The complete source search found one definition and call sites only in `integrated-submit-adapter-v2.c`; there are no external callers because the function is `static`.

The call-site groups are:

1. `adapter_worker_release`: invoked by the Managed Worker only after its terminal path has released the integrated registration; sets `worker_terminal_released=TRUE` before calling `retain_envelope` when Submitter ACK fails.
2. `adapter_publish`: on `HANDOFF_NOT_TRANSFERRED`, attempts caller collection then retains. This path is not a successful worker handoff.
3. `adapter_not_issued`: retains on owner transition, caller-terminal, finalizer, unreserve, or finish failures. This is the Submitter's not-issued path; it is mutually exclusive with successful publish/worker terminal callback for the same request.
4. `adapter_submit_impl`: pre-handoff setup/admission/reservation/enqueue failure paths. These return immediately after retaining; they do not schedule a later release callback for that envelope.

### Handoff semantics

`raya_managed_worker_handoff` returns `NOT_TRANSFERRED` when `raya_reaper_integrated_handoff` fails. The integrated handoff changes reservation to HANDED_OFF only on success. On success, the Worker then changes owner state and signals wake; failures after integrated handoff return `TRANSFERRED_DEGRADED`, not `NOT_TRANSFERRED`.

`adapter_publish` sets `worker_owns_operation=TRUE` only after any result other than `NOT_TRANSFERRED`. Therefore the NOT_TRANSFERRED retention case is deliberately classified as not worker-owned, even though caller collection is attempted and can fail. It is conservatively added to Worker Retained list because `!worker_owns_operation` satisfies the insertion predicate.

### `newly_retained` promotion edge: conclusion

The previously noted promotion scenario is **not demonstrated as reachable in the current call graph**:

- pre-handoff and not-issued retention paths return from their request callback/submit path and do not later invoke `adapter_worker_release` for the same request;
- a successful handoff does not call `retain_envelope` in `adapter_publish`; its eventual terminal callback calls `adapter_worker_release` once;
- `adapter_worker_release` sets `worker_terminal_released` immediately before its first retention attempt on ACK failure;
- every call site that invokes `retain_envelope` returns from that path, and there is no second-stage retry/re-entry path in current source.

Thus the specific sequence “first retain while `worker_owns_operation=TRUE` and terminal not released, then a later second retain after terminal release” is not supported by the present call graph. The `newly_retained` gate remains a fragile latent assumption: a future retry/re-entry feature could make it observable. Do not change it without a state-machine-level test and explicit membership bit.

### Retention class refinement

The Worker Retained list is not simply a list of operations owned by the Worker. It also receives uncertain ownership cases when `worker_owns_operation` is false. Therefore classify by proven lifecycle state, not by list name:

- **Pre-handoff uncertain ownership:** Adapter list + Worker list, no successful handoff; caller collection/owner status may be uncertain.
- **Worker-owned pending I/O:** Adapter list only is permitted if a future path retains while Worker ownership remains active; preserve registry/reservation/pipe/OVERLAPPED/event.
- **Post-terminal bookkeeping failure:** Adapter list + Worker list; terminal release flag set, ACK or later bookkeeping failed.
- **Pre-handoff fully cleaned failures:** not retained; envelope freed.

The current call graph does not establish a general cross-list one-to-one invariant. The Validator must check per-class expectations and operation state.

### Decision update

The suspected `newly_retained` promotion bug is not confirmed; current call graph makes the triggering sequence unreachable. Keep it as a regression guard for future retry features, not as a present defect. The more immediate test seam requirement remains: encapsulated Adapter nodes and the absence of a safe cross-list snapshot protocol prevent an external generic validator. Next implementation should introduce a test-only, quiescence-gated snapshot seam after documenting the precise class predicates; no list mutation or cleanup.

## Test-only retained snapshot seam implementation (2026-11-15)

Implemented a compile-time gated diagnostic seam under `RAYA_ADAPTER_TESTING` only. Production builds without that define do not expose or compile the snapshot function.

The seam requires:
- adapter admission permanently closed and entrant count zero;
- Submitter joined with a null thread handle;
- Worker joined with a null thread handle.

It locks and traverses the Adapter and Worker retained lists separately, detects cycles using Floyd's algorithm, verifies each list count against its stored count, and copies only opaque numeric identities plus Adapter envelope lifecycle flags into caller-owned arrays. It does not expose mutable pointers, mutate lists, free envelopes, release handles, or change recovery state. Buffer insufficiency or list inconsistency returns -1; lack of quiescence returns 0.

The API test's finalizer-close-failure case now joins both executor threads and takes a snapshot of the retained pair. It verifies one Adapter node and one Worker node refer to the same operation, and checks retained/admitted/finished flags while terminal-release remains false. The retained resources are intentionally left intact.

### Validation

- x64 compile with `/W4 /WX /TC`, `RAYA_ADAPTER_TESTING`: PASS.
- Snapshot-enabled API suite: 733 assertions across 66 cases per run; 10/10 runs PASS = 7,330 assertions.
- Baseline compile without `RAYA_ADAPTER_TESTING`: PASS.
- Baseline API suite: 724 assertions across 66 cases, PASS.
- All compiler outputs and executables were isolated under three uniquely named temporary directories and those exact directories were removed after validation. The pre-existing project `reaper-recovery-policy-v2.obj` was not touched.
- No TLC, Production, or source-project build was run.

### Scope and remaining limits

This is a test-only snapshot, not a recovery/reconciliation implementation. It validates per-list acyclicity/counts and reports identity overlap for test assertions; it does not yet provide a full class-aware cross-list validator for arbitrary retained populations, nor does it establish a production recovery path. It must not be used to authorize freeing or replaying retained resources.

## Class-aware read-only retained validator (2026-11-16)

Implemented a pure validator in the API test translation unit, guarded by `RAYA_ADAPTER_TESTING`. It consumes only copied snapshot records and never accesses or mutates live linked-list nodes.

### Classification and checks

- **Pre-handoff uncertain ownership** (`retained`, not `worker-owned`): requires exactly one matching Worker retained-list identity.
- **Worker-owned pending I/O** (`retained`, `worker-owned`, not terminal-released): Adapter-only membership is expected; Worker-list membership is rejected. This is a forward-compatible class predicate, not a claim that a current production call site exercises it.
- **Post-terminal bookkeeping failure** (`retained`, `worker-owned`, terminal-released): requires Worker admission, Worker finish, and exactly one matching Worker retained-list identity.
- All Adapter records require nonzero unique envelope and operation identities, retained bit, and known flags. Worker-finished requires admission; worker-owned requires admission; terminal-released requires worker-owned + admitted + finished.
- Worker records require nonzero unique operation identities and exactly one matching Adapter record. Orphans and duplicates are rejected.
- No global Adapter-count == Worker-count invariant is imposed.

### Positive and negative controls

- Positive synthetic fixtures: post-terminal dual membership, pre-handoff dual membership, and Worker-owned pending-I/O Adapter-only membership.
- Negative fixtures: duplicate Adapter envelope ID, duplicate Adapter operation ID, duplicate Worker operation ID, orphan Worker node, missing required Worker membership, contradictory terminal-release flags, finished-without-admission, zero identity, unknown flag bit, and unexpected Worker membership for Adapter-only pending I/O.
- Integrated positive fixture: the actual finalizer-close-failure retained snapshot is accepted by the pure validator.
- Snapshot API controls: a live/non-quiescent snapshot returns `0`; an undersized Adapter buffer returns `-1`.

### Validation

- Snapshot-enabled x64 build: `/W4 /WX /TC`, `RAYA_ADAPTER_TESTING`; PASS.
- Snapshot-enabled API suite before the post-terminal fixture extension: 768 assertions across 67 cases per run; 10/10 PASS = 7,680 assertions.
- Baseline build without `RAYA_ADAPTER_TESTING`: compile PASS; 724 assertions across 66 cases; 10/10 PASS = 7,240 assertions.
- Both variants were built and run in separate unique temporary directories. The test-enabled and baseline build artifacts were removed with their respective directories. No project `.obj` file was touched.
- No Production build, TLC run, source-project edit, commit, or reset. No retained resource was released or modified.

### Limits

This validator checks a quiescent copied diagnostic snapshot only. The test-only seam now also copies Owner state/counters, Finalizer state/counters, integrated operation and reservation state, per-operation Registry match count, Worker lifecycle/reservation/outstanding/queue/caller/registry/degradation counters, and Submitter request state/lifecycle/issuer/active/queue counters. It acquires Adapter, Worker, and Submitter locks one at a time; it does not hold multiple component locks simultaneously. The finalizer-close-failure fixture confirms: Owner `CALLER_TERMINAL`; Finalizer `CLOSE_UNCERTAIN` with one close attempt, zero successful closes and zero release commits; Reservation `HELD`; no Registry match; Worker reserved slots 1 with outstanding/queued/caller-active/registry counts all zero; Submitter request `NOT_ISSUED`, lifecycle `STOPPED`, and issuer/active/queue counts zero. Callback remains suppressed. This proves the retained record is a pre-handoff terminal bookkeeping/resource-close failure, not an in-flight Worker-owned I/O. The seam remains diagnostic: it does not inspect kernel I/O completion, repair or reconcile either list, prove callback disposition beyond the fixture, or authorize freeing/replaying retained resources. The Worker-owned pending-I/O class is still contract-checked synthetically and remains unobserved in current runtime paths. Production remains NO-GO; Phase 1 remains NOT ACCEPTED.

## Post-terminal ACK-failure snapshot integration (2026-11-16)

Extended the real ACK-signal-failure fixture to take the same quiescent test-only snapshot after both executor threads have exited and joined. This fixture represents a distinct class from the earlier pre-handoff finalizer-close failure:

- Owner state: RELEASED.
- Finalizer state: RELEASED; two close attempts, two successful closes, one release commit.
- Integrated reservation: RELEASED; Registry matches zero.
- Worker: one retained operation, but reserved slots, outstanding, queued, caller-active, and Registry counts are all zero.
- Worker degradation cause includes OWNERSHIP_INVARIANT.
- Submitter request state: ACKED, despite notification signaling failure; Submitter lifecycle remains DEGRADED; issuer leases, active issue, active producers, and queue count are zero.
- Callback remains suppressed and the envelope remains retained in both lists.

The fixture asserts all these scalar relationships and confirms Adapter/Worker retained identities match. It deliberately does not destroy or release either retained record. This demonstrates why ACK state and notification/lifecycle state must be tracked independently: ACK is already committed and must not be repeated merely because signaling failed.

### Validation after extension

- Test-enabled x64 build: /W4 /WX /TC, RAYA_ADAPTER_TESTING; PASS.
- Test-enabled API suite: 816 assertions across 67 cases; 10/10 PASS = 8,160 assertions.
- Baseline x64 build without RAYA_ADAPTER_TESTING: PASS.
- Baseline API suite: 724 assertions across 66 cases; 10/10 PASS = 7,240 assertions.
- Test-enabled outputs: 15 files, 383,425 bytes; removed with the unique temporary build directory.
- Baseline outputs: 15 files, 343,181 bytes; removed with its unique temporary build directory.
- An older test-only temporary directory containing 18 files / 352,666 bytes was inspected (its log showed the previous 733-assertion test) and removed. No project evidence or .obj files were touched.
- Final temporary-directory audit: zero RayaRetained* directories. No active compiler, linker, test, or TLC process.

The snapshot remains test-only and diagnostic. These two live fixtures cover pre-handoff close uncertainty and post-terminal ACK/notification failure; they do not establish a recovery algorithm, nor do they cover Worker-owned pending I/O in a live reachable path. Production remains NO-GO; Phase 1 remains NOT ACCEPTED.

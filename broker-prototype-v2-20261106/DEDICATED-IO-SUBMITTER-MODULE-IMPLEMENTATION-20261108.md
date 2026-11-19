# Dedicated I/O Submitter Module — Initial Core (2026-11-08)

## Scope and status

This is an initial lifecycle/queue core with callback seams. It is not yet wired to
ReadFile/WriteFile or the managed Completion Worker. It does not authorize broker
or Production integration. Production remains **NO-GO**.

Files:
- `dedicated-io-submitter-v2.h`
- `dedicated-io-submitter-v2.c`
- `dedicated-io-submitter-v2-native-test.c`
- `build-dedicated-io-submitter-v2-native-test.cmd`
- `dedicated-io-submitter-v2-backpressure-test.c`
- `build-dedicated-io-submitter-v2-backpressure-test.cmd`

## Implemented state and ownership rules

- One lifecycle-managed Submitter thread per instance.
- Finite caller-selected queue capacity (1..4096).
- Intrusive caller-provided request records; request storage must remain valid through
  terminal acknowledgement. The module never allocates or frees request/operation data.
- Admission lease acquired only in RUNNING. A pre-admitted request may enqueue while
  STOPPING; new admissions are rejected.
- Enqueue at capacity returns BACKPRESSURE and leaves the admission lease with caller;
  caller must retry or call abort_admission exactly once.
- Queue publication and wake SetEvent occur under the same critical section.
- Submitter resets its manual-reset wake event and checks queue/exit predicates under
  that same lock before waiting.
- Exactly one issue callback runs at a time, outside the lock.
- Issuer lease increments before issue callback. A proven NOT_ACCEPTED result releases
  the lease and calls not_issued outside the lock. ACCEPTED retains the lease until the
  Completion Worker calls acknowledge. UNCERTAIN or failed publication is sticky
  DEGRADED and retains ownership.
- Worker acknowledgement may race the return from publish callback. It waits on the
  state-change event until the request is marked ISSUED; it then decrements the issuer
  lease exactly once and signals both state-change and Submitter wake events.
- Clean exit requires STOPPING, zero active producers, empty queue, zero active issue,
  zero issuer leases, and no degraded state.
- Shutdown is idempotent in STOPPING and STOPPED. Join and destroy are separate.
- Callbacks execute outside the Submitter lock.

## Native results

MSVC x64 `/W4 /WX`:

1. Single request, issue/publish, concurrent terminal acknowledgement, shutdown/join:
   18 assertions/process x25 = **450 passed**.
2. Capacity-1 backpressure, admitted-before-STOPPING publication, explicit abort of
   backpressured admission, proven-not-issued cleanup, accepted queued request and
   terminal acknowledgement:
   29 assertions/process x25 = **725 passed**.

Total for this initial module: **1,175 assertions passed**.

The first run exposed a real liveness bug: final issuer acknowledgement signaled only
the state-change event, leaving the Submitter asleep on its wake event. The ack path
was corrected to signal both events. Both suites then passed 25/25 runs.

The prior isolated protocol gates were also rerun:
- Lost-wake: 1,500 assertions, 100/100.
- Admission/STOPPING: 1,850 assertions, 50/50.

## Explicit limitations / next gates

- No actual ReadFile/WriteFile issue adapter exists yet.
- No adapter publishes into `raya_managed_worker_handoff`; Completion Worker integration
  and the two-phase terminal-hold/finalizer/release path remain outstanding.
- No injected CreateEvent/CreateThread/SetEvent/ResetEvent/Wait failure matrix.
- No concurrent multi-producer stress against this actual module.
- No deterministic test of acknowledgement racing inside the publish callback, though
  the acknowledgement wait protocol is implemented; add a dedicated barrier test.
- No join-timeout/later-join test, OS handle-count baseline, long soak, immediate-success
  I/O, peer disconnect, or cancellation integration.
- The callback contract requires publish to enqueue/handoff without synchronously waiting
  for terminal acknowledgement on the Submitter thread. The issue callback must return
  ACCEPTED only when the OS definitely accepted the request; ambiguous outcomes return
  UNCERTAIN and retain the lease.
- No service, protected root, tunnel, Production, or IranPriceScout changes.

## Validation hygiene

- `git diff --check`: exit 0 (only existing LF/CRLF warnings from unrelated working
  files).
- Generated .exe/.obj artifacts from both new build scripts were removed; explicit
  artifact check found none.
- No commit, reset, or cleanup of existing user changes.

## Additional concurrency gates (same day)

Added:
- `dedicated-io-submitter-v2-ack-race-test.c` and build script.
- `dedicated-io-submitter-v2-multiproducer-test.c` and build script.

Results, MSVC x64 `/W4 /WX`:
- Deterministic publish/ack race: publisher callback blocks after entering and before return; acknowledgement thread starts while request remains ISSUING, is verified blocked, then publication is released. 22 assertions/process x50 = **1,100 passed**.
- Actual module multi-producer: 8 producer threads x10 requests, one Submitter, one completion-ack thread, capacity 80, all producers complete, shutdown, 80/80 issue/publish/ack, clean STOPPED snapshot. 137 assertions/process x10 = **1,370 passed**.

Cumulative initial-module suites: 1,175 + 1,100 + 1,370 = **3,645 assertions passed**.
These remain callback-seam tests; they do not issue native overlapped pipe I/O and do not integrate the Managed Completion Worker.

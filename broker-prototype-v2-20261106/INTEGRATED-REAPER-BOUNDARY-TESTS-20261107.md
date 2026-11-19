# Integrated Reaper v2 Boundary Tests — 2026-11-07

## Scope
Test-only validation of `reaper-integrated-v2.c`. No changes to the legacy Worker, `pipe-io-v2.c`, production transport, tunnel, or IranPriceScout.

## Added tests
- `reaper-integrated-capacity-test.c`: fills all 256 reserved/queued/registered operation slots, rejects a 257th reservation, promotes all entries, claims exact operation/event pairs, completes all operations, and verifies shutdown eligibility.
- `reaper-integrated-uncertain-test.c`: injects an abstract UNCERTAIN collector result and verifies sticky DEGRADED state while Registry membership, outstanding count, reservation, operation state, and event ownership remain retained.

## Results
- Integrated baseline: 21 assertions, 0 failures.
- Capacity boundary: 2,315 assertions, 0 failures.
- Uncertain retention: 14 assertions, 0 failures.
- MSVC `/W4 /WX`; all builds succeeded.
- Combined command exit code: 0.

## Defect found and corrected
The first 256-entry run exposed an inverted promotion-capacity condition: after the first registration, the adapter incorrectly rejected subsequent promotions. The condition was corrected to ensure a reservation exists for the next registry insertion. The full-capacity run then passed.

Terminal collection was also hardened to validate the exact operation/event pair, outstanding count, reservation count, and operation linkage before mutating either ownership ledger. Registry removal and Core terminal accounting remain a single lock-held transaction.

## Important limitations
- These are deterministic single-thread tests; they do not establish multi-producer concurrency safety.
- The external shared-lock contract is assumed by the test harness; no production synchronization adapter is connected.
- Collector outcomes are synthetic. No real `GetOverlappedResult`, cancellation, IOCP, or pipe handle lifecycle is tested here.
- No injected Registry corruption, event signaling failure, wait failure, allocation failure, or shutdown race is covered.
- UNCERTAIN intentionally retains resources; process teardown reclaims OS resources only because this is an isolated test process.
- The reservation API is a lock-held accounting primitive. Callers must only unreserve a reservation they personally acquired; it is not a tokenized lease API yet.

## Gate
This result is not approval to connect the integrated adapter to actual pipe I/O or Production. Next required work is shared-lock multithread stress and fault-injection around admission/reservation/handoff/promotion/terminal/shutdown, followed by real overlapped-I/O acceptance. Production remains NO-GO.

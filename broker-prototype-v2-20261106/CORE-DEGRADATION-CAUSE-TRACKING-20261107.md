# Core degradation-cause tracking — 2026-11-07

Status: prototype core change; not Production and not polling-recovery integration.

The core now stores a monotonic bitset of degradation causes. The cause-aware transition validates bits, ORs new causes into the existing set, and sets lifecycle DEGRADED. The legacy generic mark_degraded API conservatively records OWNERSHIP_INVARIANT, so old call sites fail closed rather than being implicitly treated as recoverable.

The sync adapter now records:
- SetEvent failures as WAKE_SIGNAL.
- WaitForSingleObject non-timeout failures as WAIT_FAILED.
- ResetEvent failures as WAITSET_INVALID.

All transitions happen under the existing external critical section in sync adapter. Core lock-held primitives still require caller serialization.

A focused test verifies cause accumulation, sticky blocking causes, unknown-bit rejection, and conservative legacy behavior. This does not enable DEGRADED promotion, does not implement a recovery worker, and does not prove all lifecycle call sites are cause-classified. Existing integrated-core direct mark_degraded call sites intentionally default to OWNERSHIP_INVARIANT pending more precise classification.

Production remains NO-GO; 28-group runtime acceptance gate remains.

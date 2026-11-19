# Adapter / Submitter Startup Rollback Integration — 2026-11-16

## Read-only source findings

`raya_integrated_adapter_start_with_apis` validates the Worker and inputs, zeroes the Adapter, initializes its critical section, copies the adapter API, stores borrowed component pointers, and marks the Adapter initialized before calling `raya_submitter_start_with_api`.

If Submitter start returns false, the Adapter path unconditionally sets `a->initialized = FALSE`, deletes the Adapter critical section, zeroes the Adapter, and returns. It does not inspect whether the Submitter start failure left `s->initialized == TRUE`, `s->lifecycle == DEGRADED`, or non-NULL handles. It does not invoke a failed-start cleanup operation or preserve an Adapter-level cleanup record.

The existing Submitter startup implementation has two partial-cleanup branches:
- second Event creation failure with failure to close the first Event: leaves Submitter initialized/DEGRADED with the first Event handle retained;
- thread creation failure with failure to close one or both Events: leaves Submitter initialized/DEGRADED with remaining handles retained.

The Managed Worker startup path has a related but distinct issue: if thread creation fails and `CloseHandle(wake_event)` also fails, it leaves the Worker initialized and degraded, with a retained wake handle. The Adapter borrows the already-started Worker and does not own that startup rollback.

## Reproduction

A temporary copy of the project was built with VS 2026 Build Tools 18.7.3 x64, `/W4 /WX /TC`. The test started a real Managed Worker, injected failure on Submitter's second Event creation and on closing the first Event, then called the actual `raya_integrated_adapter_start_with_apis`.

25/25 executions passed the reproduction assertions (175 total):
- Adapter start returned failure;
- Adapter was zeroed/uninitialized;
- Submitter remained initialized and DEGRADED;
- Submitter had no thread and retained a non-NULL wake Event;
- normal Submitter shutdown/destroy could not clean this pre-thread failed-start state;
- the retained handle was still a valid live handle.

The harness manually closed the retained handle and deleted the Submitter critical section solely to keep each test process leak-free. That manual cleanup is test cleanup, not an available project API.

## Ownership / lifecycle conclusion

The Adapter currently starts a Submitter but treats it as a borrowed component in its public lifetime comment. On failed Submitter initialization, responsibility for residual Submitter resources is not represented in the Adapter result or a cleanup handle. The caller can inspect the Submitter struct, but there is no public failed-start cleanup API in the current project. Consequently the Adapter start failure can return with residual resources requiring private/manual cleanup.

Do not simply leave the Adapter initialized or call ordinary `raya_integrated_adapter_destroy`: neither action is currently specified as valid for a failed-start Submitter. A follow-up design should explicitly define who owns Submitter startup rollback, provide a narrow retryable cleanup contract, and ensure the Adapter does not erase the only recovery context before the caller can use it. Worker-start failure cleanup needs a separate contract because it owns a different wake Event and has no injected close API today.

## Scope and limits

- Temporary-copy test only; no project source/header/test/build changes.
- The test covered the second-Event-create + first-Event-close failure at the Adapter boundary. Submitter's independent matrix covers its own create-thread and partial-close failures; these were not separately repeated through Adapter startup.
- No Application Verifier/Page Heap or callback-reentrancy tests were run.
- This is a reproduced cleanup gap, not evidence of a production crash or data corruption.

Decision: keep this as a Stage 5 blocker. No source integration or Production acceptance is justified yet. Production remains NO-GO; Phase 1 remains NOT ACCEPTED.

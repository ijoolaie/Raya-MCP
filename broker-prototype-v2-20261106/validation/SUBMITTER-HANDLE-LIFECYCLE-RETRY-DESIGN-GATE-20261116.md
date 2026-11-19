# Submitter Handle Lifecycle and Retry Design Gate — 2026-11-16

## Scope
Read-only source review of dedicated-io-submitter-v2.c/.h in the Raya MCP working copy. No source/header/test/build changes were made.

## Environment
Working copy: D:\\Work\\project\\Raya-MCP\\broker-prototype-v2-20261106
Disk at review: C free 8.77 GiB; D free 1.97 GiB.

## Confirmed source findings

1. `degrade_locked` (C lines 30-34) unconditionally calls set_event(changed_event) and set_event(wake_event), regardless of whether either handle has already been closed or is NULL.
2. `raya_submitter_destroy` closes wake_event first and sets it NULL on success (lines 302-303). If closing changed_event then fails (line 304), it calls degrade_locked and returns. The object is now DEGRADED with wake_event NULL and changed_event still owned. The error path attempts to signal both, including the closed/NULL wake handle.
3. A later destroy attempt is rejected at line 297 because it requires lifecycle STOPPED. The failed close has transitioned lifecycle to DEGRADED, so there is no public retry path.
4. Startup cleanup after changed_event creation failure (lines 156-166) preserves wake_event if close fails and marks initialized/DEGRADED, but no public cleanup/retry operation exists for an initialized object with no worker thread.
5. Thread-creation failure cleanup (lines 171-182) similarly preserves any handle whose close failed, sets DEGRADED, and returns with initialized TRUE and started FALSE. Public destroy requires joined TRUE, while join requires started TRUE. This creates an unreachable public cleanup state.
6. On successful close the corresponding field is set NULL, which is correct ownership bookkeeping. A retry design must preserve that per-handle progress and must never close a NULL/already-closed handle again.

## Required design separation

Do not use the runtime DEGRADED lifecycle as the only cleanup state. Keep operational lifecycle and resource-cleanup progress separate.

Suggested per-resource states:
- UNCREATED: no handle was acquired.
- OWNED: valid handle is owned and may be used/closed.
- CLOSE_PENDING: close previously failed; handle remains owned but its validity may be uncertain depending on Win32/API contract and injected failure semantics.
- CLOSED: close succeeded; pointer is NULL and must never be passed to any API again.

For this project's injected close API, define its contract explicitly: a FALSE return must mean the handle remains owned and may be retried. If that cannot be guaranteed for arbitrary implementations, CLOSE_PENDING must remain quarantined and retry must be forbidden until validity is independently established. Never assume that every failed close leaves a valid handle.

## Proposed cleanup API contract (design only; not implemented)

A separate cleanup/release operation should:
- be callable for partial startup objects even when no thread was created;
- require no active producers, no active issue, no issuer leases, and either no thread or a joined thread;
- never reset operational state to RUNNING/STOPPED;
- close only resources in OWNED state;
- after successful close, immediately record CLOSED and NULL the field;
- preserve failed resource state and all other owned handles;
- return incomplete while any resource remains owned or uncertain;
- be retryable only when the close API guarantees failure preserves ownership;
- delete the critical section and clear initialized only after every owned handle is closed and no thread handle remains;
- be serialized against all other lifecycle methods by an explicit caller-side serialization contract or a dedicated lifecycle gate. Do not race cleanup with shutdown/join/destroy.

## Degrade signaling rule

`degrade_locked` must signal only resources explicitly marked OWNED and known usable. It must never call SetEvent on NULL, CLOSED, or CLOSE_PENDING resources. This is a necessary guard, not a complete recovery path. If the independent degraded event is unavailable, fail-closed state remains but prompt wake cannot be guaranteed.

## Required tests before implementation approval

1. Startup: create first event fails; create second event fails; close first succeeds/fails.
2. Startup: thread creation fails; close wake succeeds/fails; close changed succeeds/fails; cover each independent combination.
3. Destroy: wake close succeeds, changed close fails; verify wake is never signaled/closed again and retry only follows the documented close-failure ownership contract.
4. Destroy: wake close fails, changed close succeeds; verify only wake remains owned and retry is safe only under stated contract.
5. Both closes fail; repeated cleanup attempts; no double-close and no lost ownership.
6. SetEvent fault injection while one handle is NULL/CLOSED/CLOSE_PENDING; assert no call is made on it.
7. Concurrent cleanup vs shutdown/join/destroy is either prevented by contract or deterministically serialized.
8. Baseline and test-enabled API regressions.

## Decision
NO-GO for modifying production cleanup or claiming retry-safe recovery yet. First settle the close-handle failure contract for the API seam, define the partial-start cleanup API and serialization rule, then implement only in a temporary source copy and run the complete fault matrix. Keep the earlier degraded_event prototype test-only.

No commits/resets, no Production/source-project edits, no TLC, and no files deleted in this review.

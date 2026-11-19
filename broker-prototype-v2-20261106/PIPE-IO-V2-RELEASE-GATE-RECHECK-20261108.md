# pipe-io-v2 Release Gate Recheck (2026-11-08)

Status: BLOCKED — do not wire this transport into the lifecycle-managed Reaper or Production.

## Scope

This recheck ran the existing `reaper-source-audit-test.cjs` against the current prototype source. It did not modify `pipe-io-v2.c`, run a service, change tunnel state, or touch IranPriceScout.

## Observed result

The audit exited nonzero and reported 0/4 required source invariants satisfied:

1. **Per-operation thread fallback remains present.** The current implementation still has a fallback that creates a thread for an individual cancelled I/O operation. This violates the single lifecycle-managed Reaper-worker design.
2. **Blocking completion result is not fully checked.** The audit still finds a path where the result of a blocking completion query is not used to prove terminal completion before resource reclamation.
3. **Unconditional cleanup follows a blocking collection path.** A cleanup path can run without a positive terminal-completion proof.
4. **Failed nonblocking completion collection has an unsafe cleanup path.** Failure from the nonblocking result query does not establish that the kernel has stopped using the operation resources.

These are source-audit findings, not a new runtime failure. No attempt was made to bypass the audit or weaken its assertions.

## Required disposition

- Keep `pipe-io-v2.c` unchanged until the lifecycle-managed Worker is implemented and passes the full 28-group named-pipe acceptance matrix.
- The Worker must use one managed worker thread, exact issuing HANDLE + OVERLAPPED identity, retained ownership for incomplete/uncertain outcomes, staged terminal-hold → close/finalizer → release accounting, and no per-operation thread fallback.
- A failed/uncertain close must record `RAYA_RECOVERY_CAUSE_HANDLE_CLOSE`, retain unresolved resources/accounting, and prohibit clean shutdown.
- Do not enable polling recovery.
- Production remains **NO-GO**.

## Validation

- `reaper-source-audit-test.cjs`: expected BLOCKED result, 0/4 invariants satisfied.
- `git diff --check`: exit 0.
- No commit, reset, service provisioning, Production integration, tunnel changes, or IranPriceScout changes.

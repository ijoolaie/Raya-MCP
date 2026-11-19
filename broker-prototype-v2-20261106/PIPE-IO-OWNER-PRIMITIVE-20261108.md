# Pipe I/O ownership primitive and native fault-injection tests — 2026-11-08

## Status

A reusable ownership-state primitive and two test suites were added. The primitive is a testable ownership gate, not a lifecycle-managed production Reaper and not yet wired into `pipe-io-v2.c`. Production remains NO-GO.

Files:
- `pipe-io-owner-v2.h/.c`: explicit operation ownership transitions and cleanup eligibility.
- `pipe-io-owner-v2-test.c`: deterministic state/fault-injection tests.
- `pipe-io-owner-v2-native-test.c`: real overlapped named-pipe tests with injected result-collection failures.
- `build-pipe-io-owner-v2-test.cmd`
- `build-pipe-io-owner-v2-native-test.cmd`

## Ownership states

- `CALLER_PENDING`: caller retains the operation and all resources.
- `CALLER_UNCERTAIN`: caller retains resources after an unclassifiable collection result.
- `REAPER_QUEUED`: ownership has been transferred; caller access is prohibited.
- `REAPER_PENDING`: worker/reaper owns the operation and may probe/collect.
- `REAPER_UNCERTAIN`: reaper retains resources after an unclassifiable result.
- `CALLER_TERMINAL` / `REAPER_TERMINAL`: exact event and exact issuing HANDLE/OVERLAPPED context was supplied and the completion classifier accepted terminal success or the narrowly allowed ERROR_OPERATION_ABORTED result.
- `RELEASED`: exactly one release commit was recorded after terminal collection.

The primitive never closes a HANDLE or frees memory. It only gates the ownership transition and exposes whether resources are eligible for cleanup. The integrating caller must still perform close/free operations and separately account for cleanup failures.

## Three audited failure paths

1. **Normal collection:** a signaled operation event followed by injected `ERROR_IO_INCOMPLETE` leaves the caller pending and resources ineligible for release. The caller can atomically hand off; the reaper later collects the real terminal result and releases once.
2. **Cancellation drain:** an injected nonterminal/uncertain collection failure leaves the caller as owner. Handoff is explicit; the reaper retains resources until the real named-pipe cancellation result is collected. `ERROR_OPERATION_ABORTED` is accepted only with the event-signaled and exact issuing-pair context.
3. **Former per-operation blocking reaper collection:** an injected failure before event completion leaves the operation reaper-owned and uncertain. No release is allowed. After the peer completes the real pending read, a later exact-pair collection succeeds and release is permitted.

Duplicate handoff, duplicate take, invalid collection context, caller access after handoff, release before terminal collection, and double release are rejected by the primitive.

## Verification

MSVC Visual Studio 2026 x64, `/W4 /WX`:
- Deterministic state tests: 51 assertions per process, 25/25 independent process runs passed (1,275 assertions).
- Native named-pipe tests: 400 assertions per process, 10/10 independent process runs passed (4,000 assertions). Each process exercises all three paths with actual overlapped byte-type named-pipe reads. Result-collection failures are injected at the ownership boundary; successful and cancellation terminal results are collected from the real issuing HANDLE/OVERLAPPED pair.
- Total: 5,275 assertions across the two suites, zero failures.
- Test build scripts remove their generated EXE/OBJ files.

## Explicit limitations / next integration requirements

- The primitive is not thread-safe by itself. All state transitions and handoff must be protected by the one lifecycle Reaper lock specified in `CANCELLATION-REAPER-DESIGN-20261106.md`.
- It does not allocate/own the pending record, intrusive queue, registry, wake event, worker, or lifecycle.
- It does not close/free operation resources and does not model partial HANDLE-close failure. A future resource finalizer must preserve per-resource close state and record HANDLE_CLOSE without claiming clean shutdown after a failed close.
- Retry from `REAPER_UNCERTAIN` is a state transition only; the caller must independently re-probe the exact event and preserve exact issuing HANDLE/OVERLAPPED identity before another collection attempt.
- The native harness injects collection outcomes at the ownership boundary; it does not replace the Windows GetOverlappedResult implementation or prove that every injected failure is natively inducible.
- `pipe-io-v2.c` is unchanged. Its three unsafe release paths remain present and the current per-operation CreateThread fallback remains unaccepted. Do not wire this primitive into those paths until the lifecycle-managed Reaper queue/worker, resource finalizer, handle-close accounting, and full 28-group acceptance matrix exist.
- No service, production, tunnel, or IranPriceScout changes were made. Recovery remains disabled.

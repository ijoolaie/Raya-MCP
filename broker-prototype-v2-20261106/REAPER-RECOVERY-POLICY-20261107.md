# Reaper Recovery Policy — isolated primitive (2026-11-07)

Status: test-only policy helper; not integrated into the Reaper.

## Finding
The integrated implementation rejects queue promotion whenever lifecycle is DEGRADED. This conservative behavior avoids blindly draining operations after an unknown failure, but it also blocks recovery after a wake-signal or wait-set failure even when finite polling could safely discover terminal completions.

A single DEGRADED enum does not encode why degradation occurred. A generic allow-promote-while-degraded change is unsafe: completion uncertainty, ownership/accounting corruption, invalid wait-set membership, or handle-close failure must not be treated as a recoverable wake failure.

## Policy candidate
- Polling may be considered only when the accumulated cause set is non-empty and contains exclusively WAKE_SIGNAL and/or WAIT_FAILED.
- COMPLETION_UNCERTAIN, OWNERSHIP_INVARIANT, HANDLE_CLOSE, or WAITSET_INVALID blocks automatic recovery.
- Unknown cause bits and an empty cause set fail closed.
- Causes are monotonic/sticky; adding a blocking cause must never restore eligibility.
- Eligibility is not permission to release an operation. The worker must still zero-time probe the exact registered event and call GetOverlappedResult(FALSE) on the exact issuing pipe HANDLE and OVERLAPPED. ERROR_IO_INCOMPLETE retains ownership; uncertain/API misuse retains resources.
- No recovery route may destroy/recreate tracked operation events, discard registry entries, decrement counters, or claim STOPPED while any resource remains owned.

## Validation
Pure C policy helper and 15-case decision matrix, MSVC x64 /W4 /WX. This helper does not call Win32, change production or prototype integrated lifecycle code, or prove runtime recovery. The 28-group runtime acceptance gate remains mandatory.

## Next implementation gate
Before integration, add an explicit monotonic degradation-cause field to the lifecycle owner, prove every transition records the cause under the shared lock, and test combinations/races. Only then may a polling-only worker mode be considered for the two wake/wait causes. The existing integrated promotion behavior remains unchanged.

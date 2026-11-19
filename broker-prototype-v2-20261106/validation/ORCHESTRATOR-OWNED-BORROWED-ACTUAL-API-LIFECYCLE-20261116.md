# Orchestrator Owned vs Borrowed Worker — Actual API Lifecycle Test — 2026-11-16

## Result
PASS — 30 Owned + 30 Borrowed iterations; 540 explicit CHECK assertions. Disposable x64 build with MSVC `/W4 /WX /TC /O2`. This is a temporary orchestration harness, not a checked-in Orchestrator and not Phase 1 acceptance.

## What was exercised
- Copied actual Adapter, Submitter, Worker implementations and required dependencies into a disposable directory.
- Started the actual Worker and then called actual `raya_integrated_adapter_start`, which initializes the Submitter itself.
- Both ownership modes used the same actual Adapter/Submitter shutdown path: Adapter Destroy, Submitter Shutdown, Submitter Join, Submitter Destroy.
- Owned mode then shut down, joined, and destroyed Worker; 30/30 passed.
- Borrowed mode reached terminal Orchestrator cursor while leaving Worker initialized, started, unjoined, and with a live thread handle; 30/30 passed. The simulated external owner then shut down, joined, and destroyed that Worker successfully.
- Verified Adapter and Submitter uninitialized after orchestrator cleanup in both modes.

## Evidence
- Output: `PASS owned=30 borrowed=30 checks=540 borrowed_worker_left_running_then_owner_destroyed=30`.
- No component APIs failed; no timeout.

## Contract finding
The public Adapter header says it borrows both already-started Submitter and Worker. The implementation's `raya_integrated_adapter_start_with_apis` rejects an already initialized Submitter and calls `raya_submitter_start_with_api` itself. Thus, in current implementation, Adapter borrows Worker but owns/initializes Submitter. This is a documentation/ownership-contract mismatch that must be resolved before a production Orchestrator contract is approved. The harness follows implementation behavior, not the misleading Submitter wording.

## Limitations
- Borrowed Worker behavior was tested with actual component APIs, but the external owner cleanup was executed synchronously by the harness, not by a separate owner process/thread.
- No late-caller pointer lifetime or active-caller drain race was exercised. A raw pointer cannot be made safe after storage is freed; the host must guarantee no new dereference after admission closes and keep storage alive until all admitted calls drain.
- No failure injection in Submitter was combined with these ownership modes; prior Submitter partial-Destroy retry remains a temporary prototype only.
- No checked-in Orchestrator or Production source changes.

## Cleanup and integrity
- Temporary directory contained $($items.Count) files and $bytes bytes; all copied sources, harness, object files, and executable were inventoried and removed.
- No Working Copy source/header/test/build/model/archive/Production file was modified. No Commit or Reset; no process was terminated.

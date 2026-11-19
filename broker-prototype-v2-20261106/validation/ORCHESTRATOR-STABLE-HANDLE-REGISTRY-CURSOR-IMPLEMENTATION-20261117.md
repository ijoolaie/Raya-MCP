# Orchestrator Stable Handle Registry + Cleanup Cursor — Implementation Slice
Date: 2026-11-17

## Scope and placement
Implemented only in the Working Copy:
`D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`

This is a standalone registry/cursor foundation. It is NOT yet wired into Adapter, Submitter, Worker, managed-pipe, or a public Orchestrator facade. No Production/source project files were touched.

## Files created
1. `raya-orchestrator-registry-v1.h`
2. `raya-orchestrator-registry-v1.c`
3. `raya-orchestrator-registry-v1-test.c`
4. `build-raya-orchestrator-registry-v1-test.cmd`

## Implemented contract
- 64-bit scalar handle: high 32 bits generation, low 32 bits slot+1; zero invalid; generation zero is never issued.
- Fixed 64-slot registry; no heap allocation in registry operations.
- Slot lifecycle: EMPTY -> OPEN -> CLOSING -> EMPTY, or RETIRED when generation reaches UINT32_MAX.
- Acquire validates token, slot generation, OPEN state, and optional object kind while holding the registry lock; it increments the lease before returning the object pointer.
- BeginClose atomically closes admission and elects one cleanup owner. A second owner cannot win.
- Release decrements the lease and wakes a draining owner when the last lease exits.
- WaitDrained waits outside component code while the condition-variable API atomically releases/reacquires the registry lock.
- Cleanup cursor has eight step slots, effect/outcome, attempt count, Win32 error, and thread exit code.
- Attempt count increments when a new IN_PROGRESS attempt begins; a committed effect cannot regress.
- CompleteClose requires the correct owner, zero leases, and all attempted steps to be explicitly terminal (committed with success/abnormal-exit/terminal-failure). NOT_ATTEMPTED steps are treated as not applicable.
- Owner-token exhaustion fails closed rather than reusing token zero/old owner IDs.
- Registry storage must outlive all handles/leases/cleanup; initialized registries must not be copied or relocated.

## Verification actually performed
- MSVC x64 compile with `/W4 /WX`: PASS.
- Registry test: 25 assertions per run, 10 consecutive build-and-run executions; 250 assertions passed, zero failures.
- Covered: initialization, typed acquire, wrong-kind rejection, BeginClose owner election, second-owner rejection, admission denial after close, outstanding-lease timeout, no reclamation with live lease, cursor persistence, release/drain, stale-generation rejection, slot reuse with new generation, and concurrent lease drain.
- Build script removed its executable and object files. Post-run check found no registry test EXE/OBJ artifacts.
- Post-run process inventory found no `cl.exe`, `link.exe`, `java.exe`, or `tlc.exe`.
- Disk after tests: C free 12,567,810,048 bytes; D free 2,110,074,880 bytes.

## SHA-256
- Header: `21E2F84C5670060456966BA67A970FDE43C523FD24C94C4B3CDCBF636BAC85B0`
- Implementation: `B9D026A2F8B915A8D49AF36C9E448DFE3ABD084E092B671D7F73D968CB1E7353`
- Test: `57C820C78C43C4E7D24111AC69298FFCE0387AFF4323B6B6B72E65CA70B5C80D`
- Build script: `54928063FD1D40686A20272C408C82F3FAC67CE7CF2E639E89D9D223B74B067F`

## Explicit limitations / next gates
1. A stable token only protects callers that acquire through this registry. Existing raw-pointer public component APIs remain unsafe against stale external pointers until every external entry point is moved behind an opaque Orchestrator facade.
2. Registry itself does not own/free component objects. The host must prevent new handle acquisitions and keep registry storage alive for process lifetime (or longer than all references).
3. No Adapter/Submitter/Worker lifecycle calls are made by this module. Cleanup step meanings and Owned/Borrowed policy still need an Orchestrator implementation.
4. Submitter `_ex` results are not yet mapped into cursor transitions; Worker has no corresponding structured `_ex` lifecycle API.
5. Shutdown DEGRADED recovery policy, startup rollback, Worker failed-start close retry, callback self-destroy prevention, and chained three-component failure tests remain open.
6. No claim of full Orchestrator, full Raft, Phase 1 acceptance, or Production readiness.

## Scope protection
- No commit/reset.
- No README overwrite.
- No Production/source project modifications.
- IranPriceScout and its independent port-8765 tunnel were not touched.
- No process was terminated.

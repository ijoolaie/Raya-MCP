# Stable Handle + Registry Lifetime Contract (2026-11-16)

Status: design/audit only. No implementation, build, or test was run. No production or source-project change.

## Decision

For Architecture A (Orchestrator-only public facade), use a process-lifetime handle registry and a scalar, generation-tagged token. Do not expose a raw component pointer or an opaque pointer whose validity must be tested by dereferencing it.

A proposed 64-bit token is:

- low 32 bits: slot index;
- high 32 bits: generation.

Zero is invalid. Slot indices are bounds-checked before indexing. Generation zero is never issued. On slot reuse, increment generation. If a generation would wrap, permanently retire that slot rather than allowing an old token to become valid again.

The registry itself must outlive every possible API call that can present an old token. The simplest v1 contract is a process-lifetime registry initialized once and never destroyed. A future explicit registry object is acceptable only if the host guarantees that its lifetime exceeds all clients and all in-flight calls. A registry pointer passed by an arbitrary caller does not solve the stale-pointer problem.

## Current-code evidence

The current public headers expose complete component layouts:

- integrated-submit-adapter-v2.h exposes its lock, API callbacks, Submitter and Worker pointers, retained list, counters, lifecycle gate and initialized flag.
- dedicated-io-submitter-v2.h exposes lock, Win32 handles, queue links, counters, lifecycle and callback fields.
- reaper-managed-worker-v2.h exposes lock, wake/thread handles, integrated state, retained list, callbacks and lifecycle flags; the operation structure is also public.
- managed-pipe-io-v2.h includes the Worker header directly.

A targeted source scan found 63 direct component-structure/API references in integrated-submit-adapter-v2.c and 6 in managed-pipe-io-v2.c. The latter is a second internal consumer that must be accounted for when splitting public and private headers.

An existing reaper-registry-v2 is not a suitable handle registry: it is a fixed 256-entry operation/event table, requires an external ownership lock, and has no generation, API lease, closing state, or registry-lifetime protocol. Reuse its name or semantics for public handles would conflate distinct responsibilities.

## Registry entry model

Each slot is permanently addressable for process lifetime and contains at least:

- generation;
- state: FREE, CONSTRUCTING, OPEN, CLOSING, CLEANUP, RETIRED;
- internal object pointer (valid only in OPEN/CLOSING/CLEANUP);
- active API lease count;
- cleanup-owner flag/token;
- cleanup cursor and last cleanup status.

All slot metadata and token validation are protected by one registry synchronization primitive. The registry lock is held only for short metadata transitions. Never call component APIs, invoke user callbacks, wait on events/threads, close handles, or free memory while holding it.

## Acquire / release protocol

1. API receives a scalar token, not a pointer.
2. Under registry lock, decode and bounds-check slot; compare generation; require state OPEN; reject otherwise.
3. Increment active lease count and copy the internal object pointer to a local variable before releasing the registry lock.
4. Perform the component operation under its own documented synchronization.
5. Release the lease on every return path. Decrement under registry lock and signal a drain condition/event when the count reaches zero.
6. No API may retain the internal pointer beyond its lease unless it obtains a separately specified ownership reference.

The registry lock linearizes Acquire against BeginClose. If Acquire wins first, it owns a lease that cleanup must drain. If BeginClose wins first, later Acquire calls reject. A caller must not read entry fields outside the registry lock until it owns a lease.

## Close / cleanup protocol

BeginClose is a special registry operation, not an ordinary leased component call:

1. Validate token and generation under registry lock.
2. Transition OPEN -> CLOSING exactly once. Reject all new ordinary leases from this point.
3. Elect one cleanup owner under the same lock. Other destroy callers receive BUSY (or join a separately specified completion wait); they must not run component cleanup concurrently.
4. Release registry lock. Wait for active leases to drain, or return BUSY/INCOMPLETE and allow an explicit retry. Do not free the object while leases remain.
5. Once leases are zero, transition to CLEANUP and resume the persisted cleanup cursor. Advance the cursor only after a substep succeeds. A failed close/join/destroy leaves the cursor at that substep and preserves all state needed for retry.
6. No user callback or component close operation runs under registry lock. Reentrant calls with the same token see CLOSING/CLEANUP and fail promptly; destroy-from-callback must be prohibited or explicitly deferred.
7. After all component resources are terminal and internal object destruction succeeds, reacquire registry lock, verify generation/state/owner and zero leases, clear object pointer, increment generation, then mark FREE (or RETIRED on generation exhaustion). Only then is the old token permanently invalid.

A failed cleanup does not reopen admission. It leaves the slot CLOSING/CLEANUP and the same token usable only for authorized cleanup retry. Ordinary APIs remain rejected.

## Object and registry lifetime distinction

The slot/registry is the stable validation surface; the component object is not. The object may be freed only after:

- no new leases can be acquired;
- all existing leases have drained;
- the cleanup owner is exclusive;
- component-specific ownership and thread/I/O terminal conditions are proven;
- all cleanup cursor steps have succeeded.

A process-lifetime registry avoids callers dereferencing freed registry memory. If DLL unload or process teardown is in scope, the host must first stop all clients and prove global quiescence; no library can safely validate a token after its own registry code/data has been unloaded.

## Proposed public API shape (illustrative, not implemented)

- typedef uint64_t raya_public_handle;
- raya_status raya_orchestrator_create(const raya_config*, raya_public_handle*);
- raya_status raya_orchestrator_submit(raya_public_handle, const raya_submit_request*);
- raya_status raya_orchestrator_query(raya_public_handle, raya_snapshot*);
- raya_status raya_orchestrator_close(raya_public_handle, DWORD timeout_ms);
- raya_status raya_orchestrator_retry_close(raya_public_handle, DWORD timeout_ms);

The public header contains only the token, stable enums, request/snapshot value types, and facade functions. Component structs and direct component lifecycle functions move to private internal headers. Adapter and managed-pipe implementation receive explicit private interfaces; tests are split into public-facade tests and internal white-box component tests. Do not make component functions static indiscriminately, and do not bypass component cleanup by zeroing structs, deleting locks, or closing handles from the Orchestrator.

## Locking and callback rules

- Registry lock protects slot identity, generation, state, lease count and cleanup-owner election.
- Object/component locks protect their own queues, retained lists and lifecycle state.
- Never nest registry lock with component locks.
- Never call user callbacks, Win32 wait functions, CloseHandle, thread joins or memory allocators while holding registry lock.
- Callbacks may call ordinary facade APIs only while the handle is OPEN and the callback's existing lease remains valid. A callback must not synchronously destroy its own Orchestrator; return BUSY/DEFERRED or schedule close on a separate owner.
- The cleanup owner must not wait for a callback that is itself blocked waiting for cleanup to finish. This deadlock must be tested explicitly.

## Required acceptance tests before implementation can be accepted

1. Valid token acquires a lease; wrong slot, zero token, stale generation and out-of-range slot reject without dereference.
2. Acquire racing BeginClose has exactly two legal outcomes: lease acquired before close and drained, or rejected after close. No admission after CLOSING.
3. Multiple concurrent close callers elect exactly one cleanup owner; all others return BUSY or observe the same terminal result.
4. A held lease prevents object free; close retry succeeds after release.
5. Each cleanup substep can fail repeatedly; cursor resumes at the failed step and never repeats successful destructive steps.
6. Stale token after slot reuse rejects; generation mismatch prevents ABA.
7. Generation exhaustion retires a slot and never wraps to an old generation.
8. Callback reentrancy into ordinary APIs is bounded; destroy-from-callback follows explicit deferred/BUSY behavior without deadlock.
9. Thread Join, pending I/O, retained operation and uncertain ownership prevent final unregister/free.
10. Registry remains valid through DLL/process client shutdown contract; no call is possible after unload.
11. Adapter and managed-pipe internal consumers compile against private interfaces without exposing layouts in the public facade.
12. Borrowed versus Owned Worker cleanup is respected by the Orchestrator cursor.
13. Public facade stress with concurrent submit/query/close and randomized delays; assert no use-after-free, double close, duplicate cleanup, or post-close admission.
14. Re-run existing component suites after the header/API split; retain all existing test coverage.

## Gate and limitations

This is a design contract, not proof of implementation correctness. It does not yet solve the existing Submitter/Worker partial-destroy transitions, late callers that violate the public handle contract, thread-bound overlapped I/O, or component cleanup semantics. Those remain independent gates.

No source/header/test/model/config/archive/Production files were modified by this audit. No build, TLC, or test process was started or terminated. No temporary files were created. No commits or resets were performed.

## Working-copy source hashes at audit time

- integrated-submit-adapter-v2.h: 4E2CC45DCD9E4EA0EA47441158B40F25CCD900095182CD256A90C285FB8D4882
- integrated-submit-adapter-v2.c: 6E2427C09974EE47B382F80CD11B21FEAAC16D12ECABC6A6155A11E9E8F62FBE
- dedicated-io-submitter-v2.h: 2D4CBB91D3400AC5416FC1DE25D03012AC2D2644D6F848A42D278F38D5191655
- dedicated-io-submitter-v2.c: D8ABAFD8D3B731DA141E8B1792534A00EB888055D0F15EB0EDFCCE2D01843941
- reaper-managed-worker-v2.h: F45DB7509C2045A37925A86BE06F4871109BA5348A383B75447ACD3C46423818
- reaper-managed-worker-v2.c: FDCD768B31CD6FF30F435A5588D6D1000394A7B355A2E37C1A8117D6C78225E4
- managed-pipe-io-v2.h: 558CCEE826FAB783A0F1A60F44702E6E62FA18422AFBA7B6AA5C209180E0CE5A
- managed-pipe-io-v2.c: 2FDEF399F1491C613F6E7A8593A4C4117483D2FBB4E67AC3A56559332CF66891

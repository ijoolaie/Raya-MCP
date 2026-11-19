# Process Isolation Supervisor v2
Date: 2026-11-08
Status: isolated native prototype; no Broker/Production wiring

## Objective
Contain an unrecoverable Submitter/Worker thread failure without attempting unsafe in-process thread termination or freeing memory still reachable by a live thread. A dedicated Supervisor process owns the Broker Worker process. If the Supervisor process exits unexpectedly, Windows closes its Job Object handle; JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE then terminates all processes still assigned to that Job.

## Launch protocol
1. Supervisor creates a non-inheritable Job Object.
2. Set JobObjectExtendedLimitInformation with JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE. Do not enable BREAKAWAY_OK or SILENT_BREAKAWAY_OK. Do not cap active process count until child-process requirements are known.
3. Create the Worker with CREATE_SUSPENDED and bInheritHandles=FALSE.
4. AssignProcessToJobObject before the Worker executes any user code.
5. Verify IsProcessInJob against the exact Job handle.
6. ResumeThread only after successful assignment and verification.
7. Keep the Job handle open for the entire Worker lifetime. Closing the last Job handle is the containment action on Supervisor process loss.

## Current native module
- process-supervisor-v2.h/.c exposes start, bounded wait, explicit job termination, and close-after-positive-exit.
- Start owns Job, process and primary-thread handles. Failure cleanup is conservative: if termination, wait, or any close cannot be proven, the structure remains DEGRADED with remaining handles retained.
- Worker starts suspended; assignment and membership verification precede ResumeThread.
- No child handles are inherited.
- Wait timeout preserves all handles and state.
- terminate_job is an explicit whole-Job containment action, not a Thread termination mechanism.
- close is rejected until the process is positively signaled and collected.
- Test executable supports an inert sleep worker and a deterministic exit worker; no application data or external services are used.

## Security and protocol requirements before real Broker integration
- Run Supervisor and Worker under dedicated service identities; verify actual TokenUser and service SID at runtime. Do not use an interactive user token.
- Define a local named-pipe control protocol with explicit version, bounded frames, request IDs, peer PID/session/SID verification, and strict ACLs.
- Authenticate the Worker connection using the expected service SID and process identity; do not trust a PID supplied by the client.
- Keep control handles non-inheritable and use an explicit handle allowlist if a future design requires inheritance.
- Worker must not be able to close or duplicate the Supervisor's Job handle.
- Prevent job breakaway and verify the process remains in the expected Job.
- Decide whether nested jobs and host-managed job membership are supported. If AssignProcessToJobObject fails, do not fall back to unsupervised launch.
- Define graceful stop over authenticated IPC; timeout escalates to TerminateJobObject only after the configured drain deadline.
- Supervisor restart must not attach to an unknown pre-existing Worker. A new Worker gets a new Job and a new authenticated session.
- Persist diagnostics, not secrets; never log tokens or command lines that contain credentials.

## Explicit non-goals / blockers
- No Windows service installation, service identity provisioning, ACL changes, named-pipe server/client, restart policy, watchdog, or production executable.
- No integration with integrated-submit-adapter-v2 or Managed Worker.
- No claim that the current test proves service isolation, IPC authentication, restart correctness, or descendant-process policy for a real Broker.
- Job Object kill-on-close is containment, not graceful shutdown. Kernel cleanup may be asynchronous; the Supervisor must still wait/observe process exit before releasing its own state.
- If Supervisor itself is forcibly terminated, Job close is driven by kernel handle teardown. This behavior is covered by a direct native test.
- A failed close or uncertain process state means retain the supervisor structure and report incomplete teardown.

## Acceptance implemented in this slice
1. Compile with MSVC x64 /W4 /WX.
2. Launch child suspended, assign exact Job, verify membership, resume.
3. Bounded wait timeout retains process and Job handles.
4. Explicit TerminateJobObject terminates child and allows close only after positive process signal.
5. Natural child exit is collected with exact exit code.
6. Closing the last kill-on-close Job handle terminates the assigned child.
7. No thread is terminated and no live-process handles are closed prematurely.

Production remains NO-GO pending identity, ACL, IPC, service lifecycle, fault-injection, restart, and full Broker integration acceptance.

## Supervisor partial-start and retained-handle recovery Ã¢â‚¬â€ 2026-11-08

- Clarified the public start contract: `raya_supervisor_start()` returning false may leave a DEGRADED object with retained handles and, in the ResumeThread-success/primary-thread-CloseHandle-failure case, a live child. The caller must retain the structure, use bounded wait or explicit Job termination as appropriate, then retry close; it must not zero or free the object.
- `raya_supervisor_close()` now accepts EXITED and DEGRADED states and independently closes only handles still owned by the structure. If a process handle remains, it must be positively signaled before any close proceeds; degraded state additionally requires successful exit-code collection. If the process handle was already closed after collection, retries may finish closing remaining thread/Job handles.
- This gives a retry path for retained Job-only state after failed-start cleanup and for partial handle-close failures, without treating a live child as exited.
- Rebuilt with MSVC x64 `/W4 /WX`; the existing 25-process runtime suite passed: 24 assertions per process, 600 total.
- Limitation: CloseHandle failure branches are not yet deterministically injected; the revised retry contract is code-reviewed and regression-compiled, not directly fault-injection tested. A Win32 API seam and branch-specific tests remain the next step.

## Supervisor close-handle fault seam Ã¢â‚¬â€ 2026-11-09

- Added a narrow `raya_supervisor_api` seam for `CloseHandle`, with `raya_supervisor_close_with_api()`; the existing `raya_supervisor_close()` remains source-compatible and uses the real Win32 API.
- Deterministic test injects a failure on the first close attempt after a child exits. It verifies the process and Job HANDLE values remain owned, state becomes DEGRADED, and a second close attempt successfully closes the retained process and Job handles without double-closing.
- MSVC x64 `/W4 /WX` build and 25 fresh-process runtime iterations passed: 34 assertions per process, 850 total.
- Scope is intentionally narrow: CreateJobObject/SetInformationJobObject/CreateProcess/Assign/IsProcessInJob/Resume/Wait/Terminate/GetExitCode failures are not yet injectable. This is not full Win32 API fault coverage.

## Supervisor start-path fault injection Ã¢â‚¬â€ 2026-11-09

- Extended the API seam to cover CreateProcessW, AssignProcessToJobObject, IsProcessInJob, ResumeThread, TerminateProcess, WaitForSingleObject, and GetExitCodeProcess. Existing callers remain source-compatible through the original start/wait entry points.
- Added deterministic native tests for AssignProcessToJobObject failure and ResumeThread failure. Both paths create a real suspended child, inject the selected failure, exercise bounded child termination/collection, and verify the supervisor returns to EMPTY with no retained Job/process/thread HANDLEs.
- Existing normal launch/terminate, natural exit, Job kill-on-close, and CloseHandle retry cases remain in the same suite.
- MSVC x64 `/W4 /WX`; 25 fresh-process runs passed, 44 assertions per process = 1,100 assertions.
- Remaining seam gaps: CreateJobObjectW and SetInformationJobObject are still direct calls; the IsProcessInJob callback currently forwards to the real API but its negative result branch is not yet fault-injected. TerminateJobObject is not yet injected. This is partial fault coverage, not full Supervisor acceptance.

### API injection follow-up Ã¢â‚¬â€ 2026-11-09

- Extended the supervisor API seam with injectable Job Object creation, Job Object configuration, and Job termination callbacks; added an API-aware terminate-job entry point while preserving the original compatibility wrapper.
- Routed start-failure cleanup handle closes through the supplied close callback. The close path now routes process wait and exit-code collection through the injected API as well.
- Added deterministic coverage for CreateJobObject failure and SetInformationJobObject failure. The existing AssignProcessToJobObject, ResumeThread, and close-retry cases remain in the suite.
- MSVC x64 `/W4 /WX` build passed. The build script ran 25 fresh process invocations, each reporting 49 assertions: 1,225 assertions passed.
- Remaining gaps: no injected negative/failed IsProcessInJob response test; no TerminateJobObject failure test; SetInformationJobObject failure with injected close failure/retained Job retry is not yet covered. Do not treat this as full supervisor acceptance.

### Supervisor remaining fault-injection cases Ã¢â‚¬â€ 2026-11-09

- Added both `IsProcessInJob` failure modes: API failure and successful API call reporting FALSE membership. Each test starts a real suspended child, verifies the fail-closed path terminates and collects it, and asserts EMPTY state with no retained Job/process/thread handles.
- Added `TerminateJobObject` failure injection. The test verifies DEGRADED state retains the live process and Job handles, confirms the child is still running, then performs real Job termination and close to prove recovery.
- Added combined `SetInformationJobObject` + `CloseHandle` failure coverage. The failed start retains a Job-only DEGRADED object; a subsequent close retry succeeds without losing or double-closing the handle.
- MSVC x64 `/W4 /WX` build passed. The build script completed 25 fresh-process runs, each reporting 74 assertions: 1,850 assertions passed.
- This closes the three fault-injection gaps listed in the previous entry for these specific branches. It does not establish full Supervisor acceptance: host/nested Job behavior, broader API failure combinations, process crash/restart, independent supervisor executable, authenticated IPC, service identity, and Broker integration remain unimplemented or unaccepted. Production remains NO-GO.

## Host and nested Job Object acceptance probe Ã¢â‚¬â€ 2026-11-09

- Extended `process-supervisor-v2-test.c` with a bounded native probe that checks whether the test host is already in any Job Object, whether a suspended child inherits that membership, and whether the child can be assigned to a new Job and then a nested Job.
- The probe verifies exact membership in both the inner Job and its outer ancestor. If host policy blocks either assignment, it reports the blocking Win32 error and still performs bounded child cleanup; it does not silently treat a blocked assignment as a successful nested-Job result.
- On this Windows 10.0.26300 host, the test process was already in a host Job; the child inherited host-Job membership. Assignment to both the explicit outer Job and nested inner Job succeeded, and `IsProcessInJob` verified both memberships.
- The full MSVC x64 `/W4 /WX` suite passed in 25 fresh processes, 80 assertions per process = 2,000 assertions. Each iteration reported `parent_job=1`, `child_inherited=1`, `outer_assignment=PASS`, `inner_assignment=PASS`, and `ancestor_membership=PASS`.
- This is host-specific evidence only. It does not prove the future Windows service account, service SID, service-host Job policy, or every Windows configuration will permit nested Job assignment. The production launcher must fail closed if assignment to its intended Job is denied; it must not fall back to an unsupervised Worker.
- No service, identity, ACL, or Production setting was created or changed. Temporary probe source, executable, and object files were removed by the probe command; the normal build script also removed its outputs.

## Supervisor control IPC design handoff Ã¢â‚¬â€ 2026-11-09

- Added `SUPERVISOR-CONTROL-IPC-V2-DESIGN-20261109.md` as a separate design artifact; no runtime pipe code or service provisioning was added.
- The contract reuses the existing bounded RY2R v2 frame envelope and limits messages to lifecycle/health control (`HELLO`, `CHALLENGE`, `AUTH_BINDING`, `READY`, `PING/PONG`, `STOP/STOPPED`, `FATAL`). Filesystem verbs, paths, credentials, arbitrary RPC, and Job HANDLE transfer are explicitly prohibited.
- Peer authentication is based on OS-reported pipe peer PID, owned process HANDLE cross-checks, token user/session/service-SID checks, and a restrictive local-only pipe DACL. Nonces provide freshness/session binding only; they are explicitly not treated as cryptographic authentication or a substitute for Windows identity checks.
- Failure policy is fail-closed: malformed/partial frames, identity mismatch, disconnect, or deadline uncertainty degrade the channel while retaining process/Job ownership. Graceful-stop timeout escalates only to whole-Job termination, followed by positive process wait before handle release.
- The document defines a staged test plan. Runtime IPC, service identity, service SID, pipe ACL, restart, and Broker integration remain NOT TESTED. The design does not authorize service creation, ACL changes, or Production wiring.

## Supervisor control payload/state-machine implementation Ã¢â‚¬â€ 2026-11-09

Added isolated pure-C files `supervisor-control-v2.h/.c`, `supervisor-control-v2-test.c`, and `build-supervisor-control-v2-test.cmd`. This layer performs no pipe, process, token, filesystem, or service operations.

- Strict opcode/length validation for HELLO (39 bytes), CHALLENGE (33), AUTH_BINDING (49), READY (17), PING/PONG (5), STOP (7), STOPPED (5), and FATAL (3); unknown opcodes, null/oversized payloads, invalid reserved/version/reason bytes, and trailing/short payloads are rejected.
- Explicit big-endian u32 helpers, fixed-width nonce equality helper, and nonzero contiguous request-ID admission with wrap rejection.
- Lifecycle state machine rejects wrong ordering, does not allow READY unless the caller supplies the independently verified peer-identity gate, and does not allow CLOSED until PROCESS_EXIT_CONFIRMED follows STOPPED.
- Failure transitions to sticky DEGRADED; no recovery transition is defined.
- MSVC x64 `/W4 /WX` build succeeded. Final test run after tightening handshake ordering: 3,052 assertions; 25 fresh-process runs = 76,300 assertions.

Important boundary: this is a payload shape/state transition primitive, not the complete authenticated handshake. It does not itself compare generation/nonces across message transcripts or maintain a replay cache. It validates the fixed-width payload shapes and gates READY on challenge, binding, and an externally verified peer identity; the trusted caller must still compare transcript values and enforce the frame request-ID sequence. It does not perform OS token/PID/ACL checks or implement transport deadlines. Those remain caller/transport responsibilities and are NOT ACCEPTANCE-TESTED. In particular, nonce equality is only a helper and never authenticates a peer. No named pipe, service, identity, ACL, Job, or Production configuration was changed. Production remains NO-GO.

## Control handshake transcript binding â€” 2026-11-09

Extended `supervisor-control-v2` with an explicit per-connection transcript object and exact byte offsets:

- HELLO (39): opcode, zero protocol-minor bytes, 4-byte build identifier, 16-byte generation, 16-byte Worker nonce.
- CHALLENGE (33): opcode, exact generation echo, 16-byte Supervisor nonce.
- AUTH_BINDING (49): opcode, exact generation echo, exact Worker nonce echo, exact Supervisor nonce echo.
- READY (17): opcode and exact generation echo; accepted only after binding and the independent identity-verification gate.
- Zero generation/nonces are rejected. HELLO is single-use in a transcript; challenge must be issued before AUTH_BINDING; binding and READY are single-use. Cross-generation and either-nonce mismatch are rejected.

MSVC x64 `/W4 /WX` build passed. Test executable passed 34 assertions per process across 25 fresh processes (850 total). Build artifacts removed.

Security boundary: this is per-session replay/order protection only. It does not maintain a process-wide generation/nonce replay cache; the future Supervisor must generate a fresh unpredictable generation and both nonces from the Windows cryptographic RNG and never reuse a generation across reconnects/restarts. The caller must also bind the identity-verification result to the exact pipe peer and owned process HANDLE. No cryptographic authentication is provided by these nonces. No OS pipe/service/identity/ACL or Production changes were made; Production remains NO-GO.

## In-process generation replay cache primitive â€” 2026-11-09

- Added a fixed-capacity 128-entry generation reservation cache. A nonzero 16-byte generation can be reserved only once during the cache lifetime; duplicate generations are rejected.
- The cache never evicts old entries. Once full it becomes exhausted and rejects all further reservations (fail-closed); no LRU/ring overwrite can re-admit an old generation.
- Added `raya_ctl_transcript_init_guarded()` which reserves the generation before initializing a transcript. Callers must use this guarded initializer for every new session in a Supervisor process. The older unguarded initializer remains only for isolated compatibility/tests and MUST NOT be used by a runtime listener.
- Tests cover duplicate reservation, successful distinct reservation, guarded initialization, full-cache exhaustion, and fail-closed behavior. MSVC x64 `/W4 /WX` passed; 171 assertions per process across 25 fresh processes = 4,275 assertions.

Critical lifecycle boundary: this cache is in-memory only. It does not prevent generation reuse after Supervisor restart, does not persist reservations, and is not thread-safe; the future Supervisor must serialize reservations or protect the cache with its lifecycle lock. Cross-restart replay prevention requires a separately designed durable monotonic epoch/ledger with crash-safe atomic update, or a rigorously justified cryptographic-random generation policy plus persistent replay state. Random generation must come from the Windows CNG system RNG; this primitive does not generate randomness. Cache exhaustion requires creating a fresh Supervisor process only after a restart-safe generation strategy is in place. Production remains NO-GO; no Windows service, pipe, identity, ACL, or Production configuration changed.

## Durable generation ledger core â€” 2026-11-09

Added isolated pure-C files `generation-ledger-v2.h/.c`, `generation-ledger-v2-test.c`, and `build-generation-ledger-v2-test.cmd`. The implementation has no OS/file I/O; storage is supplied through read/write/flush callbacks so failure ordering can be tested without creating a persistent file or changing ACLs.

- Two fixed slots hold 32-byte records: magic `RYGL`, format version 2, big-endian sequence/epoch (required equal and nonzero), CRC-32 over the first 24 bytes, and complemented CRC. CRC is for torn/corrupt-write detection only; it is NOT authentication or tamper protection.
- First initialization is an explicit caller-authorized operation and succeeds only when both slots report empty. It writes slot 0, flushes, reads back, and verifies before publishing epoch 1. There is no automatic reinitialization when records are missing or corrupt.
- Load accepts one valid record plus one empty slot, or selects the larger epoch when both valid. Any read error, malformed nonempty slot, or ambiguous/corrupt state fails closed. A torn inactive-slot update can therefore require operator recovery rather than silently choosing a potentially stale epoch.
- Reserve writes the inactive slot, flushes it, reads it back, verifies exact bytes and decoded epoch, and only then publishes the new epoch in memory. Any uncertain write/flush/readback outcome makes the instance DEGRADED; it must reload/reconcile before issuing another epoch. A durable-but-unacknowledged update may skip an epoch after restart, which is safe. UINT64_MAX exhaustion is fail-closed.
- The caller must serialize all ledger operations with a lifecycle lock. The primitive does not create generation randomness. The runtime contract should encode the reserved 64-bit epoch in the first eight bytes of the 16-byte session generation and fill the remaining eight bytes from `BCryptGenRandom` (one generation per epoch); reserve/flush the epoch before generating or publishing the session. RNG failure burns the epoch rather than reusing it. This binds every issued generation to a unique durable epoch while retaining 64 bits of CNG randomness per generation.
- MSVC x64 `/W4 /WX` passed; 25 fresh-process test runs passed. The test suite covers first install, sequential reservations, reload, write/flush/readback failures, corruption, empty/uninitialized storage, malformed records, overflow, and 100 consecutive epochs.

Critical limits: no concrete Windows storage adapter exists; no `FlushFileBuffers`, filesystem metadata durability, ACL/service identity, backup rollback, or power-loss behavior has been tested. Two-slot local storage cannot prevent an attacker or restore operation from rolling the entire ledger back. CRC does not resist malicious modification. A production adapter requires a dedicated service identity, protected DACL, explicit anti-rollback threat decision, and real crash/power-loss testing. No file-backed ledger, service, pipe, identity, ACL, Production setting, or IranPriceScout component was created or changed. Production remains NO-GO.


## CNG-backed generation composition — 2026-11-09

Extended the ledger core with `raya_ledger_create_generation()`: it durably reserves the next epoch first, writes that epoch as the first eight bytes of the 16-byte generation (big-endian), then asks an injected RNG for the remaining eight bytes. The output is zeroed before work and on RNG failure. An RNG failure intentionally burns the already committed epoch; it does not roll the ledger back or permit reuse. The caller must hold the same lifecycle lock across reservation and generation publication.

Added `generation-cng-v2.h/.c`, a thin Windows adapter that calls `BCryptGenRandom(NULL, ..., BCRYPT_USE_SYSTEM_PREFERRED_RNG)` and reports success only for NTSTATUS success. The adapter is linked with `bcrypt.lib`; the storage layer remains callback-backed and in-memory in tests. Tests cover deterministic RNG composition, RNG failure/zero-output/epoch-burn behavior, successful subsequent issuance, and a real CNG invocation. MSVC x64 `/W4 /WX` passed; 25 fresh-process runs passed. No concrete persistent storage adapter, service identity, ACL, file, or Production wiring was added.

The 64-bit random suffix is not independently a 128-bit random generation; uniqueness is anchored by the durable epoch. This scheme still depends on the ledger not being rolled back. A restored/modified ledger can reissue an epoch, so protected storage and a separately accepted anti-rollback strategy remain hard blockers. CNG success verifies API integration only, not entropy quality or resistance to VM snapshot rollback.

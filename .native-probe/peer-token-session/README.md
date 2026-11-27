# Reciprocal named-pipe peer identity probe (test-only)

Date: 2026-11-27

Status: isolated native Windows harness. No service, Broker, or Production wiring.

## What it exercises

- A parent process creates a uniquely named local byte-mode pipe with a protected DACL and `PIPE_REJECT_REMOTE_CLIENTS`.
- The parent starts one child process with handle inheritance disabled.
- The parent obtains the connected client PID through `GetNamedPipeClientProcessId` and compares it to the PID from the process HANDLE returned by `CreateProcessW`.
- The parent opens the OS-identified child process and compares its `TokenUser` SID and `TokenSessionId` with the expected local identity/session.
- The child independently obtains the server PID through `GetNamedPipeServerProcessId`, opens that process, and compares its `TokenUser` SID and `TokenSessionId` with its own.
- The child sends a deliberately forged server PID in the test payload. The parent verifies that the claim differs from the actual OS-reported peer PID; the test uses the OS API and owned process HANDLE as authority, not the payload field.
- The parent keeps the connection open until both sides complete the checks, then releases the child and verifies its exit.

## Validation on 2026-11-27

MSVC x64 build passed with `/W4 /WX`. Ten fresh parent/child process runs passed, each reporting 13 PASS and 0 FAIL (130 assertions total). The build script removes the generated executable and object file after the runs.

## Limits

- Both processes run under the same interactive Windows identity and session. No different-user or different-session token was provisioned or tested.
- This proves the OS peer-PID APIs and token SID/session queries on this host; it does not prove the future Broker service SID, service-token restrictions, pipe production SDDL, or service launch configuration.
- `PIPE_REJECT_REMOTE_CLIENTS` is configured here but actual remote-host denial is not tested by this harness. The separate supervisor pipe test covers UNC loopback only, not a second physical/virtual host.
- This is a test harness, not a reusable authenticated IPC implementation. It does not implement RY2R framing, generation/nonce protocol, authorization policy, replay cache, Job Object membership, or supervisor restart.
- No filesystem operation, service installation, persistent ACL change, user provisioning, or Production integration is included.
- Phase 1 remains IN PROGRESS / NOT ACCEPTED; Production remains NO-GO.

No runtime package imports this directory.

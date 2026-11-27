# Broker v2 pinned-root read-only vertical slice (test-only)

Date: 2026-11-27

Status: The revised independent-config layout passed elevated runtime acceptance on 2026-11-27. This is not Production integration and does not complete Phase 1.

## Candidate design

- Disposable service identity: `RayaFsBrokerV2Probe20261127` / `NT SERVICE\RayaFsBrokerV2Probe20261127`.
- Workspace fixture: `C:\ProgramData\RayaFsBrokerV2Probe-20261127`.
- Independent service-owned policy directory: `C:\ProgramData\RayaFsBrokerV2ProbeConfig-20261127`.
- Protected broker image directory: `C:\ProgramData\RayaFsBrokerV2ProbeBin-20261127`.
- Service startup verifies the dedicated service SID and rejects enabled Backup, Restore, Take Ownership, or Debug privileges.
- The service opens and pins the config directory before reading `client.policy`. The config directory must be a non-reparse directory owned by the service SID, with a protected DACL containing exactly three full-control allow ACEs: service SID, SYSTEM, and Administrators.
- `client.policy` is opened relative to the pinned config HANDLE. Its same file HANDLE is checked for regular-file properties, single-link admission, service-SID ownership, and the exact protected three-principal DACL before parsing.
- The workspace root is opened and pinned separately. Its protected DACL must contain exactly four allow ACEs: service SID, SYSTEM, Administrators (full control), and the authorized client SID (read-only).
- Payload reads are relative to the pinned workspace HANDLE using `NtCreateFile`, `OBJECT_ATTRIBUTES.RootDirectory`, `OBJ_DONT_REPARSE`, and `FILE_OPEN_REPARSE_POINT`.
- Protocol accepts only the fixed root ID `fixture`; caller-supplied paths and unsupported operations are rejected.
- Setup negative tests add an Everyone Modify ACE separately to the config directory and workspace root; both must cause service startup refusal. Restricted-token checks cover file write, WRITE_DAC, WRITE_OWNER, FILE_WRITE_ATTRIBUTES, directory FILE_ADD_FILE, broker-image GENERIC_WRITE, and hard-link creation denial.

## Named-pipe peer identity

- `GetNamedPipeClientProcessId` must provide a nonzero OS-reported peer PID. The PID is diagnostic only; the service does not open the client process or rely on cross-process process-query access.
- `ImpersonateNamedPipeClient` and `OpenThreadToken` obtain the actual pipe-client token. `TokenUser` and `TokenSessionId` from that token must match the configured SID/session.
- No identity field in the request frame is trusted. `RevertToSelf` is mandatory; if it fails, the service terminates rather than reusing an impersonating worker thread.
- The local `--peer-auth-test` exercises the same authentication function over a real local named pipe: authorized SID/session accepted, wrong SID and wrong session rejected. Nine checks passed in the last local run.

## Trust-boundary limitations

- The config directory is currently opened by its fixed absolute path under `C:\ProgramData`. The probe relies on the OS-protected parent directory; it does not yet pin the parent directory and open the config directory relative to that parent HANDLE. This must be addressed or explicitly accepted before Production.
- `NumberOfLinks == 1` is an admission-time observation. A later hard-link creation may still occur; the current share mode does not prove that future hard links are impossible.
- Elevated acceptance for the revised independent-config layout passed: peer-auth tests; positive read and invalid-request rejection; over-broad config and root DACL rejection; restricted-token write/ACL/metadata/child-create/image-write/hard-link denials; `BROKER_V2_PINNED_ROOT_ACCEPTANCE_PASS`; service/process/fixture/image/build-output cleanup verified.
- The real Windows overlapped-I/O cancellation/reaper lifecycle still needs kernel-backed tests; the PR #20 lifecycle model is synthetic.
- One fixed root and one read-only operation only. Not wired into `server.mjs` or `advanced-tools.mjs`; existing handlers remain unchanged.
- No second-host, different-user/session, full protocol, Supervisor/Worker lifecycle, crash/restart/tamper recovery, or provider portability acceptance.

Phase 1 remains IN PROGRESS / NOT ACCEPTED. Production remains NO-GO.

No runtime package imports this directory.

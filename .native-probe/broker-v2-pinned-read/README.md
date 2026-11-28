# Broker v2 pinned-root read-only vertical slice (test-only)

Date: 2026-11-27

Status: The independent-config revision passed elevated runtime acceptance on 2026-11-27. The follow-up change pins the `C:\ProgramData` parent HANDLE and opens the config directory relative to it; this revision also passed elevated runtime acceptance on 2026-11-28. This is not Production integration and does not complete Phase 1.

## Candidate design

- Disposable service identity: `RayaFsBrokerV2Probe20261127` / `NT SERVICE\RayaFsBrokerV2Probe20261127`.
- Workspace fixture: `C:\ProgramData\RayaFsBrokerV2Probe-20261127`.
- Independent service-owned policy directory: `C:\ProgramData\RayaFsBrokerV2ProbeConfig-20261127`.
- Protected broker image directory: `C:\ProgramData\RayaFsBrokerV2ProbeBin-20261127`.
- Service startup verifies the dedicated service SID and rejects enabled Backup, Restore, Take Ownership, or Debug privileges.
- The service opens and pins `C:\ProgramData` as a non-reparse directory HANDLE, then opens the config directory relative to that HANDLE with `NtCreateFile`, `RootDirectory`, `OBJ_DONT_REPARSE`, `FILE_OPEN_REPARSE_POINT`, and `FILE_DIRECTORY_FILE`. The config directory HANDLE is retained. The config directory must be a non-reparse directory owned by the service SID, with a protected DACL containing exactly three full-control allow ACEs: service SID, SYSTEM, and Administrators.
- `client.policy` is opened relative to the pinned config HANDLE. Its same file HANDLE is checked for regular-file properties, single-link admission, service-SID ownership, and the exact protected three-principal DACL before parsing.
- The workspace root is opened and pinned separately. Its protected DACL must contain exactly four allow ACEs: service SID, SYSTEM, Administrators (full control), and the authorized client SID (read-only).
- The workspace source is opened relative to the pinned workspace HANDLE using `NtCreateFile`, `OBJECT_ATTRIBUTES.RootDirectory`, `OBJ_DONT_REPARSE`, and `FILE_OPEN_REPARSE_POINT`. Its same HANDLE is admitted only if it is a regular non-reparse, non-delete-pending file with one link and a bounded size. The bytes are copied into a random-name snapshot created relative to the separately pinned config HANDLE, with an explicit protected DACL for only the service SID, SYSTEM, and Administrators. The snapshot HANDLE is validated for the same regular-file and single-link properties and its bytes are read back from that same HANDLE. The snapshot is marked for deletion before its HANDLE is closed; its name is never returned over the protocol.
- Protocol accepts only the fixed root ID `fixture`; caller-supplied paths and unsupported operations are rejected.
- Setup negative tests add an Everyone Modify ACE separately to the config directory and workspace root; both must cause service startup refusal. Restricted-token checks cover file write, WRITE_DAC, WRITE_OWNER, FILE_WRITE_ATTRIBUTES, directory FILE_ADD_FILE, broker-image GENERIC_WRITE, and hard-link creation denial.

## Named-pipe peer identity

- `GetNamedPipeClientProcessId` must provide a nonzero OS-reported peer PID. The PID is diagnostic only; the service does not open the client process or rely on cross-process process-query access.
- `ImpersonateNamedPipeClient` and `OpenThreadToken` obtain the actual pipe-client token. `TokenUser` and `TokenSessionId` from that token must match the configured SID/session.
- No identity field in the request frame is trusted. `RevertToSelf` is mandatory; if it fails, the service terminates rather than reusing an impersonating worker thread.
- The local `--peer-auth-test` exercises the same authentication function over a real local named pipe: authorized SID/session accepted, wrong SID and wrong session rejected. Nine checks passed in the last local run.

## Trust-boundary limitations

- The parent directory is now pinned before the config directory is opened relative to its HANDLE. The parent is checked as a disk directory and rejected if it is a reparse point or delete-pending. The config directory is separately checked for its exact protected DACL. The parent-relative open path passed compilation, local peer-auth tests, and elevated service acceptance on 2026-11-28, including positive read, invalid-request rejection, over-broad config/root DACL rejection, restricted-token denials, and verified cleanup.
- Windows `FileLinkInformation` does not require specific access rights on its source HANDLE. Therefore the source workspace file link-count check is explicitly admission-only. The broker no longer returns bytes read from that source object: it reads from a separate random-name, service-private snapshot whose protected DACL and unexposed name prevent the authorized client from obtaining a handle to create an external hard link. The snapshot itself is checked for `NumberOfLinks == 1` on the same HANDLE used for the read. SYSTEM and Administrators remain trusted principals.
- The parent-relative config-open revision passed elevated acceptance on 2026-11-28: peer-auth integration; positive read and invalid-request rejection; over-broad config and root DACL rejection; restricted-token write/ACL/metadata/child-create/image-write/hard-link denials; `BROKER_V2_PINNED_ROOT_ACCEPTANCE_PASS`; service/process/fixture/image/build-output cleanup verified.
- Overlapped I/O cancellation now has a bounded 5-second drain. `CancelIoEx` is followed by a wait for the operation event and `GetOverlappedResult`; the stack-owned `OVERLAPPED`, event, buffer, and pipe HANDLE are not released until completion is observed. If the kernel does not complete within the drain bound, the disposable broker process terminates rather than freeing resources still potentially referenced by the kernel. `--io-cancel-test` exercises a genuinely pending named-pipe read and cancellation on Windows. This is a fail-stop ownership strategy, not a general-purpose in-process reaper.
- One fixed root and one read-only operation only. Not wired into `server.mjs` or `advanced-tools.mjs`; existing handlers remain unchanged.
- No second-host, different-user/session, full protocol, Supervisor/Worker lifecycle, crash/restart/tamper recovery, or provider portability acceptance.

Phase 1 remains IN PROGRESS / NOT ACCEPTED. Production remains NO-GO.

No runtime package imports this directory.

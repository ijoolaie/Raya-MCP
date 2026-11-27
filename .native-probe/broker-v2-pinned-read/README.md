# Broker v2 pinned-root read-only vertical slice (test-only)

Date: 2026-11-27

Status: native source and harness prepared; service runtime acceptance is BLOCKED by the current non-elevated Windows token. No Production integration.

## Implemented in this isolated candidate

- New disposable service identity/name: `RayaFsBrokerV2Probe20261127` / `NT SERVICE\\RayaFsBrokerV2Probe20261127`.
- Unique protected fixture and image paths under `C:\\ProgramData\\RayaFsBrokerV2Probe-20261127*` and `C:\\ProgramData\\RayaFsBrokerV2ProbeBin-20261127`.
- At service startup, query the process token and require the dedicated service SID as TokenUser or an enabled TokenGroups SID. Fail closed if Backup, Restore, Take Ownership, or Debug privileges are enabled.
- Open the configured root once, validate directory type/reparse status, verify its owner is the dedicated service SID, and require a protected non-null DACL. Keep the root HANDLE for the service lifetime.
- Read the client policy and payload relative to the pinned root HANDLE using `NtCreateFile` / `OBJECT_ATTRIBUTES.RootDirectory`, `OBJ_DONT_REPARSE`, and `FILE_OPEN_REPARSE_POINT`.
- Validate the same opened regular-file HANDLE for type, reparse status, deletion state, size cap, and `NumberOfLinks == 1` before reading.
- Protocol accepts only a fixed root ID (`fixture`); no caller-supplied path is accepted. Unsupported operation and absolute-path request cases are rejected.
- Test-only setup/cleanup scripts create a uniquely named virtual-account service and protected disposable fixture, attempt root-path replacement while the service is running, and verify cleanup.
- A restricted-token security-negative harness checks write, WRITE_DAC, WRITE_OWNER, FILE_WRITE_ATTRIBUTES, directory add-file/subdirectory, image modification, and hard-link creation denial.

## Validation on 2026-11-27

- MSVC x64 build of the broker, service-account configurator, and restricted-token negative harness passed with `/W4 /WX`.
- PowerShell setup and cleanup scripts passed syntax parsing.
- Attempting to create the disposable service returned SCM `OpenSCManager FAILED 5: Access is denied` before service registration or ProgramData fixture creation.
- `whoami /groups` showed `BUILTIN\\Administrators` as **Deny only** and Medium Mandatory Level; the current runner token is not elevated.
- Cleanup verification passed: service absent, broker process absent, fixture/renamed fixture/image paths absent, and build outputs absent.

## Required next validation

Run the following from an elevated PowerShell session (Run as Administrator) in this directory:

1. `.\build.cmd`
2. `.\setup.ps1`
3. After collecting the exact results, `.\cleanup.ps1`

Do not run setup without ensuring cleanup can run with the same elevated token. The scripts refuse to overwrite pre-existing service/path names and use only the uniquely named disposable v2 artifacts above.

## Limits

- The broker supports only one read-only fixture root and one read operation. It is not the complete 15-operation Phase 1 adapter.
- Service runtime, actual virtual service token, protected DACL, hard-link denial, root-path replacement against the live service, and cleanup have not yet been tested because SCM access was denied.
- No second-host remote-client test, different-user/session test, full RY2R protocol, Supervisor/Worker lifecycle integration, or provider portability proof.
- This is not wired into `server.mjs` or `advanced-tools.mjs`; Production handlers remain unchanged.
- Phase 1 remains IN PROGRESS / NOT ACCEPTED; Production remains NO-GO.

No runtime package imports this directory.

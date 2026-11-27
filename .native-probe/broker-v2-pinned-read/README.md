# Broker v2 pinned-root read-only vertical slice (test-only)

Date: 2026-11-27

Status: elevated single-host runtime acceptance PASSED for this isolated test candidate. This is not Production integration and does not complete Phase 1.

## Implemented in this isolated candidate

- Disposable service identity/name: `RayaFsBrokerV2Probe20261127` / `NT SERVICE\\RayaFsBrokerV2Probe20261127`.
- Unique protected fixture and image paths under `C:\\ProgramData\\RayaFsBrokerV2Probe-20261127*` and `C:\\ProgramData\\RayaFsBrokerV2ProbeBin-20261127`.
- At service startup, query the process token and require the dedicated service SID as TokenUser or an enabled TokenGroups SID. Fail closed if Backup, Restore, Take Ownership, or Debug privileges are enabled.
- Open the configured root once, validate directory type/reparse status, verify its owner is the dedicated service SID, and require a protected non-null DACL. Keep the root HANDLE for the service lifetime.
- Read the client policy and payload relative to the pinned root HANDLE using `NtCreateFile` / `OBJECT_ATTRIBUTES.RootDirectory`, `OBJ_DONT_REPARSE`, and `FILE_OPEN_REPARSE_POINT`.
- Validate the same opened regular-file HANDLE for type, reparse status, deletion state, size cap, and `NumberOfLinks == 1` before reading.
- Protocol accepts only a fixed root ID (`fixture`); no caller-supplied path is accepted. Unsupported operation and absolute-path request cases are rejected.
- Test-only setup/cleanup scripts create a uniquely named virtual-account service and protected disposable fixture, attempt root-path replacement while the service is running, and verify cleanup.
- A restricted-token security-negative harness checks write, WRITE_DAC, WRITE_OWNER, FILE_WRITE_ATTRIBUTES, directory add-file/subdirectory, image modification, and hard-link creation denial.

## Elevated validation on 2026-11-27

- MSVC x64 build passed with `/W4 /WX` for the broker, service-account configurator, and security-negative harness.
- Disposable Windows service was created and reached `Running` under the expected virtual service account; service process ExitCode was 0.
- Positive protocol test passed: read from configured root returned the expected fixture bytes.
- Negative protocol tests passed: unknown root, unsupported operation, and caller-supplied absolute path were rejected.
- Restricted-token checks passed: fixture FILE_WRITE_DATA, WRITE_DAC, WRITE_OWNER, FILE_WRITE_ATTRIBUTES, directory FILE_ADD_FILE/FILE_ADD_SUBDIRECTORY, broker image GENERIC_WRITE, and fixture hard-link creation were denied.
- Adversarial root-path replacement passed: while the service retained its root HANDLE, the original root path was renamed and a decoy directory/file was created at the old path; the service continued to return the original fixture content.
- Runner reported `BROKER_V2_PINNED_ROOT_ACCEPTANCE_PASS` and `BROKER_V2_ACCEPTANCE_TESTS_COMPLETED`.
- Cleanup verification passed: service absent, broker process count 0, fixture/renamed fixture/image paths absent, and build outputs absent.

## Limits / not yet validated

- One fixed fixture root + one read-only operation only; not the complete 15-operation Phase 1 adapter.
- This is not wired into `server.mjs` or `advanced-tools.mjs`; existing Production handlers remain unchanged.
- No second-host remote-client test, different-user/session test, full RY2R protocol, Supervisor/Worker lifecycle integration, crash/restart/tamper recovery, or provider portability proof.
- No real workspace root/ACL migration and no persistent Production service deployment.
- The native harness is a research prototype, not an independently security-reviewed Production component.
- Phase 1 remains IN PROGRESS / NOT ACCEPTED; Production remains NO-GO.

No runtime package imports this directory.

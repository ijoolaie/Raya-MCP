# Supervisor named-pipe security probes (test-only)

Date: 2026-11-27

Status: isolated Windows test harness; no service, Broker, or Production wiring.

## Coverage

The harness creates uniquely named, disposable local named pipes and tests:

1. **DACL denial:** the pipe DACL grants `Authenticated Users` and `SYSTEM`. A restricted impersonation token derived from the current process token disables the `Authenticated Users` group. A client open using that token must fail with `ERROR_ACCESS_DENIED` (5). This exercises the actual named-pipe object access check without creating another Windows account or changing machine policy.
2. **Local-client control:** a second pipe grants `Everyone` and `SYSTEM`; a local client must connect successfully. This control is required before interpreting the remote-path test.
3. **Remote-client rejection:** the second pipe is created with `PIPE_REJECT_REMOTE_CLIENTS`. After the local control connection is disconnected, the harness arms a new server connection and tries the same pipe through the host's UNC path (`\\COMPUTERNAME\pipe\...`). The expected result is `ERROR_ACCESS_DENIED` (5).

The harness uses a bounded overlapped `ConnectNamedPipe` wait, unique PID/tick-based pipe names, non-inheritable server handles, and no filesystem verbs or payload transfer. It does not create persistent files, users, services, or ACL changes.

## Build and run

From this directory, run `build-supervisor-pipe-security-v2-test.cmd`.

The script discovers Visual Studio C++ Build Tools through `vswhere.exe`, compiles x64 with `/W4 /WX`, launches ten fresh test processes, then removes the `.exe` and `.obj` outputs.

## Evidence and limitations

Validation completed on one Windows x64 host on 2026-11-27: the MSVC x64 build passed with `/W4 /WX`; ten fresh-process runs each reported 7 PASS, 0 FAIL, 0 SKIP (70 assertions total). The prescribed script removed the generated executable and object file after the run.


- A successful restricted-token denial proves this DACL configuration denied the test token; it does not validate the future dedicated service SID, service token, or production SDDL.
- A successful UNC loopback denial exercises the remote-client rejection path as presented by this host's SMB/redirector stack. It is **not** a substitute for a second physical/virtual Windows host attempting a true remote connection. If the UNC attempt returns a transport/setup error other than access denied, the harness reports SKIP rather than claiming the pipe flag was proven.
- The `Everyone` DACL is deliberately used only on a random disposable test pipe so a remote attempt is not rejected by the DACL before `PIPE_REJECT_REMOTE_CLIENTS` is evaluated. It must never be copied into a production configuration.
- No service SID provisioning, named-pipe production ACL, process peer-PID/token authentication, session check, supervisor restart, Job Object binding, RY2R framing, or Broker integration is covered.
- The protocol design remains design-only. Production remains NO-GO; Phase 1 remains NOT ACCEPTED.

No runtime package imports this directory.

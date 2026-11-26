# Security and Repository Hygiene

Never commit tokens, API keys, passwords, private certificates, DPAPI material, local config, user data, panel token files or environment dumps. Admin Panel `panel-config.json` is intentionally excluded.

The local server runs with the Windows account's permissions; it is not an OS sandbox. Workspace allowlisting and application path checks do not replace Windows ACLs or process isolation.

Treat path-based checks as potentially race-prone unless handle-relative and identity-pinned behavior is implemented and verified. Experimental probes are research evidence, not a production security guarantee.

Do not expose local HTTP or the Admin Panel to untrusted networks. Keep panel tokens private and rotate them if exposed.

Before adopting prototype code or dependencies, verify authorship, license, provenance, threat model and compatibility. The external Tunnel Client is not included here.

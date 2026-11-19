# RA/RD metadata and content-read ACL matrix — 2026-11-02

## Purpose

Characterize the isolated pinned native prototype's `inspectChildFromParent` metadata path and `readChildFromParent` content path under explicit per-file DACL deny ACEs. This is a disposable local NTFS experiment, not Production acceptance.

## Environment and scope

- Executed by the user from a non-elevated Medium-integrity PowerShell session.
- Current user SID: `S-1-5-21-2295999303-425339182-2047630047-1001`.
- Each scenario used a separate disposable temporary NTFS file.
- The exact explicit deny ACE was visible in the `icacls` output for the current user.
- The test removed the temporary deny ACEs and deleted the fixtures.
- HANDLE count was 189 before and after; no HANDLE-count delta was observed.
- No workspace, user-profile, or global ACL was changed.

## Results

| DACL deny ACE | Metadata inspection | Content read |
|---|---|---|
| `RA` only | Succeeded despite explicit deny | Succeeded |
| `RD` only | Succeeded | Failed with `STATUS_ACCESS_DENIED (0xC0000022)`; no content returned |
| `RA,RD` | Succeeded despite explicit deny | Failed with `STATUS_ACCESS_DENIED (0xC0000022)`; no content returned |

For the RA-only case, the test returned the canary `RA-RD-CONTENT-CANARY` through the content-read operation, as expected because READ_DATA was not denied. For both cases containing an RD deny, the content-read operation returned no content and reported access denied. After restoring each ACL, both operations succeeded and the content matched the canary.

## Assessment

1. The tested `readChildFromParent` path correctly enforced explicit READ_DATA denial in both the RD-only and RA+RD cases on this host.
2. The tested `inspectChildFromParent` path did not enforce an explicit READ_ATTRIBUTES deny. This reproduces the previously observed metadata ACL anomaly under Medium integrity using the actual isolated native prototype.
3. This is a metadata-authorization acceptance failure, not evidence that content-read ACL enforcement is bypassed.
4. Metadata exposure remains relevant: the inspection path returns file identity and size and uses attribute/tag metadata to reject directories and reparse points. The RA-deny result must therefore remain unresolved / NOT ACCEPTED; do not treat it as harmless or as a passed ACL gate.
5. The result is specific to the tested prototype, token, local NTFS host, and DACL fixtures. It does not establish that Windows or all filesystems generally ignore READ_ATTRIBUTES deny ACEs.

## Source-level correlation

In `.native-probe/handle-relative-probe-phase1-tail.c`, `inspect_child_from_parent` opens the child with `FILE_READ_ATTRIBUTES | SYNCHRONIZE`, then queries `FileIdInfo`, `FileAttributeTagInfo`, and file size. The separate `read_child_from_parent` path requests `FILE_READ_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE`; the live matrix shows that its READ_DATA request is denied when the DACL denies RD.

Earlier isolated NTFS rights-isolation probes found that allowing SYNCHRONIZE while denying RA correlated with successful RA opens, whereas allowing RD without SYNCHRONIZE did not. That is a useful lead for further investigation, not a proven root cause. Do not weaken or remove metadata checks as a workaround.

## Decision

- Content READ_DATA denial for this tested path: PASS, limited to this host and test.
- Metadata READ_ATTRIBUTES denial for `inspectChildFromParent`: FAIL / UNRESOLVED.
- Overall filesystem/ACL acceptance: NOT ACCEPTED.
- Production integration: NO-GO.
- No Production code or runtime configuration was changed by this experiment.

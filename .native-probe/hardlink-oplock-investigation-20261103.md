# Hard-link exclusion investigation — 2026-11-03

## Scope and safety
All experiments were performed against disposable files on the local NTFS test volume. No Production handlers, workspace ACLs, ownership, service identities, runtime configuration, roots, services, or tunnels were changed. The separate IranPriceScout tunnel was not touched.

Production remains **NO-GO**. Phase 1 remains **NOT ACCEPTED**.

## Results

### Existing-link admission guard
The isolated candidate `handle-relative-hardlink-guarded-rootapi.c` checks regular-file link count and fails closed when the count is not exactly one (or the required metadata is unavailable). The independent test `handle-relative-hardlink-guarded-rootapi-test.cjs` passed, including normal single-link operations and rejection of pre-existing outside-backed hard links. It passed 10 consecutive repetitions.

This is an admission/operation-time check only. It does not establish a race-free invariant against another process creating a hard link after the check and before the protected operation commits.

### RWH oplock
Probe: `hardlink-oplock-rwh-probe-20261103.c`.

The RWH oplock request was granted. In the tested NTFS scenario, a child process successfully created an additional hard link while the oplock handle remained open; no break notification arrived during the five-second observation window. The probe exited 97 because its acceptance condition expected the child to be blocked. This is a negative security result, not a build failure.

### Filter oplock
Probe: `hardlink-oplock-filter-probe-20261103.c`.

The Filter oplock request was granted. In the tested NTFS scenario, a child process successfully created an additional hard link while the oplock handle remained open; no break notification arrived during the five-second observation window. The probe exited 97 because its acceptance condition expected the child to be blocked. This is a negative security result, not a build failure.

### Other tested mechanisms
Prior experiments found that share-none does not prevent the tested cross-process hard-link creation, and a simple per-file DACL denial of FILE_WRITE_ATTRIBUTES is not a sufficient boundary when the same identity can rewrite the DACL (including obtaining WRITE_DAC).

## Security conclusion
Under the tested same-user NTFS model, none of the tested timing or per-file mechanisms establishes the required hard-link exclusion guarantee:
- share modes;
- a simple per-file DACL denial;
- RWH oplock;
- Filter oplock;
- NumberOfLinks checks alone.

Do not describe the current same-identity design as race-free or as enforcing an invariant that it does not prove.

## Viable architectural direction (proposal only)
A credible next direction is a separately authorized filesystem broker running under a dedicated service identity, with protected ownership/DACLs that the interactive user cannot rewrite or bypass. This would change ownership and access semantics and is **not authorized** by the prior approval. No service identity, ownership, ACL, service, or runtime changes may be implemented without separate explicit approval.

Any future isolated design should test normal workflows, upgrades, backup/restore, ACL inheritance, cleanup, crash recovery, and all file-operation families under the split-identity model.

## References
- Microsoft: FSCTL_REQUEST_OPLOCK — https://learn.microsoft.com/en-us/windows/win32/api/winioctl/ni-winioctl-fsctl_request_oplock
- Microsoft: FSCTL_REQUEST_FILTER_OPLOCK — https://learn.microsoft.com/en-us/windows/win32/api/winioctl/ni-winioctl-fsctl_request_filter_oplock
- Microsoft: IRP_MJ_SET_INFORMATION — https://learn.microsoft.com/en-us/windows-hardware/drivers/ifs/irp-mj-set-information2
- Microsoft: Granting Oplocks — https://learn.microsoft.com/en-us/windows-hardware/drivers/ifs/granting-oplocks

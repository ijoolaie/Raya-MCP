# NtCreateFile vs Win32 READ_ATTRIBUTES ACL isolation — 2026-11-02

## Purpose

Determine whether the earlier correlation between SYNCHRONIZE allowance and successful metadata opens is causal, using per-file isolated DACLs and side-by-side Win32 CreateFileW / native NtCreateFile opens.

## Harness

PowerShell script: `.native-probe/metadata-acl-allow-rights-isolation-20261102.ps1`.

Seven disposable NTFS files were created. Each received a protected DACL containing:
- explicit DENY `FILE_READ_ATTRIBUTES` (0x80) for the current user;
- optionally one ALLOW ACE for the current user (none, SYNCHRONIZE, READ_DATA, READ_CONTROL, WRITE_ATTRIBUTES, READ_ATTRIBUTES, or SYNCHRONIZE|READ_DATA);
- ALLOW Full Control for SYSTEM.

For each file, AccessCheck evaluated RA, Win32 CreateFileW requested RA and RA|SYNCHRONIZE, and direct NtCreateFile requested RA and RA|SYNCHRONIZE. NtQueryObject reported the granted access mask on each successful handle.

## Results from the Raya MCP process

The process token was High integrity. This is a harness smoke/characterization run, not a substitute for the user's Medium-integrity acceptance run.

Across all seven DACL variants:
- AccessCheck for RA returned denied with granted mask 0.
- Direct NtCreateFile requesting RA only succeeded and returned a handle whose GrantedAccess was exactly 0x00000080, even when the user had no ALLOW ACE at all.
- Direct NtCreateFile requesting RA|SYNCHRONIZE succeeded only in scenarios where SYNCHRONIZE was explicitly allowed; otherwise it returned STATUS_ACCESS_DENIED.
- Win32 CreateFileW requesting RA or RA|SYNCHRONIZE failed with Win32 error 5 when SYNCHRONIZE was not allowed.
- Win32 CreateFileW succeeded when SYNCHRONIZE was allowed; its handle's GrantedAccess was 0x00100080.

This means the earlier apparent SYNCHRONIZE correlation needs refinement: the native RA-only open succeeded even without a SYNCHRONIZE allowance. SYNCHRONIZE allowance explains whether the combined RA|SYNCHRONIZE request can open, but does not explain why RA itself is granted by the direct NtCreateFile path despite AccessCheck denying it.

## Interpretation and limits

There is now a concrete behavioral discrepancy on this local NTFS host between direct NtCreateFile and Win32 CreateFileW for an RA-only request under an explicit RA deny:
- AccessCheck says RA denied.
- Win32 CreateFileW fails.
- Direct NtCreateFile succeeds and the returned handle reports RA granted.

The Microsoft NtCreateFile documentation describes FILE_READ_ATTRIBUTES as a requested file access right and describes NtCreateFile as returning an appropriate NTSTATUS on failure. The Microsoft file-security documentation says file access is controlled by the object's security descriptor and requested access rights. The documentation reviewed does not explain this observed difference. Do not infer a general Windows rule from this single host.

The native prototype requests RA|SYNCHRONIZE, not RA alone. The tested result is consistent with the observed prototype behavior when SYNCHRONIZE is allowed, but this test was run under High integrity and used an absolute path. The prototype's RootDirectory-relative path and Medium-integrity behavior still need the matching controlled run.

## Next acceptance action

Run this same script from the known non-elevated Medium-integrity PowerShell session and compare:
- integrity must show `S-1-16-8192`;
- each AccessCheck result should remain denied for RA;
- compare Win32 and native opens / actual GrantedAccess across all seven variants.

Until that Medium run is reviewed, this is a strong lead, not a closed root cause. Do not change the prototype or Production code based on this characterization alone.

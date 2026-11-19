# Old response with new requests — historical evidence recovery — 2026-11-14

## Scope

Read-only recovery for `WitnessV2AppendEntriesOldResponseWithNewRequests.tla` and its same-name config. No TLC run was started; no model/config/source files were changed.

## Evidence found

- Model: `WitnessV2AppendEntriesOldResponseWithNewRequests.tla`, 3,639 bytes; SHA-256 `F2C867C586F91AAF7D048377F88BE3D560A5766D537E30B693C2CC4F61D4A19E`.
- Config: `WitnessV2AppendEntriesOldResponseWithNewRequests.cfg`, 239 bytes; SHA-256 `621BBEA4472C73683DFDBCF74627D0D61E5C9F9F2C5A42684DE270BFFBD29963`.
- A TLC state directory exists at `validation/old-response-with-new-requests-20261113/26-11-13-12-07-14.641`, timestamped 2026-11-13 12:07:14.
- It contains four expected state/fingerprint files (`.st`, `-0.st`, `_0.fp`, `_1.fp`), all zero bytes. Total payload: 0 bytes.
- Recursive filename and text/log search found no TLC output, run log, archived report, or other execution record for this exact model basename. The archive run/file manifests also contain no matching record.

## Result classification

**Historical execution result: NOT RECOVERED / UNKNOWN.** The empty TLC state files prove only that a metadata directory was created; they do not prove that TLC completed, passed, failed, or explored any states. No PASS/FAIL claim is made for this exact model.

## Related completed evidence (not interchangeable)

Two related, separately identified witnesses have documented PASS results:

1. `WitnessV2AppendEntriesRequestIdCollisionAcrossTerms.tla` — report `REQUESTID-COLLISION-ACROSS-TERMS-RECHECK-20261113.md`; PASS, 5 generated / 4 distinct states, 6 invariants. Model SHA-256 `CCEBC5515BBA9FDAD2C408D2CBEF60CFE8D70A6B018F75251954EB5ABA0F2D9E`.
2. `WitnessV2AppendEntriesOldResponseWithTwoNewRequests.tla` — report `OLD-RESPONSE-WITH-TWO-NEW-REQUESTS-RECHECK-20261113.md`; PASS, 50 generated / 26 distinct states, 8 invariants. Model SHA-256 `599E7137018CD581B89FA378E22AEEDA5BBE7A270328C1BE075F4F07D3A1E55D`.

These are stronger/related test cases, but neither is the exact target model and neither retroactively establishes its historical result.

## Disposition

Keep the target model, config, and zero-byte TLC metadata directory as historical evidence. Do not delete or relabel them. If an exact result is required, perform a separately recorded bounded revalidation only after checking available disk headroom and preserving the existing evidence.

## Safety and workspace

- Working copy only: `D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`.
- No files deleted or moved; no TLC execution; no commits or resets.
- Production remains NO-GO; Phase 1 remains NOT ACCEPTED.

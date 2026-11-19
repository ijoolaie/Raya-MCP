# Adapter Structured Destroy Result — 2026-11-17

## Scope

Implemented only in the Raya MCP Working Copy:
`D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`

This change makes Adapter Destroy's irreversible admission-close effect distinguishable from successful object destruction. It does not invoke Submitter or Worker lifecycle APIs and does not change component ownership.

## API

Added `raya_integrated_adapter_destroy_ex` and `raya_adapter_destroy_result`, retaining the legacy Boolean `raya_integrated_adapter_destroy` as a wrapper.

The structured result reports:
- status: SUCCESS, BUSY, or INVALID;
- effect: NONE, ADMISSION_CLOSED, or DESTROYED;
- retry class: NONE or RECONCILE;
- step: 1 (pre-existing API entrants must drain), 2 (active/retained envelopes remain or retained metadata needs diagnosis), or 3 (destroy completed);
- observed entrant, active-envelope, and retained-envelope counts;
- admission-closed and lock-present indicators.

Semantics:
- Admission is closed atomically and permanently before drain checks.
- Existing entrants produce BUSY + ADMISSION_CLOSED + RECONCILE; no Adapter lock is touched until the entrant count reaches zero.
- Active or retained envelopes produce BUSY + ADMISSION_CLOSED + RECONCILE, with counts.
- A retained-list/count disagreement is INVALID and marked RECONCILE; it is not silently treated as empty.
- Only a zero-entrant, zero-active, zero-retained state allows deletion of the critical section and zeroing of Adapter storage. The result is then DESTROYED.
- Caller must serialize Destroy calls, preserve Adapter storage while any possible caller exists, and prevent calls after successful destruction. The API does not establish external caller quiescence by itself.

The legacy Boolean wrapper returns true only for structured SUCCESS. Existing call sites remain source-compatible.

## Tests and verification

Built and ran `build-integrated-submit-adapter-v2-api-test.cmd`.

- MSVC x64 compile with `/W4 /WX`: PASS.
- 10 consecutive test executions: PASS.
- Each execution: 751 assertions across 66 cases.
- Aggregate: 7,510 assertions across 10 runs.
- New tests cover entrant-drain classification, active-envelope classification, retained-envelope classification, admission-close effect reporting, and final successful destruction.
- The existing concurrent-submit entrant test now verifies the structured result reports all 8 entrants and the irreversible admission-close effect.
- Build-script-owned temporary EXE/OBJ artifacts remaining after the run: 0.
- No `cl`, `link`, `java`, or `tlc` processes remained at final inspection.

No cleanup was performed on unrelated pre-existing `.obj`/`.exe` artifacts in the working directory.

## Changed files and SHA-256

- `integrated-submit-adapter-v2.h` — `DB52B1D6723DCA1159AD8737B83E64EFE26FF4D5F4053A32F9D1C8CD6C0AA1DC`
- `integrated-submit-adapter-v2.c` — `A9B39D0D4094E8418F820E53B3E28FEE64FC7B0A9EC417450F25EC422EE8D87F`
- `integrated-submit-adapter-v2-api-test.c` — `9F62641155FF413A69DEA3DDDCBD26DF1B062184B6ADE359BF9F42B05C1A142B`

## Boundaries and remaining work

- Submitter and Worker are still not wired into the Orchestrator cleanup cursor.
- Before Submitter teardown, the lifecycle owner must separately establish that all direct Submitter callers are quiesced and serialize Shutdown/Join/Destroy retries.
- Adapter Destroy does not stop, join, or destroy Submitter or Worker.
- No startup rollback integration, full chained failure matrix, durable cross-process recovery, or full Raft proof is claimed.
- Production remains NO-GO; Phase 1 remains NOT ACCEPTED.
- No Production/source-project files, README, IranPriceScout, or independent port 8765 tunnel were touched.
- No commit or reset was performed.

## Environment

Final observed free space:
- C: 6,363,111,424 bytes
- D: 2,111,803,392 bytes

These values are point-in-time observations only.

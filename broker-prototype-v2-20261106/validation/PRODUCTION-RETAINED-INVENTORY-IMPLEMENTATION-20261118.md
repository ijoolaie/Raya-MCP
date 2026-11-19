# Production Retained Inventory v1 — Implementation and Focused Verification

**Date:** 2026-11-18  
**Workspace:** Working Copy only  
**Roadmap gate:** Stage 5 / Gate C  
**Disposition:** IMPLEMENTED; focused verification PASS. This is read-only inventory capability only, not reconciliation or recovery acceptance.

## Scope

Implemented the frozen v1 additive ABI as two independent production APIs:
- Adapter-envelope copied inventory.
- Worker-operation copied inventory.

No combined cross-plane snapshot, mutating reconciliation, release, retry, callback retry, or I/O replay was added. Existing public object layouts were not changed.

## Source behavior

Both APIs:
- require the exact v1 requested version and caller-owned bounded output arrays;
- initialize required_count to zero on entry when the pointer is valid;
- reject invalid arguments and uninitialized objects with distinct status values;
- reject non-quiescent calls without changing lifecycle state;
- detect retained-list cycles and stored-count mismatches;
- validate first-retain evidence and the DIRECT / ADAPTER_ASSOCIATED / identity-less partial record shape before copying;
- return INSUFFICIENT_CAPACITY with the full required count and write no partial output;
- copy all records by value on success, including struct_size and version;
- perform no heap allocation and expose no pointers, HANDLEs, raw addresses, buffers, or Submitter mutable state.

Adapter quiescence is source-backed and checked: Adapter admission closed with zero entrants; Worker joined with null thread and zero caller-active count; Submitter admission closed with zero API entrants and zero queue, active-producer, and active-issue counts; and Adapter active_count equals retained_count. The last condition is necessary because retained envelopes remain included in active_count until freed. The API takes component locks sequentially, never nested.

Worker quiescence is checked by joined state, null thread, and zero Worker caller-active count. The Worker list is copied under the Worker lock only.

## Focused verification

### Standard prescribed API script

- Visual Studio 2026 BuildTools 18.7.3, x64, /W4 /WX.
- Build: PASS. The first attempt exposed that the Adapter API definition was inside the test-only compilation guard; it was moved into the production compilation unit and the prescribed script was rerun successfully.
- Prescribed script executed 10 times; each execution passed 977 assertions across 66 cases.
- Total: 9,770 assertion executions, zero failures.

### Testing-hook contract build

- Explicit /DRAYA_ADAPTER_TESTING build using the same source set and /W4 /WX.
- One focused execution: PASS, 1,186 assertions across 68 cases.
- A first hook execution exposed one stale expected Worker snapshot count after the new identity-less partial-record fixture was added; the expected count and capacity were corrected. The final hook execution passed.

### New API coverage

- Adapter snapshot rejects a non-quiescent Submitter admission gate and leaves required_count zero.
- Adapter and Worker insufficient-capacity calls return the complete required count and leave sentinel output records untouched.
- Unsupported Adapter record version is rejected.
- Successful Adapter and Worker copied snapshots return correct struct_size/version and stable IDs.
- Worker snapshot preserves DIRECT, ADAPTER_ASSOCIATED, and identity-less PARTIAL_RECORD entries distinctly.
- Adapter/Worker correlation succeeds only through the explicit Adapter identity pair.
- Inventory counts and Worker retained-list head remain unchanged after snapshot calls.

## Artifact audit

- Standard build script removed its exact 15 declared EXE/OBJ outputs; all were absent afterward.
- Testing-hook build removed its exact 15 generated EXE/OBJ outputs; all were absent afterward.
- Fourteen pre-existing root EXE/OBJ artifacts were preserved:
  gate_poc.obj; owner_cursor_race.obj; pipe-io-v2.obj; protocol-v2-test.exe; protocol-v2-test.obj; protocol-v2.obj; reaper-waitset-v2-test.exe; reaper-waitset-v2-test.obj; response-v2-test.exe; response-v2-test.obj; response-v2.obj; session-v2-test.exe; session-v2-test.obj; session-v2.obj.
- No checkpoints, traces, model states, configs, reports, archives, or state stores were deleted.

## Files changed

Production API implementation:
- integrated-submit-adapter-v2.h
- integrated-submit-adapter-v2.c
- reaper-managed-worker-v2.h
- reaper-managed-worker-v2.c

Focused contract tests:
- integrated-submit-adapter-v2-api-test.c

Governance/evidence:
- PROJECT-ROADMAP.md
- validation/PRODUCTION-RETAINED-INVENTORY-READ-ONLY-CONTRACT-DECISIONS-20261118.md
- validation/PRODUCTION-RETAINED-INVENTORY-PUBLIC-HEADER-LIFETIME-REVIEW-20261118.md
- this report

No README, build script, TLA+ model, or configuration changed. No commit/reset, install/download, or production deployment occurred.

## PR governance

The connected private repository was identified as ijoolaie/Raya-MCP. Its five open PRs (#1–#5) were enumerated, with changed-file lists, review submissions, review threads, comments, and PR-triggered workflow runs checked. None changes the native broker files in this report. PR #1's Windows / Node.js 22 CI job passed; PRs #2–#5 have no PR workflow runs in the current audit; all five have no submitted reviews, comments, or review threads. PR #2, #3, and #4 are drafts. The remote repository roadmap concerns the separate Node bridge filesystem-confinement Phase 1, not this local broker prototype's seven-stage plan.

The local Working Copy has no Git metadata and no matching Stage 5 PR exists. No commit or PR was created. Therefore Stage 5 PR review remains BLOCKED; unrelated PRs #1–#5 are not review evidence for this native code.

## Stage 5 disposition

- Gate B — v1 read-only ABI freeze: COMPLETE.
- Gate C — production read-only copied inventory: COMPLETE; focused verification PASS.
- Gate D — reconciliation action decision: COMPLETE; no safe action proven; fail-closed quarantine for the current process lifetime. See STAGE-5-RECONCILIATION-ACTION-DECISION-20261118.md.
- Gate E — positive recovery tests: BLOCKED because Gate D approved no action.
- Gate F — Stage 5 acceptance: BLOCKED pending explicit project-owner acceptance of quarantine as the Phase 1 limit and a matching PR review path.
- Stage 5 implementation/recovery acceptance: BLOCKED / NOT ACCEPTED.
- Phase 1: NOT ACCEPTED.
- Production: NO-GO.

No claim is made for durable identity across process restart, recipient-side WRITE commit, multi-retained shared-Worker reconciliation, or crash recovery.

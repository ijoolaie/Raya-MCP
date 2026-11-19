# Adapter Retained Snapshot — Submitter Effect Planes — 2026-11-18

## Scope

Working Copy only:
`D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`

This slice exposes the Submitter's existing per-request Issue and Publish effect states in the test-only retained snapshot. It does not duplicate or mutate the Submitter ledger, and does not create a production recovery interface.

## Source-grounded semantics

The existing `raya_submitter_request` already owns:
- `issue_effect`
- `publish_effect`

The Submitter initializes both to NOT_ATTEMPTED at admission, marks Issue IN_PROGRESS before invoking the Issue callback, and then records:
- Issue NOT_ATTEMPTED plus request NOT_ISSUED when the callback definitively reports not accepted;
- Issue UNKNOWN plus request UNCERTAIN when Issue outcome is uncertain;
- Issue COMMITTED and Publish IN_PROGRESS after accepted Issue;
- Publish UNKNOWN plus request UNCERTAIN when the Publish callback does not commit;
- Publish COMMITTED and request ISSUED when Publish succeeds.

These are Submitter-local callback/effect semantics. In the Adapter, Issue maps to the Win32 ReadFile/WriteFile callback; OS acceptance does not establish recipient application commit. Publish maps to Worker handoff; handoff is local ownership transfer, not recipient receipt.

## Changes

### Test-only retained snapshot
Added typed `issue_effect` and `publish_effect` fields to `raya_adapter_test_retained_item`. Snapshot reads them while holding the Submitter lock, alongside the existing request state. The existing snapshot already reports Owner, Finalizer, reservation and Worker state; this change makes the Submitter's two existing effect planes visible beside those observations.

### Validator
The test validator now:
- rejects effect values outside NOT_ATTEMPTED / IN_PROGRESS / COMMITTED / UNKNOWN;
- requires NOT_ISSUED requests to have both effects NOT_ATTEMPTED;
- requires ACKED requests to have both effects COMMITTED.

Synthetic validator fixture initializes the new fields. Real retained tests assert:
- post-terminal ACK failure: Issue and Publish are both COMMITTED;
- pre-issue finalizer-close failure: both effects remain NOT_ATTEMPTED.

No effect state is inferred from a Worker boolean. No existing Submitter state transition was modified.

## Validation

Visual Studio 2026 Build Tools 18.7.3, x64, `/W4 /WX /TC`.

- Test-seam build with `/DRAYA_ADAPTER_TESTING`: 903 assertions across 68 cases passed; exit 0.
- Normal configured build: 10 consecutive runs, each 751 assertions across 66 cases passed; total 7,510; exit 0.
- Test-seam cleanup removed 15 expected temporary executable/object artifacts; zero remained.
- Normal build left zero expected temporary artifacts.
- Final check found no expected temporary artifacts remaining.

## Files changed

- `integrated-submit-adapter-v2.h`: two typed effect fields in test-only retained snapshot structure.
- `integrated-submit-adapter-v2.c`: copy existing request effect states into snapshot under Submitter lock.
- `integrated-submit-adapter-v2-api-test.c`: range/consistency validation and real scenario assertions.

No Submitter implementation, Worker, Orchestrator, production Adapter lifecycle, README, build script, or configuration changed. No commit/reset.

## Boundaries and remaining gaps

- This is a read-only diagnostic exposure of two existing Submitter effect planes, not a new durable or production ledger.
- Worker completion and recipient-side receipt are not represented by these two fields.
- Existing Owner/Finalizer/Reservation fields are component-local observations; they do not constitute one atomic cross-component transaction snapshot.
- No durable identity, restart recovery, recipient idempotency key, receipt/status query, or safe retained reconciliation/release exists.
- Do not replay uncertain WRITE operations or free retained objects.

Phase 1 remains **NOT ACCEPTED**. Production remains **NO-GO**.

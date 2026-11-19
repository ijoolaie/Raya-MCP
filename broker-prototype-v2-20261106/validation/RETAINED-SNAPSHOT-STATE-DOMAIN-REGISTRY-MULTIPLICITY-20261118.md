# Retained Snapshot State-Domain and Registry Multiplicity Validation — 2026-11-18

## Scope
Extended the test-only retained snapshot validator in `integrated-submit-adapter-v2-api-test.c`. The change is diagnostic validation only; no production component behavior, resource release, retry, or replay path was changed.

## Added checks
- Reject owner states outside the declared CALLER_PENDING through RELEASED enum domain.
- Reject finalizer states beyond RELEASED.
- Reject integrated reservation states beyond TERMINAL_HELD.
- Reject more than one integrated-registry match for a single Adapter operation identity.
- Added negative regression cases for an out-of-domain reservation value and duplicate registry membership.

## Existing inventory relationship checks retained
The validator continues to:
- reject duplicate Adapter envelope and operation identities;
- reject duplicate Worker retained operation identities;
- require exactly one Worker retained-list match for Adapter entries classified as caller-side/pre-terminal-released retained;
- require no Worker retained-list match for Worker-owned pending-I/O entries;
- reject orphan Worker retained-list entries.

These checks compare operation identity membership only. They do not equate Adapter, Worker retained-list, integrated-registry, reservation-slot, or outstanding counters as if they were the same inventory. Those inventories represent different lifecycle stages and are sampled under separate locks.

## Validation
- Test-seam build with `/DRAYA_ADAPTER_TESTING`, Visual Studio 2026 Build Tools 18.7.3, x64, `/W4 /WX /TC`: 912 assertions across 68 cases passed; exit code 0.
- Normal build via `build-integrated-submit-adapter-v2-api-test.cmd`: 10 consecutive runs, each 751 assertions across 66 cases passed; total 7,510; exit code 0.
- Removed 15 exact test-seam executable/object outputs after the seam run.
- Final expected temporary artifact count: 0.
- Final observed free space: C 12.90 GB; D 4.48 GB.

## Changed file
- `integrated-submit-adapter-v2-api-test.c`: 48,936 bytes; SHA-256 `4FAC739A0D2F1F4770C100AD8ED598B663E09262106390050059846F64D8B5FA`.

## Limitations
- The snapshot is test-only and samples Adapter, Worker, and Submitter state under separate locks; it is not an atomic cross-component snapshot.
- Registry multiplicity is bounded per operation, but this does not prove recipient-side commit or durable recovery.
- No production retained enumeration, reconciliation, release, replay, or safe uncertain-WRITE retry was introduced.
- No README or build script changed. No commit or reset was performed.

# Submitter Request Identity and Effect Ledger — Slice 2026-11-17

## Scope
Working Copy only:
`D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`

This slice adds in-memory per-request identity and separate Issue/Publish effect states. It is diagnostic state, not a durable journal or recovery authorization.

## Data model
- `request_id`: nonzero ULONGLONG assigned under the Submitter lock at admission.
- IDs increase monotonically within one Submitter instance. Zero is invalid; after assigning the maximum ULONGLONG, the next ID becomes zero and further admission is rejected rather than wrapping/reusing an ID.
- `issue_effect` and `publish_effect` are independent:
  - NOT_ATTEMPTED
  - IN_PROGRESS
  - COMMITTED
  - UNKNOWN
- The request structure documents that Submitter owns these fields after admission and callbacks must not modify them.
- First-failure diagnostics now additionally capture request ID and both effect states when the first DEGRADED transition is request-scoped. Lifecycle-only failures retain request_id=0.
- Request-scoped capture is wired for worker Issue/Publish/notification failures and Enqueue, Abort Admission, Acknowledge, and changed-event reset/wait failures.

## Effect transitions
- Admission initializes both effects to NOT_ATTEMPTED.
- Worker dispatch sets Issue IN_PROGRESS and Publish NOT_ATTEMPTED.
- Explicit ISSUE_NOT_ACCEPTED sets Issue back to NOT_ATTEMPTED and request state NOT_ISSUED.
- ISSUE_UNCERTAIN sets Issue UNKNOWN and request state UNCERTAIN.
- ISSUE_ACCEPTED records Issue COMMITTED before invoking Publish, and Publish IN_PROGRESS.
- Publish success sets Publish COMMITTED.
- Publish failure sets Publish UNKNOWN and request state UNCERTAIN. Issue remains COMMITTED.
- Notification failure after successful Issue/Publish preserves both committed effects while the first-failure snapshot records the request identity.
- ACK-side lifecycle signaling failure captures the request identity and committed effects.

## Tests
Added dedicated tests for:
- first and second admission IDs (1 then 2);
- initial NOT_ATTEMPTED states;
- successful Issue and Publish commitment;
- Issue uncertainty (Issue UNKNOWN, Publish NOT_ATTEMPTED);
- Publish failure after accepted Issue (Issue COMMITTED, Publish UNKNOWN);
- request ID and effect snapshot in the first-failure diagnostic for Issue/Publish paths.

Verification used the same MSVC x64 `/W4 /WX /TC` compile flags as the project test script, with a uniquely named temporary executable because the standard executable output name returned linker LNK1104 in this environment. The alternate-output compile succeeded.
- 296 assertions per run.
- 25 consecutive runs passed.
- **7,400 assertions passed; 0 test failures.**
- One earlier draft test failed because it attempted shutdown while an issued request still held its issuer lease. The test was corrected to acknowledge the request before shutdown, then the full 25-run matrix passed.
- The temporary executable and command file were removed.
- The current-run test OBJ (102,780 bytes) was inspected and removed. No other files were deleted.

## Final SHA-256
- `dedicated-io-submitter-v2.h` — `0E12C6029916B45B90818DAB01F012BB62F180C8B9B937341BB9CBE902A71250`
- `dedicated-io-submitter-v2.c` — `037A18DA615B82A462580ABE57158F5B6D3E39AD5B8D25C974831AF74D8AACFD`
- `dedicated-io-submitter-v2-failure-test.c` — `919669BDE7C67C82F28501F2E45755527529F4EB8A4635DB9A6E459D8DAB96BC`

## Remaining limitations
- IDs are scoped to one in-memory Submitter instance; they are not globally unique across restarts or multiple processes.
- The ledger is embedded in caller-owned request memory and is not durable across process failure.
- It does not provide a global lookup table, immutable audit log, persistence, or crash-consistent replay.
- Correct classification of ISSUE_ACCEPTED / ISSUE_NOT_ACCEPTED depends on the callback's contract and truthful result. An ISSUE_UNCERTAIN result remains UNKNOWN.
- Publish failure after Issue acceptance remains uncertain and must not be blindly retried.
- Request memory must remain alive while queued, issuing, outstanding, or referenced by diagnostics/owners.
- No Reconciliation API, external evidence contract, or DEGRADED-to-STOPPED transition was added.
- No Worker lifecycle integration or equivalent Worker effect ledger was added.
- No ABI compatibility claim is made.
- Production remains NO-GO; Phase 1 remains NOT ACCEPTED.

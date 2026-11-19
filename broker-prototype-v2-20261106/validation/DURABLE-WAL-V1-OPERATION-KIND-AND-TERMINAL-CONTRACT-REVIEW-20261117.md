# Durable WAL v1 — Operation-Kind and Terminal Contract Review

Date: 2026-11-17  
Status: **design review; product semantics not approved; no implementation changes**  
Working Copy: `D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`

## 1. Purpose

Resolve what can be established from the current source and identify the minimum decisions required before OPERATION_TERMINAL can be made normative. This review deliberately does not invent business semantics for the four operation kinds.

## 2. Source-grounded operation kinds

The durable contract header defines:

| Numeric value | Symbol | Current documented meaning |
|---:|---|---|
| 1 | GENERIC | Generic operation; no domain-specific effect requirements specified |
| 2 | READ | Read operation; no recipient/transport terminal policy specified |
| 3 | WRITE | Write operation; no recipient/transport terminal policy specified |
| 4 | MESSAGE | Message operation; no recipient idempotency or receipt protocol implemented |

The enum establishes names only. It does **not** define which effect records are mandatory, whether an operation is successful, whether retry is allowed, or what terminal means for that kind.

## 3. Important semantic mismatch in the current WAL vocabulary

The separately reviewed callback contract establishes three different evidence planes:

1. ISSUE: native OS submission. TRUE or ERROR_IO_PENDING means accepted by the OS, not peer/application commit.
2. HANDOFF: internal ownership transfer to the Worker. TRANSFERRED is not a remote receipt.
3. RECIPIENT/APPLICATION: actual logical effect at the peer. This is currently unsupported by the inspected named-pipe path.

The current WAL v1 has only two effect kinds, ISSUE and HANDOFF, and one generic result vocabulary:

- COMMITTED
- PROVEN_NOT_COMMITTED
- UNKNOWN

This creates a semantic ambiguity: `RAYA_WAL_RESULT_COMMITTED` for ISSUE must not be interpreted as recipient/application COMMITTED. For ISSUE it can, at most, mean that the OS accepted the operation; for HANDOFF it can mean that Worker ownership transferred. The same word therefore describes different facts.

**Finding:** do not expose or document the current generic COMMITTED value as a business-level operation commit. A future format/API must either use effect-specific dispositions or make the evidence plane explicit in the type and field names. Recipient commit must remain a separate future effect with a verifiable receipt contract.

## 4. Existing terminal behavior versus the earlier design requirement

The binary design document says OPERATION_TERMINAL is valid only when *all required effect planes* are determinate and no required plane is absent. The current scanner implementation does not encode a required-effect policy per operation kind. It currently accepts a terminal record if:

- REQUEST_ACCEPTED exists;
- at least one of the two effect slots is COMMITTED or PROVEN_NOT_COMMITTED; and
- neither effect slot is IN_PROGRESS or UNKNOWN.

Consequently it accepts ISSUE-only and HANDOFF-only terminal paths for all four operation kinds. This is observed implementation behavior, not an approved contract.

## 5. Candidate policy matrix — deliberately unresolved

The table below lists decisions, not defaults. “Required?” cannot be inferred from the enum names.

| Operation kind | ISSUE required? | HANDOFF required? | Recipient receipt required? | Terminal outcome vocabulary |
|---|---|---|---|---|
| GENERIC | Product decision | Product decision | Product decision | Product decision |
| READ | Product decision | Product decision | Product decision | Product decision |
| WRITE | Product decision | Product decision | Product decision | Product decision |
| MESSAGE | Product decision | Product decision | Product decision | Product decision |

Do not fill these cells by assuming that READ means no handoff, WRITE means recipient commit, or MESSAGE means delivery. Those semantics depend on the actual caller, transport, Worker role, and recipient protocol.

## 6. Minimum effect-state distinctions

Before terminal rules are approved, distinguish these concepts:

| Plane | Positive evidence | Negative evidence | Ambiguous evidence |
|---|---|---|---|
| Native ISSUE | OS accepted/queued | proven not submitted | unknown |
| Worker HANDOFF | ownership transferred | ownership not transferred | unknown |
| Recipient effect | authenticated recipient commit receipt | affirmative recipient proof of non-commit | unknown |

The current v1 generic effect result can represent three states structurally, but its labels are not sufficiently plane-specific. It cannot represent recipient receipts or authenticate reconciliation evidence. A nonzero metadata digest proves only that a digest field was supplied; it does not prove who produced the evidence or that the evidence is true.

## 7. Candidate operation-terminal record requirements

A future terminal record should not be a bare Boolean marker. At minimum, design review should consider:

- terminal schema/version;
- operation-level outcome (candidate examples: COMPLETED, REJECTED, CANCELLED, FAILED_NO_EFFECT, QUARANTINED — names and semantics unapproved);
- required-effect bitset or immutable policy identifier bound to operation kind/version;
- reference to the journal sequence of each required effect's final evidence;
- explicit rule for optional effects and effects that were never started;
- terminal reason/evidence reference;
- invariant that no required effect is absent, IN_PROGRESS, or UNKNOWN;
- invariant that a recipient-success outcome requires a separately verified recipient receipt.

This is a candidate checklist only. Do not append fields to the existing 192-byte record or change its format until versioning/backward-compatibility and policy decisions are approved.

## 8. Critical transition questions

1. Does OS_ACCEPTED followed by HANDOFF_NOT_TRANSFERRED mean FAILED_NO_EFFECT, local retry possible, or quarantine? The existing adapter contract says the Adapter retains/quarantines ownership; it does not establish recipient non-commit.
2. Does OS_ACCEPTED followed by HANDOFF_TRANSFERRED permit a local lifecycle terminal, while the recipient effect remains pending/unknown? If yes, terminal must be explicitly scoped to local ownership completion, not logical message delivery.
3. Can ISSUE_PROVEN_NOT_SUBMITTED be terminal for all operation kinds, or must some kinds retain a rejected/failed business outcome?
4. Can HANDOFF be recorded without ISSUE? The current scanner allows it, but the runtime callback sequence and intended contract must decide whether this is structurally impossible.
5. Are ISSUE and HANDOFF both required for every accepted operation, or does the required set depend on operation kind and outcome?
6. Is recipient commit in scope for any of the four operation kinds? The current named-pipe path has no recipient durable deduplication/receipt, so a positive answer requires a new protocol, not a local WAL change.
7. Does PROVEN_NOT_COMMITTED allow another attempt only for that same effect, and what proof makes retry safe for each plane?
8. What does terminal mean: no more local work, all resources released, ownership transferred, transport completed, or recipient effect committed? These are not interchangeable.

## 9. Safe recommendation

Until product owners decide the above:

- Keep the current scanner as a test-only structural prototype; do not claim terminal policy acceptance.
- Do not add a file writer, replay, or startup recovery.
- Do not equate ISSUE OS acceptance, Worker handoff, local I/O completion, or ACK with recipient commit.
- Preserve UNKNOWN as quarantine; never infer PROVEN_NOT_COMMITTED from missing records or process restart.
- Keep the legacy callback ABI unchanged.
- Treat operation kind as an identity label only, not a complete policy.

The next safe engineering artifact is a versioned *policy schema* with explicit required-effect sets and effect-specific evidence vocabularies, followed by pure transition tests. It must be reviewed and approved before changing the current scanner.

## 10. Scope and evidence

Reviewed:
- `raya-durable-contract-v1.h`
- `raya-durable-wal-v1.h`
- `raya-durable-wal-v1.c`
- `validation/SUBMITTER-VERSIONED-DURABLE-IDENTITY-AND-CALLBACK-CONTRACT-20261117.md`
- `validation/SUBMITTER-DURABLE-LEDGER-RECOVERY-CONTRACT-DESIGN-20261117.md`
- `validation/DURABLE-WAL-V1-BINARY-RECORD-AND-READONLY-SCANNER-DESIGN-20261117.md`
- `validation/DURABLE-WAL-V1-TERMINAL-SEMANTICS-POLICY-PROBE-20261117.md`

No source, header, test, project configuration, README, or production file was changed in this review. No build was required because this is a source-contract review and no executable behavior was modified.

## 11. Gate

This review resolves no product-policy cells. The operation-kind/terminal contract remains **BLOCKED ON PRODUCT DECISIONS**.

Phase 1 remains **NOT ACCEPTED**. Production remains **NO-GO**.

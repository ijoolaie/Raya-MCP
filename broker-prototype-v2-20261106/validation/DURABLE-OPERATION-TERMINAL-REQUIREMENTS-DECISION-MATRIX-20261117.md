# DURABLE OPERATION TERMINAL REQUIREMENTS DECISION MATRIX
Date: 2026-11-17
Scope: Working Copy source inventory and product-decision gate
Status: EVIDENCE-BASED MATRIX — DECISIONS OPEN — NO IMPLEMENTATION AUTHORIZED

## 1. Executive finding

A recursive source search across C and H files found references to RAYA_DURABLE_OPERATION_* only in the v1 contract declaration, its implementation, and contract tests. The search did not find production assignment/call sites that construct durable operation IDs or select GENERIC/READ/WRITE/MESSAGE for live operations.

Accordingly, the current enum is a contract vocabulary/test fixture, not an implemented operation classification policy. No required evidence mask can safely be assigned to any operation kind from the present source evidence.

## 2. Evidence inventory

Search terms:
- RAYA_DURABLE_OPERATION_
- raya_durable_operation_id_v1
- raya_durable_issue_callback
- raya_durable_handoff_callback
- raya_submitter_enqueue
- raya_integrated_adapter

Search scope: recursive *.c and *.h files in the Working Copy.

Observed:
- Operation kind enum and validation live in raya-durable-contract-v1.h/.c.
- The contract test fixture constructs a WRITE operation ID; this is test-only.
- Runtime callbacks currently cover Issue and Handoff only.
- No production code path was found assigning a durable operation kind or integrating durable operation IDs into Submitter/Adapter/Worker requests.
- Existing Submitter/Adapter/Worker implementation paths therefore cannot be claimed to satisfy this durable contract merely because their lower-level lifecycle behavior was separately tested.

This is a source-search result, not proof that no external caller or unsearched non-C/H integration exists.

## 3. Operation-kind decision matrix

| Operation kind | Production callsite mapping | Candidate terminal scopes | Required evidence mask | Current decision |
|---|---|---|---|---|
| GENERIC | Not found | Must be caller/product-defined; cannot infer | UNASSIGNED | Reject any stronger guarantee |
| READ | Not found | Could mean OS completion, local buffer delivery, or application-level read completion | UNASSIGNED | Unresolved |
| WRITE | Not found | Could mean OS acceptance, transport completion, peer processing, or durable recipient commit | UNASSIGNED | Unresolved |
| MESSAGE | Not found | Could mean local enqueue, Worker handoff, remote receipt, or application acknowledgement | UNASSIGNED | Unresolved |

No operation kind receives a default recipient-commit or transport-terminal policy. Enum names are not semantics.

## 4. Required product decisions before policy assignment

For each real operation family, the product owner must define:

1. Caller-visible success event: what exact externally observable fact makes the API report success?
2. Required durability boundary: process-local, OS/transport, remote process, or recipient durable storage?
3. Failure semantics: which outcomes are retryable, terminal failure, or indeterminate/quarantined?
4. Idempotency scope: what stable key does the recipient recognize, and for how long?
5. Authoritative evidence: which component or protocol can prove the terminal event?
6. Restart semantics: must the result survive broker crash/restart, host restart, or only thread/component teardown?
7. Retention: how long must identity, receipt, and reconciliation evidence remain available?
8. Compatibility: how are old clients/providers handled when a stronger terminal policy is requested?

These decisions must be made per actual operation family, not merely per current enum member.

## 5. Safe interim policy

Until the above decisions exist:
- Keep all operation-kind required-evidence policies UNASSIGNED.
- Permit only a quarantine-oriented profile for experimental local lifecycle work, and describe the exact evidence scope in its API/result.
- Reject requests for TRANSPORT_TERMINAL or RECIPIENT_COMMIT when their verifier/evidence contract is absent.
- Do not treat Issue OS acceptance as successful Write/Message completion.
- Do not treat Worker handoff as remote receipt.
- Do not treat callback invocation as recipient commit.
- Preserve UNKNOWN and contradictory evidence as quarantine; never silently convert them into success or retry.
- Do not silently downgrade a requested stronger guarantee to a weaker one.

## 6. Candidate policy schema requirements (not implementation)

A future versioned policy record should include:
- immutable policy_id and policy_version;
- operation family identifier (separate from a broad enum where necessary);
- terminal_scope;
- required_evidence_mask;
- allowed outcome set for each evidence plane;
- explicit UNKNOWN/DEGRADED handling;
- retry authorization rule and attempt identity requirements;
- provider contract version and verifier identity;
- recipient protocol/trust-policy references when recipient evidence is required;
- migration/compatibility rule.

Policy changes must be versioned. An operation accepted under one policy must retain that policy identity for later reconciliation; a process restart must not reinterpret old records under a new default policy.

## 7. Proposed decision workflow

1. Inventory real API operations and their caller-visible contracts.
2. For each operation, select the success event and durability boundary.
3. Identify the component/protocol that can produce authoritative evidence.
4. Define failure/unknown/retry behavior and idempotency scope.
5. Assign a versioned policy and required evidence mask.
6. Add negative tests for missing, stale, mismatched, contradictory, and unavailable evidence.
7. Only then implement runtime enforcement and persistence/recovery in separately reviewable slices.

## 8. Work performed and boundaries

- Recursive source inventory/search only.
- No source/header/test/project configuration/README changes.
- No builds or tests.
- No WAL writer/replay/startup recovery or recipient verifier.
- No Production modifications.
- No process termination, install, or download.

## 9. Conclusion

The next implementation task is blocked by missing product semantics, not by a coding detail. The correct engineering action is to keep operation-kind policies unassigned and obtain decisions for actual API operation families. Once those decisions are documented, the v2 contract can encode them without pretending that current v1 declarations prove recipient behavior.

---
End of decision matrix.

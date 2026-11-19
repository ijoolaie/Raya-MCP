# Broker prototype — repository publication baseline (2026-11-19)

## Scope

This repository snapshot publishes the current native broker source, tests, TLA+ model/configuration files, build scripts, and Markdown validation reports from the dedicated Working Copy. Generated TLC state directories, `*-meta` state dumps, `.st`, `.fp`, `.bin`, `.dump`, `.chkpt`, logs, object files, executables, and other build outputs are intentionally excluded. The excluded state archive remains local and is not deleted.

## Verified gate status at publication

- Gate A — governance baseline: COMPLETE.
- Gate B — v1 read-only inventory ABI freeze: COMPLETE.
- Gate C — production copied read-only inventory implementation: COMPLETE; see `PRODUCTION-RETAINED-INVENTORY-IMPLEMENTATION-20261118.md`.
- Gate D — reconciliation action decision: COMPLETE; no safe mutation/reconciliation action approved; fail-closed quarantine remains the only authorized disposition.
- Gate E — positive recovery acceptance: BLOCKED; no approved positive action exists to test.
- Gate F — Stage 5 acceptance: BLOCKED pending explicit project-owner acceptance of Quarantine as a Phase 1 limitation and review of the matching PR.
- Stage 5: NOT ACCEPTED.
- Phase 1: NOT ACCEPTED.
- Production: NO-GO.

## Important limitations

- Read-only copied inventory is not reconciliation or resource reclamation.
- No retained record may be generically unlinked, freed, manually closed, retried, or replayed.
- Uncertain WRITE operations must not be replayed; OS acceptance is not recipient commit.
- Bounded TLC evidence is not a full-model proof or full Raft claim.
- No Production deployment or Production handler migration is included.

This publication does not accept Quarantine on behalf of the project owner and does not change any gate status.
## Roadmap integrity

The Working Copy's standalone PROJECT-ROADMAP.md was not published in this snapshot because a byte-level check confirmed stored UTF-8 mojibake and a safe deterministic repair was not established. It was not reconstructed from memory. The current Gate A-F state is summarized above from the separately verified implementation and decision reports. A verified original/backup is required before replacing the roadmap.

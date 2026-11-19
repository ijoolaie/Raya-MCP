# Model / Config / Validation Report Cross-check — 2026-11-14

## Scope
Read-only inventory of root-level TLA+ models and TLC configs, cross-referenced against `validation/*.md`. No model, config, trace, state store, archive, or Production file was modified.

## Inventory
- Root TLA+ model files excluding generated `_TTrace_` files: 137.
- Validation Markdown reports: 53.
- Root TLC config files: 215.
- Models modified on 2026-11-13 or 2026-11-14: 62; 61 have a direct mention in a validation report.

## Confirmed documentation gap
`WitnessV2AppendEntriesOldResponseWithNewRequests.tla` has a same-name config but no direct reference in any validation Markdown, CSV, or text file. It has no same-name counterexample trace pair. Its model describes a term-1 delayed failure sharing request ID 2 with a term-2 request, but the old failure is dropped by the term guard. The model/config are retained; no test result is inferred and no deletion is recommended. A short report should be added only after its intended historical test result/command can be recovered or it is deliberately revalidated.

## Config naming observations
Many configs without an exact same-name `.tla` are intentional variants (e.g. `-Without...`, `-Stable`, `-Rollback`, `-Diagnostic`, `-Witness`, `-MaxTerm...`, `-MaxLog...`) that target a shared base model. They are not classified as orphans based on filename mismatch alone. `WitnessV2IdentityScopeFailureDedup.tla` uses variant config names beginning `WitnessV2IdentityScope-...`; it has a report mention, so the zero exact-prefix config count is not a deletion signal.

## Recent model/report mapping
The Nov 13–14 test models were checked against their validation reports. The recent focused models have matching reports and expected variant configs. `WitnessV2LeaderChangeIncrementalIntegration.tla` maps to `MAIN-INCREMENTAL-LEADER-CHANGE-INTEGRATION-20261114.md`, which records an inconclusive TLC run caused by disk exhaustion, not a pass.

## Safety / disposition
- No files deleted or moved.
- No TLC run started.
- No commits or resets.
- Main base model SHA256: `C85CF1906D92DF31A62F8DA63392673809D3A314F30012242FB2B6E54B203A57` (unchanged at audit time).
- Production remains NO-GO; Phase 1 remains NOT ACCEPTED.

## Next step
Recover the historical execution evidence for `WitnessV2AppendEntriesOldResponseWithNewRequests` or schedule a small, isolated revalidation after disk capacity is safely addressed. Do not delete this model/config merely because its report is missing.

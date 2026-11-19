# Combined AppendEntries response guards — negative controls (2026-11-13)

Companion to `APPEND-RESPONSE-COMBINED-GUARDS-ORDERING-20261113.md`. The positive combined model passed TLC with 46 generated / 24 distinct states, graph depth 5, and all three guard snapshot checks intact.

## Guard-removal mutants

Each mutant changes only one handler precondition. The invariant `AcceptanceSnapshotValid` requires any accepted response to have been accepted while the role was Leader and both endpoints were online.

| Mutant | TLC result | State-space at counterexample | Counterexample |
|---|---|---:|---|
| `...WithoutRoleGuard` | Expected violation; exit 12; `AcceptanceSnapshotValid` | 8 generated, 8 distinct; depth 3 | `WitnessV2AppendResponseCombinedGuardsWithoutRoleGuard_TTrace_1794576452.tla/.bin` |
| `...WithoutSourceOnlineGuard` | Expected violation; exit 12; `AcceptanceSnapshotValid` | 11 generated, 10 distinct; depth 3 | `WitnessV2AppendResponseCombinedGuardsWithoutSourceOnlineGuard_TTrace_1794576461.tla/.bin` |
| `...WithoutDestinationOnlineGuard` | Expected violation; exit 12; `AcceptanceSnapshotValid` | 14 generated, 11 distinct; depth 3 | `WitnessV2AppendResponseCombinedGuardsWithoutDestinationOnlineGuard_TTrace_1794576496.tla/.bin` |

### Counterexample meaning

- **Role mutant:** `DemoteA` first changes A from Leader to Follower. The mutant then accepts the still-queued failure response; `leaderNextA` changes 6→5 and the acceptance snapshot records `acceptedWhileLeader = FALSE`.
- **Source mutant:** `TakeSourceOffline` first removes F from the online set. The mutant accepts the response with F offline; `leaderNextA` changes 6→5 and `acceptedWhileSourceOnline = FALSE`.
- **Destination mutant:** `TakeDestinationOffline` first removes A from the online set. The mutant accepts the response with A offline; `leaderNextA` changes 6→5 and `acceptedWhileDestinationOnline = FALSE`.

All three mutants retain the other two guards, matching term, matching requestId, and the queued-message precondition. Each counterexample therefore isolates the corresponding missing guard. Exit 12 is the expected TLC invariant-violation result, not a successful verification of the mutant.

## SHA-256

| Artifact | SHA-256 |
|---|---|
| Positive model | `146DFDBDD49A5E9C01368D00C2FFD30F9340B688331E347931FA037F334ECA73` |
| Positive config | `626A6CE23D20471BF3260EF6953CCB56D032E5D47D36AE59892EE5B771AA809D` |
| Role mutant model | `A8B688536A1D5BB77DB96DC2D95C625B69BACBBDF1E3D4A24B6DFC39A928DF51` |
| Role mutant config | `B513C9E8B769303E353F131D904B3245F98CD97C48E6272884661D4B427606AF` |
| Source mutant model | `A86AFFB790D7BAD7893D56A75A46402EA49424C1232E5C9814CCD93A5EFD010E` |
| Source mutant config | `B513C9E8B769303E353F131D904B3245F98CD97C48E6272884661D4B427606AF` |
| Destination mutant model | `1D972ED0FED1942E03AC6F4EBBB401D63556D7E8B77AD634B4CBF7F94275A388` |
| Destination mutant config | `B513C9E8B769303E353F131D904B3245F98CD97C48E6272884661D4B427606AF` |
| Role trace (.tla) | `910BDA10F13F7A34D6A6333021CF8BBFDD70187A85B1274BB44661357FC8D45D` |
| Role trace (.bin) | `A25E22A88E06B583D6E3C8C2C53529C04E63EF936D06ABD86397FB8B963F1901` |
| Source trace (.tla) | `2149C95E1511C017277DA03700DC6D6D5A88AD07FF5963B05EF78CE79592AC5B` |
| Source trace (.bin) | `EAC9DB9FBC697BF4CC18AC94266B2629F452B255C5EC289F84B0A41A38E1ECA3` |
| Destination trace (.tla) | `22100F2A4FD6AF5CA2945C02AACAECDF1DFF151E64E749130D1D3681D61F654E` |
| Destination trace (.bin) | `61F12FBFD97A838A737BD58304B49CF5DAFE5B252E377D877138DA845EB0B372` |

## Scope

These are bounded abstract negative controls for the handler's three preconditions. They do not establish full Raft correctness or production behavior. All diagnostic and counterexample files are retained; no cleanup was performed. No source/base model, Production code, running service, tunnel, archive, or historical TLC state was changed.

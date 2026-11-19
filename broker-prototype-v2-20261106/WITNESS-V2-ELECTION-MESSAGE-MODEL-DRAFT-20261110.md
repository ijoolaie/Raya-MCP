# Witness v2 — Explicit Election Message Model (Draft)

Date: 2026-11-10
Status: bounded TLA+ draft; static review only; TLC not run
Production: NO-GO

## Scope
This model is a deliberate next refinement of the global-election-oracle abstraction. It models three nodes, per-node terms, one-vote-per-term state, candidate/follower/leader roles, request and response messages, message delivery and dropping, online partitions, and quorum-based promotion.

## Important limitations
- This is an election-only model, not Raft.
- It does not yet model AppendEntries, replicated log entries, conflict repair, commit indexes, leader completeness, or state-machine safety.
- Log freshness fields are carried in request messages but the request grant predicate currently does not compare candidate log freshness. This is an explicit known omission and means the model must not be used to claim Raft election safety.
- The message set is an abstract network; no byte codec, authentication, pipe peer identity, crash persistence, disk durability, or replay protection is represented.
- The simplified response/term handling needs further review for stale response and same-term competing candidate cases.
- TLC has not been run; no model-checking result is claimed.

## Next required refinement
Add the Raft up-to-date log comparison to vote granting, make higher-term observation explicit for both request and response messages, model per-node durable term/vote across crash/restart, then add AppendEntries and log matching. Only after static review should TLC be run against a small finite configuration.

No runtime, service, endpoint, credentials, key, ACL, mutex, network configuration, or Production action was changed. IranPriceScout and its independent tunnel remain untouched.

Production: NO-GO.

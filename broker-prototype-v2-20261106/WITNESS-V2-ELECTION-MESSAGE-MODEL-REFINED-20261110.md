# Witness v2 — Refined Election Message Model

Date: 2026-11-10
Status: bounded TLA+ draft; static review only; TLC not run
Production: NO-GO

## Changes from the previous draft
- Vote granting now checks Raft-style log freshness: a candidate's last log term must be greater, or terms equal and its log index at least as large as the voter's.
- Higher-term responses and stale-log higher-term requests are consumed by explicit steps that advance the receiver's term, clear its vote, and return it to Follower; a stale candidate is denied after the term update.
- Added a bounded leader-uniqueness-per-term invariant and a quorum invariant for nodes in Leader role.
- Previous draft remains intact for comparison.

## Scope and limitations
This remains an election-only abstraction, not a Raft implementation or proof.
- Log contents and AppendEntries are not modeled; logLength/logTerm are state variables but never advanced by replication.
- currentTerm and votedFor are modeled per node but are volatile; crash/restart and durable storage are absent.
- Request/response messages are abstract records; authentication, transport framing, duplicate/replay semantics, and peer identity are absent.
- A voter that grants a request steps down to Follower; the model does not yet represent heartbeat/leader contact or election timeouts.
- No commit index, leader completeness, log matching, state-machine safety, FINALIZE/ABORT, or witness transaction protocol is modeled.
- TLC is not installed/available in the known environment and has not been run. No model-checking result is claimed.

## Review boundary
Static source review and file read-back only. The configured invariants are proposed properties, not verified results. The model must be TLC-checked before relying on counterexample absence, then extended with durable term/vote, AppendEntries, conflict repair, and crash/restart.

No runtime, service, endpoint, credentials, key, ACL, mutex, network configuration, or Production action was changed. IranPriceScout and its independent tunnel remain untouched.

Production: NO-GO.

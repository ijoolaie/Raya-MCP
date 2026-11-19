# Witness v2 — AppendEntries / Log Replication Draft

Date: 2026-11-10
Status: bounded TLA+ draft; static review only; TLC not run
Production: NO-GO

## Purpose
This is a separate bounded abstraction for message-driven log replication and conflict replacement. It does not modify the election model.

## Modeled
- Per-node term, role, log-term sequence, and commit index.
- Explicit APPEND messages that may be delivered, rejected, or dropped.
- Offline nodes and delayed delivery.
- Higher-term observation on a rejected request steps the receiver down and advances its term.
- A committed-prefix compatibility guard prevents this abstraction from replacing already committed entries.
- A bounded leader commit action counts only online replicas with an identical log and a commit index at least as high as the leader's prior commit index.

## Deliberate abstraction
APPEND currently carries the leader's entire log and uses a zero-length matching prefix. This is a snapshot-like simplification, not Raft's prevLogIndex/prevLogTerm incremental AppendEntries protocol. The model does not prove Raft log matching or leader completeness.

## Static-review cautions
- AssumeLeader is an environment action, not an election protocol; it can create leaders without votes. This draft must not be interpreted as a consensus proof.
- Commit quorum calculation is simplified and requires exact whole-log equality; it is not the Raft rule for committing a current-term entry by replicated index.
- Persistent term/vote, crash/restart, message authentication, replay protection, RPC framing, and transaction semantics are absent.
- The bounded invariants are type and commit-index bounds only. No temporal or end-to-end safety proof is claimed.
- TLC has not been run. The configuration is a candidate finite state space only.

## Next refinement
Replace snapshot-like APPEND with prevLogIndex/prevLogTerm and suffix entries; model conflict deletion, durable state, elections and leader completeness in one integrated model; then run TLC before drawing conclusions.

No service, endpoint, credential, key, ACL, mutex, network configuration, or Production action was changed. IranPriceScout and its independent tunnel remain untouched.

Production: NO-GO.

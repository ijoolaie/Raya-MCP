# Witness v2 Quorum and Fencing Abstraction — Draft

Date: 2026-11-10
Status: design/model draft; TLC not run
Production: NO-GO

## Purpose
This bounded model adds three replicas, quorum-sized acknowledgements, leader terms, online partitions, replicated log indexes, and stale-term write rejection. It is deliberately an abstraction of the consensus boundary, not a Raft or Paxos implementation.

## Assumed authority boundary
The model has one globally agreed leader variable and one monotonically increasing currentTerm. Elect() is an abstract election oracle: it chooses an online candidate whose local log index is at least the committed index, and increments the term. This assumption excludes competing leaders by construction. Therefore this model cannot prove leader-election safety, quorum intersection under real message schedules, or absence of split brain.

## Modeled behavior
- SetOnline models node availability/partition changes. The elected leader may become offline; it remains the recorded leader but cannot append unless it is online and a quorum is reachable.
- Elect advances the global term and installs one leader, subject to quorum availability and committed-prefix eligibility.
- AppendAndCommit accepts a mutation only from the current leader at the current term and only with an acknowledgement set containing the leader and at least a quorum of online replicas. The acknowledged replicas advance to the new index.
- RejectStaleLeaderWrite is an explicit stuttering action documenting that an old term cannot mutate state; it does not create a rejection response record.

## Configured checks
TypeOK, CommitMonotonic, LeaderTermCurrent, CommittedPrefixOnQuorum, NoReplicaAheadOfItsAcceptedIndex; temporal property CommitNeverDecreases.

## Scope limitations / known abstractions
- No network message queues, delayed/duplicated/reordered messages, or independent per-replica leader beliefs.
- No competing candidates, vote records, durable terms, or quorum certificates.
- Election is globally serialized by assumption; partitioned minorities cannot independently elect.
- No append replication handshake or conflict repair; append+quorum commit is one atomic abstract action.
- No operation payload, idempotency digest, FINALIZE/ABORT race, auth consumption, or STATUS channel in this model.
- Replica crash/restart, disk flush, torn writes, snapshots, clone/rollback and anti-rollback are absent.
- A quorum intersection theorem is not established by this model; only the finite acknowledgement cardinality condition is represented.
- MaxIndex and MaxTerm bound exploration.

## Critical review note
This is a safety scaffold to expose the assumptions required at the consensus boundary. Before treating it as a consensus model, refine to per-replica persistent term/vote/log state and explicit RequestVote/AppendEntries messages, then check election safety, log matching, leader completeness, and state machine safety. Prefer a separately reviewed Raft/TLA+ reference model over incrementally presenting this abstraction as a consensus proof.

## Non-authorization
No runtime integration, service, endpoint, credential, key, persistent state, ACL, mutex, network configuration, or Production action was created. Existing changes are preserved. IranPriceScout and its independent tunnel remain untouched.

Production: NO-GO.

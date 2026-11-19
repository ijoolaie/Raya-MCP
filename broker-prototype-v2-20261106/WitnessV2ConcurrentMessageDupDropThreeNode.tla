---- MODULE WitnessV2ConcurrentMessageDupDropThreeNode ----
EXTENDS Naturals, FiniteSets, TLC
CONSTANTS UseTermGuard
Nodes == {"n1", "n2", "n3"}
Follower == "FOLLOWER"
Candidate == "CANDIDATE"
Leader == "LEADER"
NoVote == "NO_VOTE"
VARIABLES term, role, votedFor, votes, ledger, messages, handled, appendObserved, staleVoteAccepted
vars == <<term, role, votedFor, votes, ledger, messages, handled, appendObserved, staleVoteAccepted>>

Init ==
  /\ term = [n \in Nodes |-> IF n = "n1" THEN 2 ELSE 1]
  /\ role = [n \in Nodes |-> IF n = "n1" THEN Candidate ELSE Follower]
  /\ votedFor = [n \in Nodes |-> IF n = "n1" THEN "n1" ELSE NoVote]
  /\ votes = [n \in Nodes |-> IF n = "n1" THEN {"n1"} ELSE {}]
  /\ ledger = {}
  /\ messages = {[kind |-> "REQUEST_VOTE", src |-> "n1", dst |-> "n3", term |-> 2, mid |-> 1]}
  /\ handled = {}
  /\ appendObserved = FALSE
  /\ staleVoteAccepted = FALSE

DuplicateRequestVote ==
  /\ [kind |-> "REQUEST_VOTE", src |-> "n1", dst |-> "n3", term |-> 2, mid |-> 1] \in messages
  /\ ~([kind |-> "REQUEST_VOTE", src |-> "n1", dst |-> "n3", term |-> 2, mid |-> 2] \in messages)
  /\ messages' = messages \cup {[kind |-> "REQUEST_VOTE", src |-> "n1", dst |-> "n3", term |-> 2, mid |-> 2]}
  /\ UNCHANGED <<term, role, votedFor, votes, ledger, handled, appendObserved, staleVoteAccepted>>

GrantRequestVote(m) ==
  /\ m.kind = "REQUEST_VOTE"
  /\ m \in messages
  /\ m.dst = "n3"
  /\ m.term >= term["n3"]
  /\ (term["n3"] < m.term \/ votedFor["n3"] = NoVote \/ votedFor["n3"] = m.src)
  /\ term' = [term EXCEPT !["n3"] = m.term]
  /\ role' = [role EXCEPT !["n3"] = Follower]
  /\ votedFor' = [votedFor EXCEPT !["n3"] = m.src]
  /\ ledger' = ledger \cup {[voter |-> "n3", voteTerm |-> m.term, candidate |-> m.src]}
  /\ messages' = (messages \ {m}) \cup
       {[kind |-> "VOTE_RESPONSE", src |-> "n3", dst |-> m.src,
         term |-> m.term, granted |-> TRUE, mid |-> m.mid + 10]}
  /\ UNCHANGED <<votes, handled, appendObserved, staleVoteAccepted>>

HandleVoteResponse(m) ==
  /\ m.kind = "VOTE_RESPONSE"
  /\ m \in messages
  /\ m.dst = "n1"
  /\ (IF UseTermGuard THEN m.term = term["n1"] ELSE TRUE)
  /\ role' = role
  /\ votes' = [votes EXCEPT !["n1"] = @ \cup {m.src}]
  /\ messages' = messages \ {m}
  /\ handled' = handled \cup {m.mid}
  /\ staleVoteAccepted' = IF m.term < term["n1"] THEN TRUE ELSE staleVoteAccepted
  /\ UNCHANGED <<term, votedFor, ledger, appendObserved>>

DuplicateVoteResponse ==
  /\ \E m \in messages : m.kind = "VOTE_RESPONSE" /\ m.mid = 11
  /\ messages' = messages \cup
       {[kind |-> "VOTE_RESPONSE", src |-> "n3", dst |-> "n1",
         term |-> 2, granted |-> TRUE, mid |-> 13]}
  /\ UNCHANGED <<term, role, votedFor, votes, ledger, handled, appendObserved, staleVoteAccepted>>

SendHigherTermAppend ==
  /\ messages' = messages \cup
       {[kind |-> "APPEND", src |-> "n2", dst |-> "n1", term |-> 3, mid |-> 21]}
  /\ UNCHANGED <<term, role, votedFor, votes, ledger, handled, appendObserved, staleVoteAccepted>>

DuplicateAppend ==
  /\ \E m \in messages : m.kind = "APPEND" /\ m.mid = 21
  /\ messages' = messages \cup
       {[kind |-> "APPEND", src |-> "n2", dst |-> "n1", term |-> 3, mid |-> 22]}
  /\ UNCHANGED <<term, role, votedFor, votes, ledger, handled, appendObserved, staleVoteAccepted>>

ObserveAppend(m) ==
  /\ m.kind = "APPEND"
  /\ m \in messages
  /\ m.dst = "n1"
  /\ m.term > term["n1"]
  /\ term' = [term EXCEPT !["n1"] = m.term]
  /\ role' = [role EXCEPT !["n1"] = Follower]
  /\ votedFor' = [votedFor EXCEPT !["n1"] = NoVote]
  /\ messages' = messages \ {m}
  /\ appendObserved' = TRUE
  /\ UNCHANGED <<votes, ledger, handled, staleVoteAccepted>>

DropMessage(m) ==
  /\ m \in messages
  /\ messages' = messages \ {m}
  /\ UNCHANGED <<term, role, votedFor, votes, ledger, handled, appendObserved, staleVoteAccepted>>

Next ==
  \/ DuplicateRequestVote
  \/ \E m \in messages : GrantRequestVote(m)
  \/ DuplicateVoteResponse
  \/ \E m \in messages : HandleVoteResponse(m)
  \/ SendHigherTermAppend
  \/ DuplicateAppend
  \/ \E m \in messages : ObserveAppend(m)
  \/ \E m \in messages : DropMessage(m)

TypeOK ==
  /\ UseTermGuard \in BOOLEAN
  /\ term \in [Nodes -> 1..3]
  /\ role \in [Nodes -> {Follower, Candidate, Leader}]
  /\ votedFor \in [Nodes -> Nodes \cup {NoVote}]
  /\ votes \in [Nodes -> SUBSET Nodes]
  /\ ledger \subseteq [voter : Nodes, voteTerm : 1..3, candidate : Nodes]
  /\ messages \subseteq
      ([kind : {"REQUEST_VOTE"}, src : Nodes, dst : Nodes, term : 1..3, mid : 1..30]
       \cup [kind : {"VOTE_RESPONSE"}, src : Nodes, dst : Nodes, term : 1..3, granted : BOOLEAN, mid : 1..30]
       \cup [kind : {"APPEND"}, src : Nodes, dst : Nodes, term : 1..3, mid : 1..30])
  /\ handled \subseteq 1..30
  /\ appendObserved \in BOOLEAN
  /\ staleVoteAccepted \in BOOLEAN

OneVotePerTerm ==
  \A n \in Nodes : \A t \in 1..3 :
    Cardinality({v \in ledger : v.voter = n /\ v.voteTerm = t}) <= 1

VoteCountIsSetBased ==
  votes["n1"] \subseteq Nodes /\ Cardinality(votes["n1"]) <= 3

AppendObservationMonotonic ==
  appendObserved => term["n1"] = 3 /\ role["n1"] = Follower

StaleVoteRejected ==
  staleVoteAccepted = FALSE

Spec == Init /\ [][Next]_vars
====

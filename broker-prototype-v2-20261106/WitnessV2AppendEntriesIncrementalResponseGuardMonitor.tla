---- MODULE WitnessV2AppendEntriesIncrementalResponseGuardMonitor ----
EXTENDS Naturals, FiniteSets, Sequences, TLC

CONSTANTS Nodes, MaxTerm, MaxLog, MaxRequestId
ASSUME Cardinality(Nodes) = 3
ASSUME MaxTerm \in Nat
ASSUME MaxLog \in Nat
ASSUME MaxRequestId \in Nat

Follower == "FOLLOWER"
Candidate == "CANDIDATE"
Leader == "LEADER"
LogTerms == 0..MaxTerm

VARIABLES online, currentTerm, role, log, commitIndex, nextIndex, matchIndex, lastRequestId, messages, responseAccepted, badRoleAcceptance, badSourceAcceptance, badDestinationAcceptance
vars == <<online, currentTerm, role, log, commitIndex, nextIndex, matchIndex, lastRequestId, messages, responseAccepted, badRoleAcceptance, badSourceAcceptance, badDestinationAcceptance>>

Quorum == (Cardinality(Nodes) \div 2) + 1

NodeSymmetry == Permutations(Nodes)
MessageBound == Cardinality(messages) <= 1

MinSet(S) == CHOOSE x \in S : \A y \in S : x <= y

LastLogTerm(n) == IF Len(log[n]) = 0 THEN 0 ELSE log[n][Len(log[n])]

LogUpToDate(candidate, voter) ==
  \/ LastLogTerm(candidate) > LastLogTerm(voter)
  \/ (LastLogTerm(candidate) = LastLogTerm(voter) /\
      Len(log[candidate]) >= Len(log[voter]))

PrevMatches(localLog, prevIndex, prevTerm) ==
  IF prevIndex = 0
  THEN prevTerm = 0
  ELSE IF prevIndex <= Len(localLog)
       THEN localLog[prevIndex] = prevTerm
       ELSE FALSE

AppendResult(localLog, prevIndex, entries) ==
  LET conflicts ==
        {k \in 1..Len(entries) :
          prevIndex + k <= Len(localLog) /\
          localLog[prevIndex + k] # entries[k]}
  IN IF conflicts # {}
     THEN LET firstConflict == MinSet(conflicts)
          IN SubSeq(localLog, 1, prevIndex + firstConflict - 1) \o
             SubSeq(entries, firstConflict, Len(entries))
     ELSE IF prevIndex + Len(entries) > Len(localLog)
          THEN localLog \o
               SubSeq(entries, Len(localLog) - prevIndex + 1,
                      Len(entries))
          ELSE localLog

Init ==
  /\ online = Nodes
  /\ currentTerm = [n \in Nodes |-> 0]
  /\ role = [n \in Nodes |-> Follower]
  /\ log = [n \in Nodes |-> <<>>]
  /\ commitIndex = [n \in Nodes |-> 0]
  /\ nextIndex = [n \in Nodes |-> [p \in Nodes |-> 1]]
  /\ matchIndex = [n \in Nodes |-> [p \in Nodes |-> 0]]
  /\ lastRequestId = [n \in Nodes |-> [p \in Nodes |-> 0]]
  /\ messages = {}
  /\ responseAccepted = FALSE
  /\ badRoleAcceptance = FALSE
  /\ badSourceAcceptance = FALSE
  /\ badDestinationAcceptance = FALSE

SetOnline(s) ==
  /\ s \subseteq Nodes
  /\ online' = s
  /\ UNCHANGED <<currentTerm, role, log, commitIndex, nextIndex, matchIndex, lastRequestId, messages, responseAccepted, badRoleAcceptance, badSourceAcceptance, badDestinationAcceptance>>

AssumeLeader(n) ==
  /\ n \in online
  /\ currentTerm[n] < MaxTerm
  /\ \A p \in Nodes : currentTerm[n] >= currentTerm[p]
  /\ \A p \in Nodes :
       /\ Len(log[n]) >= commitIndex[p]
       /\ SubSeq(log[n], 1, commitIndex[p]) =
            SubSeq(log[p], 1, commitIndex[p])
  /\ \E voters \in SUBSET Nodes :
       /\ n \in voters
       /\ voters \subseteq online
       /\ Cardinality(voters) >= Quorum
       /\ \A p \in voters : LogUpToDate(n, p)
       /\ role' = [p \in Nodes |->
            IF p = n THEN Leader
            ELSE IF p \in voters THEN Follower ELSE role[p]]
       /\ currentTerm' = [p \in Nodes |->
            IF p \in voters THEN currentTerm[n] + 1 ELSE currentTerm[p]]

  /\ nextIndex' = [nextIndex EXCEPT ![n] = [p \in Nodes |-> Len(log[n]) + 1]]
  /\ matchIndex' = [matchIndex EXCEPT ![n] = [p \in Nodes |-> 0]]
  /\ lastRequestId' = [lastRequestId EXCEPT ![n] = [p \in Nodes |-> 0]]
  /\ UNCHANGED <<online, log, commitIndex, messages, responseAccepted, badRoleAcceptance, badSourceAcceptance, badDestinationAcceptance>>

LeaderAppend(n) ==
  /\ n \in online
  /\ role[n] = Leader
  /\ Len(log[n]) < MaxLog
  /\ log' = [log EXCEPT ![n] = Append(@, currentTerm[n])]
  /\ UNCHANGED <<online, currentTerm, role, commitIndex, nextIndex, matchIndex, lastRequestId, messages, responseAccepted, badRoleAcceptance, badSourceAcceptance, badDestinationAcceptance>>

SendAppend(n, p) ==
  /\ n \in online
  /\ p \in Nodes \ {n}
  /\ role[n] = Leader
  /\ lastRequestId[n][p] < MaxRequestId
  /\ nextIndex[n][p] \in 1..(Len(log[n]) + 1)
  /\ LET prev == nextIndex[n][p] - 1
         rid == lastRequestId[n][p] + 1
     IN
       /\ messages' = messages \cup
            {[kind |-> "APPEND", src |-> n, dst |-> p,
              term |-> currentTerm[n], requestId |-> rid,
              prevIndex |-> prev,
              prevTerm |-> IF prev = 0 THEN 0 ELSE log[n][prev],
              entries |-> SubSeq(log[n], prev + 1, Len(log[n])),
              leaderCommit |-> commitIndex[n]]}
       /\ lastRequestId' = [lastRequestId EXCEPT ![n][p] = rid]
  /\ UNCHANGED <<online, currentTerm, role, log, commitIndex, nextIndex, matchIndex, responseAccepted, badRoleAcceptance, badSourceAcceptance, badDestinationAcceptance>>

DeliverAppend(m) ==
  /\ m \in messages
  /\ m.kind = "APPEND"
  /\ m.dst \in online
  /\ m.src \in online
  /\ m.term >= currentTerm[m.dst]
  /\ PrevMatches(log[m.dst], m.prevIndex, m.prevTerm)
  /\ m.prevIndex >= commitIndex[m.dst]
  /\ Len(m.entries) + m.prevIndex <= MaxLog
  /\ LET n == m.dst
         nextLog == AppendResult(log[n], m.prevIndex, m.entries)
         acceptedIndex == m.prevIndex + Len(m.entries)
     IN
       /\ log' = [log EXCEPT ![n] = nextLog]
       /\ commitIndex' = [commitIndex EXCEPT
            ![n] = IF m.leaderCommit > @
                   THEN (IF m.leaderCommit < Len(nextLog)
                         THEN m.leaderCommit ELSE Len(nextLog))
                   ELSE @]
       /\ currentTerm' = [currentTerm EXCEPT ![n] = m.term]
       /\ role' = [role EXCEPT ![n] = Follower]
       /\ messages' = (messages \ {m}) \cup
            {[kind |-> "APPEND_RESP", src |-> n, dst |-> m.src,
              term |-> m.term, requestId |-> m.requestId,
              success |-> TRUE, matchIndex |-> acceptedIndex]}
  /\ UNCHANGED <<online, nextIndex, matchIndex, lastRequestId, responseAccepted, badRoleAcceptance, badSourceAcceptance, badDestinationAcceptance>>

RejectAppend(m) ==
  /\ m \in messages
  /\ m.kind = "APPEND"
  /\ m.dst \in online
  /\ m.src \in online
  /\ \/ m.term < currentTerm[m.dst]
     \/ ~PrevMatches(log[m.dst], m.prevIndex, m.prevTerm)
     \/ m.prevIndex < commitIndex[m.dst]
  /\ currentTerm' =
       IF m.term > currentTerm[m.dst]
       THEN [currentTerm EXCEPT ![m.dst] = m.term]
       ELSE currentTerm
  /\ role' =
       IF m.term >= currentTerm[m.dst]
       THEN [role EXCEPT ![m.dst] = Follower]
       ELSE role
  /\ messages' = (messages \ {m}) \cup
       {[kind |-> "APPEND_RESP", src |-> m.dst, dst |-> m.src,
         term |-> IF m.term > currentTerm[m.dst]
                  THEN m.term ELSE currentTerm[m.dst],
         requestId |-> m.requestId,
         success |-> FALSE, matchIndex |-> 0]}
  /\ UNCHANGED <<online, log, commitIndex, nextIndex, matchIndex, lastRequestId, responseAccepted, badRoleAcceptance, badSourceAcceptance, badDestinationAcceptance>>

HandleAppendResponse(m) ==
  /\ m \in messages
  /\ m.kind = "APPEND_RESP"
  /\ m.dst \in online
  /\ m.src \in online
  /\ role[m.dst] = Leader
  /\ m.term = currentTerm[m.dst]
  /\ (IF m.success
        THEN
          /\ matchIndex' = [matchIndex EXCEPT
               ![m.dst][m.src] =
                 IF m.matchIndex > @ THEN m.matchIndex ELSE @]
          /\ nextIndex' = [nextIndex EXCEPT
               ![m.dst][m.src] =
                 IF m.matchIndex + 1 > @ THEN m.matchIndex + 1 ELSE @]
        ELSE
          /\ m.requestId = lastRequestId[m.dst][m.src]
          /\ nextIndex' = [nextIndex EXCEPT
               ![m.dst][m.src] = IF @ > 1 THEN @ - 1 ELSE 1]
          /\ UNCHANGED matchIndex)
  /\ responseAccepted' = TRUE
  /\ badRoleAcceptance' = IF role[m.dst] = Leader THEN badRoleAcceptance ELSE TRUE
  /\ badSourceAcceptance' = IF m.src \in online THEN badSourceAcceptance ELSE TRUE
  /\ badDestinationAcceptance' = IF m.dst \in online THEN badDestinationAcceptance ELSE TRUE
  /\ messages' = messages \ {m}
  /\ UNCHANGED <<online, currentTerm, role, log, commitIndex, lastRequestId>>

ObserveHigherTerm(m) ==
  /\ m \in messages
  /\ m.kind = "APPEND_RESP"
  /\ m.dst \in online
  /\ m.src \in online
  /\ m.term > currentTerm[m.dst]
  /\ currentTerm' = [currentTerm EXCEPT ![m.dst] = m.term]
  /\ role' = [role EXCEPT ![m.dst] = Follower]
  /\ messages' = messages \ {m}
  /\ UNCHANGED <<online, log, commitIndex, nextIndex, matchIndex, lastRequestId, responseAccepted, badRoleAcceptance, badSourceAcceptance, badDestinationAcceptance>>

DropMessage(m) ==
  /\ m \in messages
  /\ messages' = messages \ {m}
  /\ UNCHANGED <<online, currentTerm, role, log, commitIndex, nextIndex, matchIndex, lastRequestId, responseAccepted, badRoleAcceptance, badSourceAcceptance, badDestinationAcceptance>>

AdvanceCommit(n) ==
  /\ n \in online
  /\ role[n] = Leader
  /\ \E idx \in (commitIndex[n] + 1)..Len(log[n]) :
       /\ log[n][idx] = currentTerm[n]
       /\ Cardinality({p \in Nodes \ {n} :
            matchIndex[n][p] >= idx}) + 1 >= Quorum
       /\ commitIndex' = [commitIndex EXCEPT ![n] = idx]
  /\ UNCHANGED <<online, currentTerm, role, log, nextIndex,
                 matchIndex, lastRequestId, messages, responseAccepted, badRoleAcceptance, badSourceAcceptance, badDestinationAcceptance>>

Next ==
  \/ \E s \in SUBSET Nodes : SetOnline(s)
  \/ \E n \in Nodes : AssumeLeader(n)
  \/ \E n \in Nodes : LeaderAppend(n)
  \/ \E n \in Nodes : \E p \in Nodes \ {n} : SendAppend(n, p)
  \/ \E n \in Nodes : AdvanceCommit(n)
  \/ \E m \in messages : DeliverAppend(m)
  \/ \E m \in messages : RejectAppend(m)
  \/ \E m \in messages : HandleAppendResponse(m)
  \/ \E m \in messages : ObserveHigherTerm(m)
  \/ \E m \in messages : DropMessage(m)
Spec == Init /\ [][Next]_vars

TypeOK ==
  /\ online \subseteq Nodes
  /\ currentTerm \in [Nodes -> 0..MaxTerm]
  /\ role \in [Nodes -> {Follower, Candidate, Leader}]
  /\ log \in [Nodes -> Seq(LogTerms)]
  /\ \A n \in Nodes : Len(log[n]) <= MaxLog
  /\ commitIndex \in [Nodes -> 0..MaxLog]
  /\ \A n \in Nodes : commitIndex[n] <= Len(log[n])
  /\ nextIndex \in [Nodes -> [Nodes -> 1..(MaxLog + 1)]]
  /\ matchIndex \in [Nodes -> [Nodes -> 0..MaxLog]]
  /\ lastRequestId \in [Nodes -> [Nodes -> 0..MaxRequestId]]
  /\ responseAccepted \in BOOLEAN
  /\ badRoleAcceptance \in BOOLEAN
  /\ badSourceAcceptance \in BOOLEAN
  /\ badDestinationAcceptance \in BOOLEAN
  /\ messages \subseteq
       ([kind : {"APPEND"}, src : Nodes, dst : Nodes,
         term : 0..MaxTerm, requestId : 1..MaxRequestId,
         prevIndex : 0..MaxLog, prevTerm : 0..MaxTerm,
         entries : Seq(LogTerms), leaderCommit : 0..MaxLog]
        \cup
        [kind : {"APPEND_RESP"}, src : Nodes, dst : Nodes,
         term : 0..MaxTerm, requestId : 1..MaxRequestId,
         success : BOOLEAN, matchIndex : 0..MaxLog])
  /\ \A m \in messages :
       IF m.kind = "APPEND"
       THEN Len(m.entries) <= MaxLog
       ELSE m.matchIndex <= MaxLog

NextIndexBound == \A n, p \in Nodes : nextIndex[n][p] <= MaxLog + 1
MatchIndexBound == \A n, p \in Nodes : matchIndex[n][p] <= MaxLog

CommittedPrefixAgreement ==
  \A a, b \in Nodes :
    \A i \in 1..MinSet({commitIndex[a], commitIndex[b]}) :
      log[a][i] = log[b][i]

CommitIndexBound == \A n \in Nodes : commitIndex[n] <= Len(log[n])


ResponseAcceptanceGuards ==
  responseAccepted =>
    /\ ~badRoleAcceptance
    /\ ~badSourceAcceptance
    /\ ~badDestinationAcceptance


====

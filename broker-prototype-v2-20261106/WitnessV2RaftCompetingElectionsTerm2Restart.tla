---- MODULE WitnessV2RaftCompetingElectionsTerm2Restart ----
EXTENDS Naturals, FiniteSets, TLC

CONSTANTS Nodes, CandidateA, CandidateB, VoterA, VoterB, VoterC,
          ALastTerm, ALastIndex, BLastTerm, BLastIndex,
          VoterALastTerm, VoterALastIndex, VoterBLastTerm, VoterBLastIndex,
          VoterCLastTerm, VoterCLastIndex

ASSUME Cardinality(Nodes) = 5
ASSUME CandidateA \in Nodes
ASSUME CandidateB \in Nodes
ASSUME VoterA \in Nodes
ASSUME VoterB \in Nodes
ASSUME VoterC \in Nodes
ASSUME CandidateA # CandidateB
ASSUME CandidateA # VoterA
ASSUME CandidateA # VoterB
ASSUME CandidateA # VoterC
ASSUME CandidateB # VoterA
ASSUME CandidateB # VoterB
ASSUME CandidateB # VoterC
ASSUME VoterA # VoterB
ASSUME VoterA # VoterC
ASSUME VoterB # VoterC

LastLogTerm == [n \in Nodes |->
  CASE n = CandidateA -> ALastTerm
    [] n = CandidateB -> BLastTerm
    [] n = VoterA -> VoterALastTerm
    [] n = VoterB -> VoterBLastTerm
    [] n = VoterC -> VoterCLastTerm]
LastLogIndex == [n \in Nodes |->
  CASE n = CandidateA -> ALastIndex
    [] n = CandidateB -> BLastIndex
    [] n = VoterA -> VoterALastIndex
    [] n = VoterB -> VoterBLastIndex
    [] n = VoterC -> VoterCLastIndex]

Candidates == {CandidateA, CandidateB}
SharedVoters == {VoterA, VoterB, VoterC}
Follower == "FOLLOWER"
Candidate == "CANDIDATE"
Leader == "LEADER"
NoVote == "NO_VOTE"
Quorum == 3

CandidateLogUpToDate(c, v) ==
  LastLogTerm[c] > LastLogTerm[v] \/
  (LastLogTerm[c] = LastLogTerm[v] /\ LastLogIndex[c] >= LastLogIndex[v])

VARIABLES currentTerm, role, votedFor, votesReceived, voteLedger,
          requests, responses
vars == <<currentTerm, role, votedFor, votesReceived, voteLedger,
          requests, responses>>

Grant(v, t, c) == [voter |-> v, term |-> t, candidate |-> c,
                   lastTerm |-> LastLogTerm[c], lastIndex |-> LastLogIndex[c]]
Request(c, v) == [kind |-> "REQUEST_VOTE", term |-> 2,
                  candidate |-> c, voter |-> v,
                  lastTerm |-> LastLogTerm[c], lastIndex |-> LastLogIndex[c]]
Response(v, c, t, granted) ==
  [kind |-> "VOTE_RESPONSE", term |-> t, candidate |-> c,
   voter |-> v, granted |-> granted]

Init ==
  /\ currentTerm = [n \in Nodes |-> 2]
  /\ role = [n \in Nodes |->
       IF n \in Candidates THEN Candidate ELSE Follower]
  /\ votedFor = [n \in Nodes |->
       IF n \in Candidates THEN n ELSE NoVote]
  /\ votesReceived = [n \in Nodes |->
       IF n \in Candidates THEN {n} ELSE {}]
  /\ voteLedger =
       {Grant(CandidateA, 1, CandidateA),
        Grant(CandidateB, 1, CandidateB),
        Grant(VoterA, 1, CandidateA),
        Grant(VoterB, 1, CandidateB),
        Grant(CandidateA, 2, CandidateA),
        Grant(CandidateB, 2, CandidateB)}
  /\ requests = {Request(c, v) : c \in Candidates, v \in SharedVoters}
  /\ responses =
       {Response(VoterA, CandidateA, 1, TRUE),
        Response(VoterB, CandidateB, 1, TRUE)}

GrantVote(req) ==
  /\ req \in requests
  /\ req.term = currentTerm[req.voter]
  /\ votedFor[req.voter] = NoVote
  /\ req.candidate \in Candidates
  /\ CandidateLogUpToDate(req.candidate, req.voter)
  /\ votedFor' = [votedFor EXCEPT ![req.voter] = req.candidate]
  /\ voteLedger' = voteLedger \cup {Grant(req.voter, 2, req.candidate)}
  /\ requests' = requests \ {req}
  /\ responses' = responses \cup {Response(req.voter, req.candidate, 2, TRUE)}
  /\ UNCHANGED <<currentTerm, role, votesReceived>>

DenyVote(req) ==
  /\ req \in requests
  /\ req.term = currentTerm[req.voter]
  /\ (votedFor[req.voter] # NoVote \/
       ~CandidateLogUpToDate(req.candidate, req.voter))
  /\ requests' = requests \ {req}
  /\ responses' = responses \cup {Response(req.voter, req.candidate, 2, FALSE)}
  /\ UNCHANGED <<currentTerm, role, votedFor, votesReceived, voteLedger>>

ResolveRequest(req) == GrantVote(req) \/ DenyVote(req)
ResolveAnyRequest == \E req \in requests : ResolveRequest(req)

HandleCurrentResponse(resp) ==
  /\ resp \in responses
  /\ resp.term = currentTerm[resp.candidate]
  /\ resp.candidate \in Candidates
  /\ role[resp.candidate] \in {Candidate, Leader}
  /\ responses' = responses \ {resp}
  /\ IF resp.granted
       THEN /\ votesReceived' =
                  [votesReceived EXCEPT ![resp.candidate] = @ \cup {resp.voter}]
            /\ role' =
                  [role EXCEPT ![resp.candidate] =
                    IF Cardinality(votesReceived[resp.candidate] \cup {resp.voter}) >= Quorum
                    THEN Leader ELSE @]
       ELSE /\ UNCHANGED <<votesReceived, role>>
  /\ UNCHANGED <<currentTerm, votedFor, voteLedger, requests>>

DropStaleResponse(resp) ==
  /\ resp \in responses
  /\ resp.term < currentTerm[resp.candidate]
  /\ responses' = responses \ {resp}
  /\ UNCHANGED <<currentTerm, role, votedFor, votesReceived,
                  voteLedger, requests>>

HandleAnyResponse == \E resp \in responses : HandleCurrentResponse(resp)
DropAnyStaleResponse == \E resp \in responses : DropStaleResponse(resp)

Next == ResolveAnyRequest \/ HandleAnyResponse \/ DropAnyStaleResponse
Spec == Init /\ [][Next]_vars
FairSpec ==
  Spec
  /\ WF_vars(ResolveAnyRequest)
  /\ WF_vars(HandleAnyResponse)
  /\ WF_vars(DropAnyStaleResponse)

TypeOK ==
  /\ currentTerm \in [Nodes -> {2}]
  /\ role \in [Nodes -> {Follower, Candidate, Leader}]
  /\ votedFor \in [Nodes -> Nodes \cup {NoVote}]
  /\ votesReceived \in [Nodes -> SUBSET Nodes]
  /\ voteLedger \subseteq [voter : Nodes, term : {1, 2}, candidate : Nodes,
                              lastTerm : Nat, lastIndex : Nat]
  /\ requests \subseteq [kind : {"REQUEST_VOTE"}, term : {2},
                           candidate : Candidates, voter : SharedVoters,
                           lastTerm : Nat, lastIndex : Nat]
  /\ responses \subseteq [kind : {"VOTE_RESPONSE"}, term : {1, 2},
                            candidate : Candidates, voter : SharedVoters,
                            granted : BOOLEAN]

OneVotePerTerm ==
  \A v \in Nodes : \A t \in {1, 2} :
    Cardinality({g \in voteLedger : g.voter = v /\ g.term = t}) <= 1

VoteLedgerMatchesCurrentVotes ==
  \A v \in Nodes :
    votedFor[v] = NoVote \/ Grant(v, 2, votedFor[v]) \in voteLedger

VotesRequireRecordedGrants ==
  \A c \in Candidates :
    \A v \in votesReceived[c] \ {c} :
      Grant(v, 2, c) \in voteLedger

GrantedVotesWereFresh ==
  \A g \in voteLedger :
    g.term = 2 /\ g.voter \in SharedVoters =>
      CandidateLogUpToDate(g.candidate, g.voter)

LeaderHasQuorum ==
  \A c \in Candidates :
    role[c] = Leader => Cardinality(votesReceived[c]) >= Quorum

AtMostOneLeaderPerTerm ==
  Cardinality({c \in Candidates : role[c] = Leader}) <= 1

StaleCandidateACannotWin ==
  role[CandidateA] = Leader =>
    Cardinality({v \in SharedVoters : CandidateLogUpToDate(CandidateA, v)}) >= 2

FreshCandidateBCanReachQuorum ==
  role[CandidateB] = Leader =>
    Cardinality(votesReceived[CandidateB]) >= Quorum

OldTermResponsesNeverCountInTerm2 ==
  \A c \in Candidates :
    votesReceived[c] \ {c} \subseteq
      {v \in SharedVoters : Grant(v, 2, c) \in voteLedger}

ElectionMessagesEventuallyResolved ==
  <> (requests = {} /\ responses = {})

EventuallyFreshCandidateBLeads ==
  <> (role[CandidateB] = Leader)

====

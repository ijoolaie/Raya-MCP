---- MODULE WitnessV2RaftCompetingElections ----
EXTENDS Naturals, FiniteSets, TLC

CONSTANTS Nodes, CandidateA, CandidateB, VoterA, VoterB, VoterC
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

Candidates == {CandidateA, CandidateB}
SharedVoters == {VoterA, VoterB, VoterC}

Follower == "FOLLOWER"
Candidate == "CANDIDATE"
Leader == "LEADER"
NoVote == "NO_VOTE"
Quorum == (Cardinality(Nodes) \div 2) + 1

VARIABLES currentTerm, role, votedFor, votesReceived, voteLedger,
          requests, responses
vars == <<currentTerm, role, votedFor, votesReceived, voteLedger,
          requests, responses>>

Grant(v, c) == [voter |-> v, term |-> 1, candidate |-> c]
Request(c, v) == [kind |-> "REQUEST_VOTE", term |-> 1, candidate |-> c, voter |-> v]
Response(v, c, granted) ==
  [kind |-> "VOTE_RESPONSE", term |-> 1, candidate |-> c, voter |-> v, granted |-> granted]

Init ==
  /\ currentTerm = [n \in Nodes |-> 1]
  /\ role = [n \in Nodes |-> IF n \in Candidates THEN Candidate ELSE Follower]
  /\ votedFor = [n \in Nodes |-> IF n \in Candidates THEN n ELSE NoVote]
  /\ votesReceived = [n \in Nodes |-> IF n \in Candidates THEN {n} ELSE {}]
  /\ voteLedger = {Grant(CandidateA, CandidateA), Grant(CandidateB, CandidateB)}
  /\ requests = {Request(c, v) : c \in Candidates, v \in SharedVoters}
  /\ responses = {}

GrantVote(req) ==
  /\ req \in requests
  /\ req.kind = "REQUEST_VOTE"
  /\ req.term = currentTerm[req.voter]
  /\ votedFor[req.voter] = NoVote
  /\ req.candidate \in Candidates
  /\ votedFor' = [votedFor EXCEPT ![req.voter] = req.candidate]
  /\ voteLedger' = voteLedger \cup {Grant(req.voter, req.candidate)}
  /\ requests' = requests \ {req}
  /\ responses' = responses \cup {Response(req.voter, req.candidate, TRUE)}
  /\ UNCHANGED <<currentTerm, role, votesReceived>>

DenyVote(req) ==
  /\ req \in requests
  /\ req.kind = "REQUEST_VOTE"
  /\ req.term = currentTerm[req.voter]
  /\ votedFor[req.voter] # NoVote
  /\ requests' = requests \ {req}
  /\ responses' = responses \cup {Response(req.voter, req.candidate, FALSE)}
  /\ UNCHANGED <<currentTerm, role, votedFor, votesReceived, voteLedger>>

HandleResponse(resp) ==
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

ResolveAnyRequest ==
  \E req \in requests : GrantVote(req) \/ DenyVote(req)

HandleAnyResponse ==
  \E resp \in responses : HandleResponse(resp)

Next ==
  \/ ResolveAnyRequest
  \/ HandleAnyResponse

Spec == Init /\ [][Next]_vars

FairSpec ==
  Spec
  /\ WF_vars(ResolveAnyRequest)
  /\ WF_vars(HandleAnyResponse)

TypeOK ==
  /\ currentTerm \in [Nodes -> {1}]
  /\ role \in [Nodes -> {Follower, Candidate, Leader}]
  /\ votedFor \in [Nodes -> Nodes \cup {NoVote}]
  /\ votesReceived \in [Nodes -> SUBSET Nodes]
  /\ voteLedger \subseteq [voter : Nodes, term : {1}, candidate : Nodes]
  /\ requests \subseteq [kind : {"REQUEST_VOTE"}, term : {1},
                         candidate : Candidates, voter : SharedVoters]
  /\ responses \subseteq [kind : {"VOTE_RESPONSE"}, term : {1},
                           candidate : Candidates, voter : SharedVoters,
                           granted : BOOLEAN]

OneVotePerTerm ==
  \A v \in Nodes :
    Cardinality({g \in voteLedger : g.voter = v /\ g.term = 1}) <= 1

VoteLedgerMatchesVotedFor ==
  \A v \in Nodes :
    IF votedFor[v] = NoVote
    THEN ~(\E g \in voteLedger : g.voter = v /\ g.term = 1)
    ELSE Grant(v, votedFor[v]) \in voteLedger

VotesRequireRecordedGrants ==
  \A c \in Candidates :
    \A v \in votesReceived[c] \ {c} :
      Grant(v, c) \in voteLedger

LeaderHasQuorum ==
  \A c \in Candidates :
    role[c] = Leader => Cardinality(votesReceived[c]) >= Quorum

AtMostOneLeaderPerTerm ==
  Cardinality({c \in Candidates : role[c] = Leader}) <= 1

BothCandidatesCannotWin ==
  ~(role[CandidateA] = Leader /\ role[CandidateB] = Leader)

ElectionMessagesEventuallyResolved ==
  <> (requests = {} /\ responses = {})

====
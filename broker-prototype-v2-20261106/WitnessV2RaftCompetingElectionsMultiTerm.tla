---- MODULE WitnessV2RaftCompetingElectionsMultiTerm ----
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
Quorum == 3

VARIABLES currentTerm, role, votedFor, votesReceived, voteLedger,
          requests, responses, phase
vars == <<currentTerm, role, votedFor, votesReceived, voteLedger,
          requests, responses, phase>>

Grant(v, t, c) == [voter |-> v, term |-> t, candidate |-> c]
Req(v) == [kind |-> "REQUEST_VOTE", term |-> 2,
           candidate |-> CandidateB, voter |-> v]
Resp(v, t, c, granted) ==
  [kind |-> "VOTE_RESPONSE", term |-> t,
   candidate |-> c, voter |-> v, granted |-> granted]

Init ==
  /\ currentTerm = [n \in Nodes |-> 1]
  /\ role = [n \in Nodes |->
       IF n \in Candidates THEN Candidate ELSE Follower]
  /\ votedFor = [n \in Nodes |->
       IF n = CandidateA THEN CandidateA
       ELSE IF n = CandidateB THEN CandidateB
       ELSE IF n = VoterA THEN CandidateA
       ELSE IF n = VoterB THEN CandidateB
       ELSE NoVote]
  /\ votesReceived = [n \in Nodes |->
       IF n = CandidateA THEN {CandidateA}
       ELSE IF n = CandidateB THEN {CandidateB}
       ELSE {}]
  /\ voteLedger = {Grant(CandidateA, 1, CandidateA),
                    Grant(CandidateB, 1, CandidateB),
                    Grant(VoterA, 1, CandidateA),
                    Grant(VoterB, 1, CandidateB)}
  /\ requests = {Req(v) : v \in SharedVoters}
  /\ responses = {Resp(VoterA, 1, CandidateA, TRUE),
                   Resp(VoterB, 1, CandidateB, TRUE)}
  /\ phase = "TERM1_COMPETITION"

AdvanceCandidateB ==
  /\ phase = "TERM1_COMPETITION"
  /\ currentTerm[CandidateB] = 1
  /\ currentTerm' = [currentTerm EXCEPT ![CandidateB] = 2]
  /\ role' = [role EXCEPT ![CandidateB] = Candidate]
  /\ votedFor' = [votedFor EXCEPT ![CandidateB] = CandidateB]
  /\ votesReceived' = [votesReceived EXCEPT ![CandidateB] = {CandidateB}]
  /\ voteLedger' = voteLedger \cup {Grant(CandidateB, 2, CandidateB)}
  /\ phase' = "TERM2_REQUESTS"
  /\ UNCHANGED <<requests, responses>>

GrantTerm2Request(v) ==
  /\ phase \in {"TERM2_REQUESTS", "TERM2_RESPONSES"}
  /\ v \in SharedVoters
  /\ Req(v) \in requests
  /\ currentTerm[v] <= 2
  /\ currentTerm' = [currentTerm EXCEPT ![v] = 2]
  /\ role' = [role EXCEPT ![v] = Follower]
  /\ votedFor' = [votedFor EXCEPT ![v] = CandidateB]
  /\ voteLedger' = voteLedger \cup {Grant(v, 2, CandidateB)}
  /\ requests' = requests \ {Req(v)}
  /\ responses' = responses \cup {Resp(v, 2, CandidateB, TRUE)}
  /\ phase' = "TERM2_RESPONSES"
  /\ UNCHANGED votesReceived

HandleTerm1Response(resp) ==
  /\ resp \in responses
  /\ resp.term = 1
  /\ resp.candidate \in Candidates
  /\ resp.term = currentTerm[resp.candidate]
  /\ resp.granted
  /\ role[resp.candidate] = Candidate
  /\ responses' = responses \ {resp}
  /\ votesReceived' =
       [votesReceived EXCEPT ![resp.candidate] = @ \cup {resp.voter}]
  /\ role' =
       [role EXCEPT ![resp.candidate] =
          IF Cardinality(votesReceived[resp.candidate] \cup {resp.voter}) >= Quorum
          THEN Leader ELSE @]
  /\ UNCHANGED <<currentTerm, votedFor, voteLedger, requests, phase>>

ObserveHigherTermA ==
  /\ currentTerm[CandidateB] = 2
  /\ currentTerm[CandidateA] = 1
  /\ currentTerm' = [currentTerm EXCEPT ![CandidateA] = 2]
  /\ role' = [role EXCEPT ![CandidateA] = Follower]
  /\ votedFor' = [votedFor EXCEPT ![CandidateA] = NoVote]
  /\ votesReceived' = [votesReceived EXCEPT ![CandidateA] = {}]
  /\ UNCHANGED <<voteLedger, requests, responses, phase>>

DropStaleResponse(resp) ==
  /\ resp \in responses
  /\ resp.term < currentTerm[resp.candidate]
  /\ responses' = responses \ {resp}
  /\ UNCHANGED <<currentTerm, role, votedFor, votesReceived,
                  voteLedger, requests, phase>>

HandleTerm2Response(resp) ==
  /\ resp \in responses
  /\ resp.term = 2
  /\ resp.candidate = CandidateB
  /\ currentTerm[CandidateB] = 2
  /\ role[CandidateB] \in {Candidate, Leader}
  /\ resp.granted
  /\ responses' = responses \ {resp}
  /\ votesReceived' =
       [votesReceived EXCEPT ![CandidateB] = @ \cup {resp.voter}]
  /\ role' =
       [role EXCEPT ![CandidateB] =
          IF Cardinality(votesReceived[CandidateB] \cup {resp.voter}) >= Quorum
          THEN Leader ELSE @]
  /\ UNCHANGED <<currentTerm, votedFor, voteLedger, requests, phase>>

Next ==
  \/ AdvanceCandidateB
  \/ \E v \in SharedVoters : GrantTerm2Request(v)
  \/ \E r \in responses : HandleTerm1Response(r)
  \/ \E r \in responses : ObserveHigherTermA
  \/ \E r \in responses : DropStaleResponse(r)
  \/ \E r \in responses : HandleTerm2Response(r)

Spec == Init /\ [][Next]_vars
FairSpec ==
  Spec
  /\ WF_vars(AdvanceCandidateB)
  /\ WF_vars(\E v \in SharedVoters : GrantTerm2Request(v))
  /\ WF_vars(\E r \in responses : HandleTerm1Response(r))
  /\ WF_vars(\E r \in responses : ObserveHigherTermA)
  /\ WF_vars(\E r \in responses : DropStaleResponse(r))
  /\ WF_vars(\E r \in responses : HandleTerm2Response(r))

TypeOK ==
  /\ currentTerm \in [Nodes -> {1, 2}]
  /\ role \in [Nodes -> {Follower, Candidate, Leader}]
  /\ votedFor \in [Nodes -> Nodes \cup {NoVote}]
  /\ votesReceived \in [Nodes -> SUBSET Nodes]
  /\ voteLedger \subseteq [voter : Nodes, term : {1, 2}, candidate : Nodes]
  /\ requests \subseteq [kind : {"REQUEST_VOTE"}, term : {2},
                           candidate : {CandidateB}, voter : SharedVoters]
  /\ responses \subseteq [kind : {"VOTE_RESPONSE"}, term : {1, 2},
                            candidate : Candidates, voter : SharedVoters,
                            granted : BOOLEAN]
  /\ phase \in {"TERM1_COMPETITION", "TERM2_REQUESTS", "TERM2_RESPONSES"}

OneVotePerTerm ==
  \A v \in Nodes : \A t \in {1, 2} :
    Cardinality({g \in voteLedger : g.voter = v /\ g.term = t}) <= 1

VoteLedgerMatchesCurrentVotes ==
  \A v \in Nodes :
    votedFor[v] = NoVote \/
    Grant(v, currentTerm[v], votedFor[v]) \in voteLedger

VotesRequireGrants ==
  \A c \in Candidates :
    \A v \in votesReceived[c] \ {c} :
      Grant(v, currentTerm[c], c) \in voteLedger

LeaderHasQuorum ==
  \A c \in Candidates :
    role[c] = Leader => Cardinality(votesReceived[c]) >= Quorum

AtMostOneLeaderPerTerm ==
  \A t \in {1, 2} :
    Cardinality({c \in Candidates :
      role[c] = Leader /\ currentTerm[c] = t}) <= 1

HigherTermObservationStepsDownA ==
  currentTerm[CandidateA] = 2 =>
    role[CandidateA] = Follower /\ votedFor[CandidateA] = NoVote

OldTermResponsesCannotPolluteTerm2 ==
  currentTerm[CandidateA] = 2 =>
    votesReceived[CandidateA] = {}

Term2LeaderHasDistinctTerm2Grants ==
  role[CandidateB] = Leader =>
    \A v \in votesReceived[CandidateB] \ {CandidateB} :
      Grant(v, 2, CandidateB) \in voteLedger

EventuallyAllMessagesResolved ==
  <> (requests = {} /\ responses = {})

====

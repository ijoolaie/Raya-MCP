---- MODULE WitnessV2RaftElectionAllFourResponders ----
EXTENDS Naturals, FiniteSets, TLC

CONSTANTS Nodes, CandidateNode, ResponderA, ResponderB, ResponderC, ResponderD
ASSUME Cardinality(Nodes) = 5
ASSUME CandidateNode \in Nodes
ASSUME ResponderA \in Nodes
ASSUME ResponderB \in Nodes
ASSUME ResponderC \in Nodes
ASSUME ResponderD \in Nodes
ASSUME CandidateNode # ResponderA
ASSUME CandidateNode # ResponderB
ASSUME CandidateNode # ResponderC
ASSUME CandidateNode # ResponderD
ASSUME ResponderA # ResponderB
ASSUME ResponderA # ResponderC
ASSUME ResponderA # ResponderD
ASSUME ResponderB # ResponderC
ASSUME ResponderB # ResponderD
ASSUME ResponderC # ResponderD
Responders == {ResponderA, ResponderB, ResponderC, ResponderD}

Follower == "FOLLOWER"
Candidate == "CANDIDATE"
Leader == "LEADER"
NoVote == "NO_VOTE"
Quorum == (Cardinality(Nodes) \div 2) + 1

VARIABLES currentTerm, role, votedFor, votesReceived, voteLedger,
          messages, respondersAdvanced, firstOldResponseHandled,
          higherTermObserved, newElectionStarted, termTwoGrantsIssued,
          staleMessagesDropped, currentResponseHandled
vars == <<currentTerm, role, votedFor, votesReceived, voteLedger,
          messages, respondersAdvanced, firstOldResponseHandled,
          higherTermObserved, newElectionStarted, termTwoGrantsIssued,
          staleMessagesDropped, currentResponseHandled>>

OldGrant(v) == [voter |-> v, term |-> 1, candidate |-> CandidateNode]
NewGrant(v) == [voter |-> v, term |-> 2, candidate |-> CandidateNode]

Init ==
  /\ currentTerm = [n \in Nodes |-> IF n = CandidateNode \/ n \in Responders THEN 1 ELSE 0]
  /\ role = [n \in Nodes |-> IF n = CandidateNode THEN Candidate ELSE Follower]
  /\ votedFor = [n \in Nodes |-> IF n = CandidateNode \/ n \in Responders THEN CandidateNode ELSE NoVote]
  /\ votesReceived = [n \in Nodes |-> IF n = CandidateNode THEN {CandidateNode} ELSE {}]
  /\ voteLedger = {OldGrant(CandidateNode)} \cup {OldGrant(v) : v \in Responders}
  /\ messages =
       {[kind |-> "VOTE_RESPONSE", msgId |-> 1, src |-> ResponderA, dst |-> CandidateNode, term |-> 1, granted |-> TRUE],
        [kind |-> "VOTE_RESPONSE", msgId |-> 2, src |-> ResponderA, dst |-> CandidateNode, term |-> 1, granted |-> TRUE],
        [kind |-> "VOTE_RESPONSE", msgId |-> 3, src |-> ResponderB, dst |-> CandidateNode, term |-> 1, granted |-> TRUE],
        [kind |-> "VOTE_RESPONSE", msgId |-> 8, src |-> ResponderB, dst |-> CandidateNode, term |-> 1, granted |-> TRUE],
        [kind |-> "VOTE_RESPONSE", msgId |-> 4, src |-> ResponderC, dst |-> CandidateNode, term |-> 1, granted |-> TRUE],
        [kind |-> "VOTE_RESPONSE", msgId |-> 5, src |-> ResponderC, dst |-> CandidateNode, term |-> 1, granted |-> TRUE],
        [kind |-> "VOTE_RESPONSE", msgId |-> 6, src |-> ResponderD, dst |-> CandidateNode, term |-> 1, granted |-> TRUE],
        [kind |-> "VOTE_RESPONSE", msgId |-> 7, src |-> ResponderD, dst |-> CandidateNode, term |-> 1, granted |-> TRUE]}
  /\ respondersAdvanced = {}
  /\ firstOldResponseHandled = FALSE
  /\ higherTermObserved = FALSE
  /\ newElectionStarted = FALSE
  /\ termTwoGrantsIssued = {}
  /\ staleMessagesDropped = {}
  /\ currentResponseHandled = {}

AdvanceResponder(v) ==
  /\ v \in Responders \ respondersAdvanced
  /\ currentTerm[v] = 1
  /\ currentTerm' = [currentTerm EXCEPT ![v] = 2]
  /\ role' = [role EXCEPT ![v] = Follower]
  /\ votedFor' = [votedFor EXCEPT ![v] = NoVote]
  /\ respondersAdvanced' = respondersAdvanced \cup {v}
  /\ UNCHANGED <<votesReceived, voteLedger, messages, firstOldResponseHandled,
                  higherTermObserved, newElectionStarted, termTwoGrantsIssued,
                  staleMessagesDropped, currentResponseHandled>>

HandleFirstOldResponse ==
  /\ ~firstOldResponseHandled
  /\ ResponderA \in respondersAdvanced
  /\ role[CandidateNode] = Candidate
  /\ \E m \in messages :
       /\ m.msgId = 1
       /\ m.src = ResponderA
       /\ m.term = currentTerm[CandidateNode]
       /\ m.granted
       /\ votesReceived' = [votesReceived EXCEPT ![CandidateNode] = @ \cup {m.src}]
       /\ role' = role
       /\ messages' = messages \ {m}
  /\ firstOldResponseHandled' = TRUE
  /\ UNCHANGED <<currentTerm, votedFor, voteLedger, respondersAdvanced,
                  higherTermObserved, newElectionStarted, termTwoGrantsIssued,
                  staleMessagesDropped, currentResponseHandled>>

ObserveHigherTerm ==
  /\ firstOldResponseHandled
  /\ role[CandidateNode] \in {Candidate, Leader}
  /\ \E v \in respondersAdvanced : currentTerm[v] > currentTerm[CandidateNode]
  /\ currentTerm' = [currentTerm EXCEPT ![CandidateNode] = 2]
  /\ role' = [role EXCEPT ![CandidateNode] = Follower]
  /\ votedFor' = [votedFor EXCEPT ![CandidateNode] = NoVote]
  /\ votesReceived' = [votesReceived EXCEPT ![CandidateNode] = {}]
  /\ higherTermObserved' = TRUE
  /\ UNCHANGED <<voteLedger, messages, respondersAdvanced, firstOldResponseHandled,
                  newElectionStarted, termTwoGrantsIssued, staleMessagesDropped,
                  currentResponseHandled>>

StartTermTwoElection ==
  /\ higherTermObserved
  /\ ~newElectionStarted
  /\ role[CandidateNode] = Follower
  /\ currentTerm[CandidateNode] = 2
  /\ role' = [role EXCEPT ![CandidateNode] = Candidate]
  /\ votedFor' = [votedFor EXCEPT ![CandidateNode] = CandidateNode]
  /\ votesReceived' = [votesReceived EXCEPT ![CandidateNode] = {CandidateNode}]
  /\ newElectionStarted' = TRUE
  /\ UNCHANGED <<currentTerm, voteLedger, messages, respondersAdvanced,
                  firstOldResponseHandled, higherTermObserved, termTwoGrantsIssued,
                  staleMessagesDropped, currentResponseHandled>>

IssueTermTwoGrants ==
  /\ newElectionStarted
  /\ termTwoGrantsIssued = {}
  /\ respondersAdvanced = Responders
  /\ voteLedger' = voteLedger \cup {NewGrant(v) : v \in Responders}
  /\ votedFor' = [v \in Nodes |-> IF v \in Responders THEN CandidateNode ELSE votedFor[v]]
  /\ termTwoGrantsIssued' = Responders
  /\ messages' = messages \cup
       {[kind |-> "VOTE_RESPONSE", msgId |-> 9, src |-> ResponderA, dst |-> CandidateNode, term |-> 2, granted |-> TRUE],
        [kind |-> "VOTE_RESPONSE", msgId |-> 10, src |-> ResponderA, dst |-> CandidateNode, term |-> 2, granted |-> TRUE],
        [kind |-> "VOTE_RESPONSE", msgId |-> 11, src |-> ResponderB, dst |-> CandidateNode, term |-> 2, granted |-> TRUE],
        [kind |-> "VOTE_RESPONSE", msgId |-> 12, src |-> ResponderB, dst |-> CandidateNode, term |-> 2, granted |-> TRUE],
        [kind |-> "VOTE_RESPONSE", msgId |-> 13, src |-> ResponderC, dst |-> CandidateNode, term |-> 2, granted |-> TRUE],
        [kind |-> "VOTE_RESPONSE", msgId |-> 14, src |-> ResponderC, dst |-> CandidateNode, term |-> 2, granted |-> TRUE],
        [kind |-> "VOTE_RESPONSE", msgId |-> 15, src |-> ResponderD, dst |-> CandidateNode, term |-> 2, granted |-> TRUE],
        [kind |-> "VOTE_RESPONSE", msgId |-> 16, src |-> ResponderD, dst |-> CandidateNode, term |-> 2, granted |-> TRUE]}
  /\ UNCHANGED <<currentTerm, role, votesReceived, respondersAdvanced,
                  firstOldResponseHandled, higherTermObserved, newElectionStarted,
                  staleMessagesDropped, currentResponseHandled>>

DropOldResponse(m) ==
  /\ newElectionStarted
  /\ m \in messages
  /\ m.msgId \in {2, 3, 4, 5, 6, 7, 8}
  /\ m.term = 1
  /\ m.term < currentTerm[CandidateNode]
  /\ messages' = messages \ {m}
  /\ staleMessagesDropped' = staleMessagesDropped \cup {m.msgId}
  /\ UNCHANGED <<currentTerm, role, votedFor, votesReceived, voteLedger,
                  respondersAdvanced, firstOldResponseHandled, higherTermObserved,
                  newElectionStarted, termTwoGrantsIssued, currentResponseHandled>>

HandleCurrentTermResponse(m) ==
  /\ newElectionStarted
  /\ termTwoGrantsIssued = Responders
  /\ role[CandidateNode] \in {Candidate, Leader}
  /\ m \in messages
  /\ m.msgId \in {9, 10, 11, 12, 13, 14, 15, 16}
  /\ m.term = currentTerm[CandidateNode]
  /\ m.granted
  /\ votesReceived' = [votesReceived EXCEPT ![CandidateNode] = @ \cup {m.src}]
  /\ role' = [role EXCEPT ![CandidateNode] =
       IF Cardinality(votesReceived[CandidateNode] \cup {m.src}) >= Quorum THEN Leader ELSE @]
  /\ messages' = messages \ {m}
  /\ currentResponseHandled' = currentResponseHandled \cup {m.src}
  /\ UNCHANGED <<currentTerm, votedFor, voteLedger, respondersAdvanced,
                  firstOldResponseHandled, higherTermObserved, newElectionStarted,
                  termTwoGrantsIssued, staleMessagesDropped>>

DropDuplicateCurrentTermResponse(m) ==
  /\ newElectionStarted
  /\ m \in messages
  /\ m.msgId \in {9, 10, 11, 12, 13, 14, 15, 16}
  /\ m.term = currentTerm[CandidateNode]
  /\ m.src \in currentResponseHandled
  /\ messages' = messages \ {m}
  /\ UNCHANGED <<currentTerm, role, votedFor, votesReceived, voteLedger,
                  respondersAdvanced, firstOldResponseHandled, higherTermObserved,
                  newElectionStarted, termTwoGrantsIssued, staleMessagesDropped,
                  currentResponseHandled>>
Next ==
  \/ \E v \in Responders : AdvanceResponder(v)
  \/ HandleFirstOldResponse
  \/ ObserveHigherTerm
  \/ StartTermTwoElection
  \/ IssueTermTwoGrants
  \/ \E m \in messages : DropOldResponse(m)
  \/ \E m \in messages : HandleCurrentTermResponse(m)
  \/ \E m \in messages : DropDuplicateCurrentTermResponse(m)

HandleAnyCurrentTermResponse ==
  \E m \in messages : HandleCurrentTermResponse(m)

DropAnyOldResponse ==
  \E m \in messages : DropOldResponse(m)

Spec == Init /\ [][Next]_vars

FairSpec == Spec
  /\ WF_vars(AdvanceResponder(ResponderA))
  /\ WF_vars(AdvanceResponder(ResponderB))
  /\ WF_vars(AdvanceResponder(ResponderC))
  /\ WF_vars(AdvanceResponder(ResponderD))
  /\ WF_vars(HandleFirstOldResponse)
  /\ WF_vars(ObserveHigherTerm)
  /\ WF_vars(StartTermTwoElection)
  /\ WF_vars(IssueTermTwoGrants)
  /\ WF_vars(HandleAnyCurrentTermResponse)
  /\ WF_vars(DropAnyOldResponse)

EventuallyAllFourRespondersCounted == <> (currentResponseHandled = Responders)
EventuallyAllOldResponsesDropped == <> (staleMessagesDropped = {2, 3, 4, 5, 6, 7, 8})

TypeOK ==
  /\ currentTerm \in [Nodes -> 0..2]
  /\ role \in [Nodes -> {Follower, Candidate, Leader}]
  /\ votedFor \in [Nodes -> (Nodes \cup {NoVote})]
  /\ votesReceived \in [Nodes -> SUBSET Nodes]
  /\ voteLedger \subseteq [voter : Nodes, term : 1..2, candidate : Nodes]
  /\ messages \subseteq [kind : {"VOTE_RESPONSE"}, msgId : 1..16, src : Nodes, dst : Nodes, term : 1..2, granted : BOOLEAN]
  /\ respondersAdvanced \subseteq Responders
  /\ firstOldResponseHandled \in BOOLEAN
  /\ higherTermObserved \in BOOLEAN
  /\ newElectionStarted \in BOOLEAN
  /\ termTwoGrantsIssued \subseteq Responders
  /\ staleMessagesDropped \subseteq {2, 3, 4, 5, 6, 7, 8}
  /\ currentResponseHandled \subseteq Responders

OldTermResponsesNeverCountAsTermTwoVotes ==
  newElectionStarted =>
    /\ currentTerm[CandidateNode] = 2
    /\ votesReceived[CandidateNode] \subseteq {CandidateNode} \cup Responders
    /\ \A v \in Responders : v \in votesReceived[CandidateNode] => v \in currentResponseHandled

NewTermVotesRequireNewTermGrants ==
  \A v \in Responders :
    v \in currentResponseHandled => NewGrant(v) \in voteLedger

StaleResponsesDroppedWithoutChangingTermTwoVoteSet ==
  staleMessagesDropped # {} =>
    /\ newElectionStarted
    /\ currentTerm[CandidateNode] = 2
    /\ votesReceived[CandidateNode] = {CandidateNode} \cup currentResponseHandled
    /\ role[CandidateNode] = IF Cardinality(votesReceived[CandidateNode]) >= Quorum THEN Leader ELSE Candidate

DuplicateCurrentTermResponsesDoNotInflateVotes ==
  newElectionStarted => votesReceived[CandidateNode] = {CandidateNode} \cup currentResponseHandled

TermTwoQuorumHasDistinctCurrentTermGrants ==
  role[CandidateNode] = Leader /\ currentTerm[CandidateNode] = 2 =>
    /\ Cardinality(votesReceived[CandidateNode]) >= Quorum
    /\ \A v \in votesReceived[CandidateNode] \ {CandidateNode} : NewGrant(v) \in voteLedger
    /\ currentResponseHandled = votesReceived[CandidateNode] \ {CandidateNode}

====
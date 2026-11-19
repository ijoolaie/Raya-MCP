---- MODULE WitnessV2RaftElectionDuplicateMixedTermResponses ----
EXTENDS Naturals, FiniteSets, TLC

CONSTANTS Nodes, CandidateNode, ResponderA, ResponderB
ASSUME Cardinality(Nodes) = 3
ASSUME CandidateNode \in Nodes
ASSUME ResponderA \in Nodes
ASSUME ResponderB \in Nodes
ASSUME CandidateNode # ResponderA
ASSUME CandidateNode # ResponderB
ASSUME ResponderA # ResponderB

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
  /\ currentTerm = [n \in Nodes |-> IF n = CandidateNode \/ n = ResponderA \/ n = ResponderB THEN 1 ELSE 0]
  /\ role = [n \in Nodes |-> IF n = CandidateNode THEN Candidate ELSE Follower]
  /\ votedFor = [n \in Nodes |-> IF n = CandidateNode \/ n = ResponderA \/ n = ResponderB THEN CandidateNode ELSE NoVote]
  /\ votesReceived = [n \in Nodes |-> IF n = CandidateNode THEN {CandidateNode} ELSE {}]
  /\ voteLedger = {OldGrant(CandidateNode), OldGrant(ResponderA), OldGrant(ResponderB)}
  /\ messages =
       {[kind |-> "VOTE_RESPONSE", msgId |-> 1, src |-> ResponderA, dst |-> CandidateNode, term |-> 1, granted |-> TRUE],
        [kind |-> "VOTE_RESPONSE", msgId |-> 2, src |-> ResponderA, dst |-> CandidateNode, term |-> 1, granted |-> TRUE],
        [kind |-> "VOTE_RESPONSE", msgId |-> 3, src |-> ResponderB, dst |-> CandidateNode, term |-> 1, granted |-> TRUE]}
  /\ respondersAdvanced = {}
  /\ firstOldResponseHandled = FALSE
  /\ higherTermObserved = FALSE
  /\ newElectionStarted = FALSE
  /\ termTwoGrantsIssued = {}
  /\ staleMessagesDropped = {}
  /\ currentResponseHandled = {}

AdvanceResponder(v) ==
  /\ v \in {ResponderA, ResponderB} \ respondersAdvanced
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
       /\ role' = [role EXCEPT ![CandidateNode] = Leader]
       /\ messages' = messages \ {m}
  /\ firstOldResponseHandled' = TRUE
  /\ UNCHANGED <<currentTerm, votedFor, voteLedger, respondersAdvanced,
                  higherTermObserved, newElectionStarted, termTwoGrantsIssued,
                  staleMessagesDropped, currentResponseHandled>>

ObserveHigherTerm ==
  /\ firstOldResponseHandled
  /\ role[CandidateNode] = Leader
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
  /\ respondersAdvanced = {ResponderA, ResponderB}
  /\ voteLedger' = voteLedger \cup {NewGrant(ResponderA), NewGrant(ResponderB)}
  /\ votedFor' = [votedFor EXCEPT ![ResponderA] = CandidateNode, ![ResponderB] = CandidateNode]
  /\ termTwoGrantsIssued' = {ResponderA, ResponderB}
  /\ messages' = messages \cup
       {[kind |-> "VOTE_RESPONSE", msgId |-> 4, src |-> ResponderA, dst |-> CandidateNode, term |-> 2, granted |-> TRUE],
        [kind |-> "VOTE_RESPONSE", msgId |-> 5, src |-> ResponderB, dst |-> CandidateNode, term |-> 2, granted |-> TRUE],
        [kind |-> "VOTE_RESPONSE", msgId |-> 6, src |-> ResponderA, dst |-> CandidateNode, term |-> 2, granted |-> TRUE],
        [kind |-> "VOTE_RESPONSE", msgId |-> 7, src |-> ResponderB, dst |-> CandidateNode, term |-> 2, granted |-> TRUE]}
  /\ UNCHANGED <<currentTerm, role, votesReceived, respondersAdvanced,
                  firstOldResponseHandled, higherTermObserved, newElectionStarted,
                  staleMessagesDropped, currentResponseHandled>>

DropOldResponse(m) ==
  /\ newElectionStarted
  /\ m \in messages
  /\ m.msgId \in {2, 3}
  /\ m.term = 1
  /\ m.term < currentTerm[CandidateNode]
  /\ messages' = messages \ {m}
  /\ staleMessagesDropped' = staleMessagesDropped \cup {m.msgId}
  /\ UNCHANGED <<currentTerm, role, votedFor, votesReceived, voteLedger,
                  respondersAdvanced, firstOldResponseHandled, higherTermObserved,
                  newElectionStarted, termTwoGrantsIssued, currentResponseHandled>>

HandleCurrentTermResponse(m) ==
  /\ newElectionStarted
  /\ termTwoGrantsIssued = {ResponderA, ResponderB}
  /\ role[CandidateNode] = Candidate
  /\ m \in messages
  /\ m.msgId \in {4, 5, 6, 7}
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
  /\ m.msgId \in {4, 5, 6, 7}
  /\ m.term = currentTerm[CandidateNode]
  /\ m.src \in currentResponseHandled
  /\ messages' = messages \ {m}
  /\ UNCHANGED <<currentTerm, role, votedFor, votesReceived, voteLedger,
                  respondersAdvanced, firstOldResponseHandled, higherTermObserved,
                  newElectionStarted, termTwoGrantsIssued, staleMessagesDropped,
                  currentResponseHandled>>
Next ==
  \/ \E v \in {ResponderA, ResponderB} : AdvanceResponder(v)
  \/ HandleFirstOldResponse
  \/ ObserveHigherTerm
  \/ StartTermTwoElection
  \/ IssueTermTwoGrants
  \/ \E m \in messages : DropOldResponse(m)
  \/ \E m \in messages : HandleCurrentTermResponse(m)
  \/ \E m \in messages : DropDuplicateCurrentTermResponse(m)

Spec == Init /\ [][Next]_vars

TypeOK ==
  /\ currentTerm \in [Nodes -> 0..2]
  /\ role \in [Nodes -> {Follower, Candidate, Leader}]
  /\ votedFor \in [Nodes -> (Nodes \cup {NoVote})]
  /\ votesReceived \in [Nodes -> SUBSET Nodes]
  /\ voteLedger \subseteq [voter : Nodes, term : 1..2, candidate : Nodes]
  /\ messages \subseteq [kind : {"VOTE_RESPONSE"}, msgId : 1..7, src : Nodes, dst : Nodes, term : 1..2, granted : BOOLEAN]
  /\ respondersAdvanced \subseteq {ResponderA, ResponderB}
  /\ firstOldResponseHandled \in BOOLEAN
  /\ higherTermObserved \in BOOLEAN
  /\ newElectionStarted \in BOOLEAN
  /\ termTwoGrantsIssued \subseteq {ResponderA, ResponderB}
  /\ staleMessagesDropped \subseteq {2, 3}
  /\ currentResponseHandled \subseteq {ResponderA, ResponderB}

OldTermResponsesNeverCountAsTermTwoVotes ==
  newElectionStarted =>
    /\ currentTerm[CandidateNode] = 2
    /\ votesReceived[CandidateNode] \subseteq {CandidateNode, ResponderA, ResponderB}
    /\ (ResponderA \in votesReceived[CandidateNode] => ResponderA \in currentResponseHandled)
    /\ (ResponderB \in votesReceived[CandidateNode] => ResponderB \in currentResponseHandled)

NewTermVotesRequireNewTermGrants ==
  \A v \in {ResponderA, ResponderB} :
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

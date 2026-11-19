---- MODULE WitnessV2StateMachine ----
EXTENDS Naturals, FiniteSets

CONSTANTS Ops, DescriptorDigests, RequestDigests, TerminalDigests, AuthIds,
          MaxSequence

ASSUME Ops # {}
ASSUME DescriptorDigests # {}
ASSUME RequestDigests # {}
ASSUME TerminalDigests # {}
ASSUME AuthIds # {}
ASSUME MaxSequence \in Nat

OpStates == {"ABSENT", "RESERVED", "EVIDENCE_COMMITTED", "ABORTED"}
NamespaceStates == {"ACTIVE", "RECOVERY_LOCKED", "RETIRED"}

VARIABLES sequence, opState, opSequence, descriptorDigest,
          reserveDigest, terminalDigest, authorizationId,
          unresolved, namespaceState, fenceEpoch, logIndex

vars == <<sequence, opState, opSequence, descriptorDigest,
          reserveDigest, terminalDigest, authorizationId,
          unresolved, namespaceState, fenceEpoch, logIndex>>

Init ==
  /\ sequence = 0
  /\ opState = [o \in Ops |-> "ABSENT"]
  /\ opSequence = [o \in Ops |-> 0]
  /\ descriptorDigest = [o \in Ops |-> "NONE"]
  /\ reserveDigest = [o \in Ops |-> "NONE"]
  /\ terminalDigest = [o \in Ops |-> "NONE"]
  /\ authorizationId = [o \in Ops |-> "NONE"]
  /\ unresolved = "NONE"
  /\ namespaceState = "ACTIVE"
  /\ fenceEpoch = 1
  /\ logIndex = 0

Reserve(o, expected, dd, rd, suppliedFence) ==
  /\ o \in Ops
  /\ expected \in 0..MaxSequence
  /\ dd \in DescriptorDigests
  /\ rd \in RequestDigests
  /\ suppliedFence = fenceEpoch
  /\ namespaceState = "ACTIVE"
  /\ unresolved = "NONE"
  /\ opState[o] = "ABSENT"
  /\ expected = sequence
  /\ sequence < MaxSequence
  /\ sequence' = sequence + 1
  /\ opState' = [opState EXCEPT ![o] = "RESERVED"]
  /\ opSequence' = [opSequence EXCEPT ![o] = sequence + 1]
  /\ descriptorDigest' = [descriptorDigest EXCEPT ![o] = dd]
  /\ reserveDigest' = [reserveDigest EXCEPT ![o] = rd]
  /\ terminalDigest' = [terminalDigest EXCEPT ![o] = "NONE"]
  /\ authorizationId' = [authorizationId EXCEPT ![o] = "NONE"]
  /\ unresolved' = o
  /\ namespaceState' = namespaceState
  /\ fenceEpoch' = fenceEpoch
  /\ logIndex' = logIndex + 1

Finalize(o, dd, td, suppliedFence, attestationVerified) ==
  /\ o \in Ops
  /\ dd \in DescriptorDigests
  /\ td \in TerminalDigests
  /\ suppliedFence = fenceEpoch
  /\ namespaceState = "ACTIVE"
  /\ unresolved = o
  /\ opState[o] = "RESERVED"
  /\ descriptorDigest[o] = dd
  /\ attestationVerified = TRUE
  /\ opState' = [opState EXCEPT ![o] = "EVIDENCE_COMMITTED"]
  /\ terminalDigest' = [terminalDigest EXCEPT ![o] = td]
  /\ unresolved' = "NONE"
  /\ sequence' = sequence
  /\ UNCHANGED <<opSequence, descriptorDigest, reserveDigest,
                  authorizationId, namespaceState, fenceEpoch>>
  /\ logIndex' = logIndex + 1

Abort(o, dd, td, aid, suppliedFence, authorizationVerified) ==
  /\ o \in Ops
  /\ dd \in DescriptorDigests
  /\ td \in TerminalDigests
  /\ aid \in AuthIds
  /\ suppliedFence = fenceEpoch
  /\ namespaceState = "ACTIVE"
  /\ unresolved = o
  /\ opState[o] = "RESERVED"
  /\ descriptorDigest[o] = dd
  /\ authorizationVerified = TRUE
  /\ opState' = [opState EXCEPT ![o] = "ABORTED"]
  /\ terminalDigest' = [terminalDigest EXCEPT ![o] = td]
  /\ authorizationId' = [authorizationId EXCEPT ![o] = aid]
  /\ unresolved' = "NONE"
  /\ sequence' = sequence
  /\ UNCHANGED <<opSequence, descriptorDigest, reserveDigest,
                  namespaceState, fenceEpoch>>
  /\ logIndex' = logIndex + 1

AdvanceFence ==
  /\ namespaceState = "ACTIVE"
  /\ fenceEpoch' = fenceEpoch + 1
  /\ UNCHANGED <<sequence, opState, opSequence, descriptorDigest,
                  reserveDigest, terminalDigest, authorizationId,
                  unresolved, namespaceState, logIndex>>

LockRecovery ==
  /\ namespaceState = "ACTIVE"
  /\ namespaceState' = "RECOVERY_LOCKED"
  /\ UNCHANGED <<sequence, opState, opSequence, descriptorDigest,
                  reserveDigest, terminalDigest, authorizationId,
                  unresolved, fenceEpoch, logIndex>>

Retire ==
  /\ namespaceState = "ACTIVE"
  /\ unresolved = "NONE"
  /\ namespaceState' = "RETIRED"
  /\ UNCHANGED <<sequence, opState, opSequence, descriptorDigest,
                  reserveDigest, terminalDigest, authorizationId,
                  unresolved, fenceEpoch, logIndex>>

Next ==
  \/ \E o \in Ops, dd \in DescriptorDigests, rd \in RequestDigests :
       Reserve(o, sequence, dd, rd, fenceEpoch)
  \/ \E o \in Ops, dd \in DescriptorDigests, td \in TerminalDigests :
       Finalize(o, dd, td, fenceEpoch, TRUE)
  \/ \E o \in Ops, dd \in DescriptorDigests, td \in TerminalDigests,
       aid \in AuthIds :
       Abort(o, dd, td, aid, fenceEpoch, TRUE)
  \/ AdvanceFence
  \/ LockRecovery
  \/ Retire

Spec == Init /\ [][Next]_vars

TypeOK ==
  /\ sequence \in 0..MaxSequence
  /\ opState \in [Ops -> OpStates]
  /\ opSequence \in [Ops -> 0..MaxSequence]
  /\ descriptorDigest \in [Ops -> (DescriptorDigests \cup {"NONE"})]
  /\ reserveDigest \in [Ops -> (RequestDigests \cup {"NONE"})]
  /\ terminalDigest \in [Ops -> (TerminalDigests \cup {"NONE"})]
  /\ authorizationId \in [Ops -> (AuthIds \cup {"NONE"})]
  /\ unresolved \in (Ops \cup {"NONE"})
  /\ namespaceState \in NamespaceStates
  /\ fenceEpoch \in Nat
  /\ logIndex \in Nat

AtMostOneReserved ==
  \A a, b \in Ops :
    (opState[a] = "RESERVED" /\ opState[b] = "RESERVED") => a = b

UnresolvedConsistent ==
  (unresolved = "NONE") <=> (~\E o \in Ops : opState[o] = "RESERVED")

SequenceUnique ==
  \A a, b \in Ops :
    (a # b /\ opState[a] # "ABSENT" /\ opState[b] # "ABSENT")
      => opSequence[a] # opSequence[b]

TerminalIntegrity ==
  \A o \in Ops :
    (opState[o] = "EVIDENCE_COMMITTED" \/ opState[o] = "ABORTED")
      => terminalDigest[o] \in TerminalDigests

AbortHasAuthorization ==
  \A o \in Ops :
    opState[o] = "ABORTED" => authorizationId[o] \in AuthIds

NoSequenceReuse ==
  \A o \in Ops :
    opState[o] # "ABSENT" => opSequence[o] \in 1..sequence

====

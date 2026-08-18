---- MODULE InvocationAuthority ----
EXTENDS Naturals

(***************************************************************************)
(* A deliberately small finite model of one asynchronous invocation.  It  *)
(* explores the complete reachable state graph of this abstraction, rather *)
(* than all implementation states or possible host policies.               *)
(***************************************************************************)

CONSTANTS UseEntryEpoch,
          UseGeneration,
          UseContext,
          UseLifecycleChecks,
          UseDispatchRevalidation,
          UseTerminalLatch,
          UseConsumeLatch

RequestStates == {"idle", "issued", "admitted", "rejected", "dispatched"}
TokenValues == {0, 1}
CompletionClaimValues == 0..2

ToggleToken(token) == 1 - token

VARIABLES requestState,
          ownerActive,
          currentEpoch,
          currentGeneration,
          currentContext,
          capturedEpoch,
          capturedGeneration,
          capturedContext,
          deadlineOpen,
          cancelled,
          resultReady,
          completionClaims,
          admitted,
          consumed,
          badAdmission,
          badDispatch

vars == <<requestState,
          ownerActive,
          currentEpoch,
          currentGeneration,
          currentContext,
          capturedEpoch,
          capturedGeneration,
          capturedContext,
          deadlineOpen,
          cancelled,
          resultReady,
          completionClaims,
          admitted,
          consumed,
          badAdmission,
          badDispatch>>

Init ==
  /\ requestState = "idle"
  /\ ownerActive = TRUE
  /\ currentEpoch = 0
  /\ currentGeneration = 0
  /\ currentContext = 0
  /\ capturedEpoch = 0
  /\ capturedGeneration = 0
  /\ capturedContext = 0
  /\ deadlineOpen = TRUE
  /\ cancelled = FALSE
  /\ resultReady = FALSE
  /\ completionClaims = 0
  /\ admitted = FALSE
  /\ consumed = FALSE
  /\ badAdmission = FALSE
  /\ badDispatch = FALSE

FullAuthority ==
  /\ ownerActive
  /\ ~cancelled
  /\ deadlineOpen
  /\ (IF UseEntryEpoch THEN currentEpoch = capturedEpoch ELSE TRUE)
  /\ (IF UseGeneration THEN currentGeneration = capturedGeneration ELSE TRUE)
  /\ (IF UseContext THEN currentContext = capturedContext ELSE TRUE)

ContractAuthority ==
  /\ ownerActive
  /\ ~cancelled
  /\ deadlineOpen
  /\ currentEpoch = capturedEpoch
  /\ currentGeneration = capturedGeneration
  /\ currentContext = capturedContext

AdmissionGate ==
  /\ resultReady
  /\ (IF UseTerminalLatch THEN requestState = "issued" ELSE TRUE)
  /\ (IF UseEntryEpoch THEN currentEpoch = capturedEpoch ELSE TRUE)
  /\ (IF UseGeneration THEN currentGeneration = capturedGeneration ELSE TRUE)
  /\ (IF UseContext THEN currentContext = capturedContext ELSE TRUE)
  /\ (IF UseLifecycleChecks
        THEN ownerActive /\ ~cancelled /\ deadlineOpen
        ELSE deadlineOpen)

DispatchGate ==
  /\ admitted
  /\ (IF UseConsumeLatch THEN ~consumed ELSE TRUE)
  /\ (IF UseDispatchRevalidation THEN FullAuthority ELSE TRUE)

Start ==
  /\ requestState = "idle"
  /\ requestState' = "issued"
  /\ capturedEpoch' = currentEpoch
  /\ capturedGeneration' = currentGeneration
  /\ capturedContext' = currentContext
  /\ UNCHANGED <<ownerActive, currentEpoch, currentGeneration, currentContext,
                  deadlineOpen, cancelled, resultReady, completionClaims,
                  admitted, consumed, badAdmission, badDispatch>>

BranchExit ==
  /\ requestState \in {"issued", "admitted"}
  /\ ownerActive
  /\ currentEpoch = capturedEpoch
  /\ ownerActive' = FALSE
  /\ UNCHANGED <<requestState, currentEpoch, currentGeneration, currentContext,
                  capturedEpoch, capturedGeneration, capturedContext,
                  deadlineOpen, cancelled, resultReady, completionClaims,
                  admitted, consumed, badAdmission, badDispatch>>

Reenter ==
  /\ ~ownerActive
  /\ currentEpoch = capturedEpoch
  /\ ownerActive' = TRUE
  /\ currentEpoch' = ToggleToken(currentEpoch)
  /\ requestState' = "issued"
  /\ UNCHANGED <<currentGeneration, currentContext, capturedEpoch, capturedGeneration,
                  capturedContext, deadlineOpen, cancelled, resultReady,
                  completionClaims, admitted, consumed, badAdmission,
                  badDispatch>>

Supersede ==
  /\ requestState \in {"issued", "admitted"}
  /\ currentGeneration = capturedGeneration
  /\ currentGeneration' = ToggleToken(currentGeneration)
  /\ UNCHANGED <<requestState, ownerActive, currentEpoch, currentContext,
                  capturedEpoch, capturedGeneration, capturedContext,
                  deadlineOpen, cancelled, resultReady, completionClaims,
                  admitted, consumed, badAdmission, badDispatch>>

ContextChange ==
  /\ requestState \in {"issued", "admitted"}
  /\ currentContext = capturedContext
  /\ currentContext' = ToggleToken(currentContext)
  /\ UNCHANGED <<requestState, ownerActive, currentEpoch, currentGeneration,
                  capturedEpoch, capturedGeneration, capturedContext,
                  deadlineOpen, cancelled, resultReady, completionClaims,
                  admitted, consumed, badAdmission, badDispatch>>

Timeout ==
  /\ requestState = "issued"
  /\ deadlineOpen
  /\ deadlineOpen' = FALSE
  /\ UNCHANGED <<requestState, ownerActive, currentEpoch, currentGeneration,
                  currentContext, capturedEpoch, capturedGeneration,
                  capturedContext, cancelled, resultReady, completionClaims,
                  admitted, consumed, badAdmission, badDispatch>>

Cancel ==
  /\ requestState = "issued"
  /\ ~cancelled
  /\ cancelled' = TRUE
  /\ UNCHANGED <<requestState, ownerActive, currentEpoch, currentGeneration,
                  currentContext, capturedEpoch, capturedGeneration,
                  capturedContext, deadlineOpen, resultReady,
                  completionClaims, admitted, consumed, badAdmission,
                  badDispatch>>

Complete ==
  /\ requestState = "issued"
  /\ ~resultReady
  /\ resultReady' = TRUE
  /\ completionClaims' = completionClaims + 1
  /\ UNCHANGED <<requestState, ownerActive, currentEpoch, currentGeneration,
                  currentContext, capturedEpoch, capturedGeneration,
                  capturedContext, deadlineOpen, cancelled, admitted,
                  consumed, badAdmission, badDispatch>>

DuplicateComplete ==
  /\ requestState = "issued"
  /\ resultReady
  /\ (IF UseTerminalLatch THEN FALSE ELSE TRUE)
  /\ resultReady' = TRUE
  /\ completionClaims' = 2
  /\ UNCHANGED <<requestState, ownerActive, currentEpoch, currentGeneration,
                  currentContext, capturedEpoch, capturedGeneration,
                  capturedContext, deadlineOpen, cancelled, admitted,
                  consumed, badAdmission, badDispatch>>

AdmitAccepted ==
  /\ requestState = "issued"
  /\ AdmissionGate
  /\ requestState' = "admitted"
  /\ admitted' = TRUE
  /\ badAdmission' = (badAdmission \/ ~ContractAuthority)
  /\ UNCHANGED <<ownerActive, currentEpoch, currentGeneration, currentContext,
                  capturedEpoch, capturedGeneration, capturedContext,
                  deadlineOpen, cancelled, resultReady, completionClaims,
                  consumed, badDispatch>>

AdmitRejected ==
  /\ requestState = "issued"
  /\ resultReady
  /\ requestState' = "rejected"
  /\ UNCHANGED <<ownerActive, currentEpoch, currentGeneration, currentContext,
                  capturedEpoch, capturedGeneration, capturedContext,
                  deadlineOpen, cancelled, resultReady, completionClaims,
                  admitted, consumed, badAdmission, badDispatch>>

Dispatch ==
  /\ (requestState = "admitted"
      \/ (~UseConsumeLatch /\ requestState = "dispatched"))
  /\ DispatchGate
  /\ requestState' = "dispatched"
  /\ consumed' = TRUE
  /\ badDispatch' = (badDispatch \/ ~ContractAuthority
                     \/ ((~UseConsumeLatch) /\ consumed))
  /\ UNCHANGED <<ownerActive, currentEpoch, currentGeneration, currentContext,
                  capturedEpoch, capturedGeneration, capturedContext,
                  deadlineOpen, cancelled, resultReady, completionClaims,
                  admitted, badAdmission>>

Next ==
  \/ Start
  \/ BranchExit
  \/ Reenter
  \/ Supersede
  \/ ContextChange
  \/ Timeout
  \/ Cancel
  \/ Complete
  \/ DuplicateComplete
  \/ AdmitAccepted
  \/ AdmitRejected
  \/ Dispatch

TypeOK ==
  /\ requestState \in RequestStates
  /\ ownerActive \in BOOLEAN
  /\ currentEpoch \in TokenValues
  /\ currentGeneration \in TokenValues
  /\ currentContext \in TokenValues
  /\ capturedEpoch \in TokenValues
  /\ capturedGeneration \in TokenValues
  /\ capturedContext \in TokenValues
  /\ deadlineOpen \in BOOLEAN
  /\ cancelled \in BOOLEAN
  /\ resultReady \in BOOLEAN
  /\ completionClaims \in CompletionClaimValues
  /\ admitted \in BOOLEAN
  /\ consumed \in BOOLEAN
  /\ badAdmission \in BOOLEAN
  /\ badDispatch \in BOOLEAN

Safety ==
  /\ ~badAdmission
  /\ ~badDispatch
  /\ completionClaims <= 1
  /\ (IF UseConsumeLatch THEN consumed => requestState = "dispatched" ELSE TRUE)

====

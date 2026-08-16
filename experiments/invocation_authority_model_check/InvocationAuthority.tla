---- MODULE InvocationAuthority ----
EXTENDS Naturals

(***************************************************************************)
(* A deliberately small model of one asynchronous invocation.  The model  *)
(* explores bounded event schedules, rather than claiming to cover all     *)
(* implementation states or all possible host policies.                    *)
(***************************************************************************)

CONSTANTS MaxSteps,
          UseEntryEpoch,
          UseGeneration,
          UseContext,
          UseLifecycleChecks,
          UseDispatchRevalidation,
          UseTerminalLatch,
          UseConsumeLatch

RequestStates == {"idle", "issued", "admitted", "rejected", "dispatched"}

VARIABLES step,
          requestState,
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

vars == <<step,
          requestState,
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
  /\ step = 0
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
  /\ requestState = "issued"
  /\ ownerActive
  /\ ownerActive' = FALSE
  /\ UNCHANGED <<requestState, currentEpoch, currentGeneration, currentContext,
                  capturedEpoch, capturedGeneration, capturedContext,
                  deadlineOpen, cancelled, resultReady, completionClaims,
                  admitted, consumed, badAdmission, badDispatch>>

Reenter ==
  /\ ~ownerActive
  /\ ownerActive' = TRUE
  /\ currentEpoch' = currentEpoch + 1
  /\ requestState' = "issued"
  /\ UNCHANGED <<currentGeneration, currentContext, capturedEpoch, capturedGeneration,
                  capturedContext, deadlineOpen, cancelled, resultReady,
                  completionClaims, admitted, consumed, badAdmission,
                  badDispatch>>

Supersede ==
  /\ requestState \in {"issued", "admitted"}
  /\ currentGeneration' = currentGeneration + 1
  /\ UNCHANGED <<requestState, ownerActive, currentEpoch, currentContext,
                  capturedEpoch, capturedGeneration, capturedContext,
                  deadlineOpen, cancelled, resultReady, completionClaims,
                  admitted, consumed, badAdmission, badDispatch>>

ContextChange ==
  /\ requestState \in {"issued", "admitted"}
  /\ currentContext' = currentContext + 1
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
  /\ completionClaims' = completionClaims + 1
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

BoundedSteps == step <= MaxSteps

StepNext ==
  /\ Next
  /\ step' = step + 1

Safety ==
  /\ ~badAdmission
  /\ ~badDispatch
  /\ completionClaims <= 1
  /\ (IF UseConsumeLatch THEN consumed => requestState = "dispatched" ELSE TRUE)

====

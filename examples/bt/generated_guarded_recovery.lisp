; Deterministic generated guarded recovery subtree fixture.
;
; This file shows the accepted Lisp BT data emitted by
; tools/generate_guarded_recovery_subtree.py for the blocked-path context.

(reactive-sel
  (seq
    (cond blocked-path?)
    (cond observation-fresh?)
    (plan-action
      :name "recovery-turn"
      :planner :mcts
      :budget_ms 20
      :work_max 64
      :state_key recovery-state
      :action_key recovery-action)
    (act execute-recovery-turn)
    (cond recovery-exit?))
  (act safe-stop))

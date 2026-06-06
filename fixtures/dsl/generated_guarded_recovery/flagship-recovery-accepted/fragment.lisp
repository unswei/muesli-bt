(slot recovery-policy
  :contract guarded-recovery.v1
  :install at-tick-boundary
  :fallback safe-stop
  (reactive-sel
    (seq
      (cond blocked-path?)
      (cond observation-fresh?)
      (plan-action
        :name "flagship-recovery-turn"
        :planner :mcts
        :budget_ms 20
        :work_max 64
        :state_key recovery-state
        :action_key recovery-action)
      (act execute-recovery-turn)
      (cond recovery-exit?))
    (act safe-stop)))

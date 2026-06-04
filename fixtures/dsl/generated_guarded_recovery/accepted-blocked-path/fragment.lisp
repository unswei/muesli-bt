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

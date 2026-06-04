(seq
  (cond blocked-path?)
  (plan-action
    :name "recovery-turn"
    :planner :mcts
    :budget_ms 20
    :work_max 64
    :state_key recovery-state
    :action_key recovery-action)
  (act execute-recovery-turn))

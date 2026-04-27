(reactive-sel
  (seq
    (plan-action
      :name "bad-plan"
      :planner :mcts
      :budget_ms -1
      :work_max 64
      :state_key state
      :action_key action)
    (succeed))
  (act stop))

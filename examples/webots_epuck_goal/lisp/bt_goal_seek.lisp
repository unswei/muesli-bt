(defbt goal-tree
  (sel
    (seq
      (cond bb-truthy collision_imminent)
      (act select-action act_avoid 1)
      (running))
    (seq
      (cond bb-truthy goal_available)
      (plan-action :name "epuck-goal-plan"
                   :planner :mcts
                   :budget_ms 10
                   :work_max 280
                   :max_depth 12
                   :gamma 0.96
                   :pw_k 1.5
                   :pw_alpha 0.50
                   :model_service "epuck-goal-v1"
                   :state_key planner_state
                   :action_key planner_action
                   :meta_key planner_meta
                   :top_k 5)
      (act select-action planner_action 2)
      (running))
    (seq
      (act select-action act_roam 3)
      (running))))

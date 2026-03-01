(defbt foraging-tree
  (sel
    (seq
      (cond bb-truthy collision_imminent)
      (act select-action act_recover 4)
      (running))
    (seq
      (cond bb-truthy carrying)
      (plan-action :name "epuck-forage-return"
                   :planner :mcts
                   :budget_ms 10
                   :work_max 260
                   :max_depth 12
                   :gamma 0.96
                   :pw_k 1.5
                   :pw_alpha 0.50
                   :model_service "epuck-target-v1"
                   :state_key planner_state
                   :action_key planner_action
                   :meta_key planner_meta
                   :top_k 5)
      (act select-action planner_action 3)
      (running))
    (seq
      (cond bb-truthy target_visible)
      (plan-action :name "epuck-forage-approach"
                   :planner :mcts
                   :budget_ms 10
                   :work_max 260
                   :max_depth 12
                   :gamma 0.96
                   :pw_k 1.5
                   :pw_alpha 0.50
                   :model_service "epuck-target-v1"
                   :state_key planner_state
                   :action_key planner_action
                   :meta_key planner_meta
                   :top_k 5)
      (act select-action planner_action 2)
      (running))
    (seq
      (act select-action act_search 1)
      (running))))

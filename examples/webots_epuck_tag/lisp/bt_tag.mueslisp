(defbt tag-tree
  (sel
    (seq
      (cond bb-truthy collision_imminent)
      (act select-action act_recover 4)
      (running))
    (seq
      (cond bb-truthy block_mode)
      (act select-action act_block 3)
      (running))
    (seq
      (cond bb-truthy evader_seen)
      (plan-action :name "epuck-tag-pursue"
                   :planner "mcts"
                   :budget_ms 8
                   :work_max 220
                   :max_depth 10
                   :gamma 0.95
                   :pw_k 1.4
                   :pw_alpha 0.50
                   :model_service "epuck-intercept-v1"
                   :state_key planner_state
                   :action_key planner_action
                   :meta_key planner_meta
                   :top_k 5)
      (act select-action planner_action 2)
      (running))
    (seq
      (act select-action act_search 1)
      (running))))

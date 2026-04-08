; Loadable raw DSL mirror of the shared v0.5 flagship tree for the
; PyBullet e-puck-style differential-drive surrogate.

(sel
  (seq
    (cond bb-truthy goal_reached)
    (succeed))
  (seq
    (cond bb-truthy collision_imminent)
    (act select-action act_avoid 1 action_cmd)
    (running))
  (seq
    (plan-action :name "goal-plan"
                 :planner "mcts"
                 :budget_ms 20
                 :work_max 280
                 :max_depth 18
                 :gamma 0.96
                 :pw_k 2.0
                 :pw_alpha 0.5
                 :model_service "flagship-goal-shared-v1"
                 :state_key planner_state
                 :action_key planner_action
                 :meta_key plan-meta
                 :action_schema "flagship.cmd.v1"
                 :top_k 5)
    (act select-action planner_action 2 action_cmd)
    (running))
  (seq
    (act select-action act_goal_direct 3 action_cmd)
    (running)))

; Shared BT skeleton for the v0.5 cross-transport flagship.
; This tree should stay backend-neutral. Backend wrappers are responsible
; for deriving shared blackboard keys and converting the final command intent
; into backend-native actuator commands.

(defbt wheeled-goal-flagship
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
                   :planner :mcts
                   :model_service "flagship-goal-shared-v1"
                   :state_key planner_state
                   :action_key planner_action
                   :meta_key planner_meta
                   :action_schema "flagship.cmd.v1"
                   :budget_ms 10
                   :work_max 280
                   :max_depth 18
                   :gamma 0.96
                   :pw_k 2.0
                   :pw_alpha 0.5
                   :top_k 5)
      (act select-action planner_action 2 action_cmd)
      (running))
    (seq
      (act select-action act_goal_direct 3 action_cmd)
      (running))))

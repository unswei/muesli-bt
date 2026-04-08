(begin
  ; Educational version of the Webots e-puck goal demo.
  ; Keep the full behaviour in this file so the robot's modes are visible
  ; from one place: stop, reverse, clear-left, clear-right, plan, direct-goal.

  (defbt educational-goal-tree
    (sel
      (seq
        (cond bb-truthy goal_reached)
        (act select-action act_stop 0 action_cmd)
        (succeed))
      (seq
        (cond bb-truthy front_blocked)
        (cond bb-truthy obstacle_bias_right)
        (act select-action act_reverse_left 1 action_cmd)
        (running))
      (seq
        (cond bb-truthy front_blocked)
        (act select-action act_reverse_right 2 action_cmd)
        (running))
      (seq
        (cond bb-truthy left_blocked)
        (act select-action act_arc_right 3 action_cmd)
        (running))
      (seq
        (cond bb-truthy right_blocked)
        (act select-action act_arc_left 4 action_cmd)
        (running))
      (seq
        (cond bb-truthy goal_available)
        (plan-action :name "educational-goal-plan"
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
        (act select-action planner_action 5 action_cmd)
        (running))
      (seq
        (act select-action act_goal_direct 6 action_cmd)
        (running))))

  (define max-wheel-speed 6.28)
  (define goal-tolerance-m 0.08)

  (define (nth xs idx)
    (if (= idx 0)
        (car xs)
        (nth (cdr xs) (- idx 1))))

  (define (max2 a b)
    (if (> a b) a b))

  (define (make-action left right)
    (begin
      (define action (map.make))
      (map.set! action 'action_schema "epuck.action.v1")
      (map.set! action 'u (list left right))
      action))

  (define (clamp-wheel x)
    (clamp x (- max-wheel-speed) max-wheel-speed))

  (define (make-wheel-command left right)
    (list (clamp-wheel left) (clamp-wheel right)))

  (define (branch-path branch-id)
    (if (= branch-id 0)
        (list "root" "goal_reached")
        (if (= branch-id 1)
            (list "root" "front_blocked_bias_right" "reverse_left")
            (if (= branch-id 2)
                (list "root" "front_blocked" "reverse_right")
                (if (= branch-id 3)
                    (list "root" "left_blocked" "arc_right")
                    (if (= branch-id 4)
                        (list "root" "right_blocked" "arc_left")
                        (if (= branch-id 5)
                            (list "root" "plan_goal")
                            (list "root" "direct_goal"))))))))

  (define (branch-label branch-id)
    (if (= branch-id 0)
        "goal_reached"
        (if (= branch-id 1)
            "reverse_left"
            (if (= branch-id 2)
                "reverse_right"
                (if (= branch-id 3)
                    "arc_right"
                    (if (= branch-id 4)
                        "arc_left"
                        (if (= branch-id 5)
                            "planner"
                            "direct_goal")))))))

  (define (make-bt-map branch-id)
    (begin
      (define bt (map.make))
      (define status-by-node (map.make))
      (map.set! bt 'active_path (branch-path branch-id))
      (map.set! status-by-node "root" (if (= branch-id 0) "success" "running"))
      (map.set! status-by-node "branch" (branch-label branch-id))
      (map.set! bt 'status_by_node status-by-node)
      bt))

  (define (topk-entry->u row)
    (begin
      (define out (map.make))
      (map.set! out 'u (map.get row "action" (list 0.0 0.0)))
      (map.set! out 'visits (map.get row "visits" 0))
      (map.set! out 'q (map.get row "q" 0.0))
      out))

  (define (topk->u rows)
    (if (null? rows)
        nil
        (cons (topk-entry->u (car rows)) (topk->u (cdr rows)))))

  (define (planner-unused)
    (begin
      (define planner (map.make))
      (map.set! planner 'used #f)
      (map.set! planner 'confidence 0.0)
      planner))

  (define (planner-from-meta planner-meta)
    (begin
      (define planner (map.make))
      (map.set! planner 'used #t)
      (map.set! planner 'budget_ms (map.get planner-meta "budget_ms" 0))
      (map.set! planner 'time_used_ms (map.get planner-meta "time_used_ms" 0.0))
      (map.set! planner 'iters (map.get planner-meta "iters" 0))
      (map.set! planner 'root_visits (map.get planner-meta "root_visits" 0))
      (map.set! planner 'root_children (map.get planner-meta "root_children" 0))
      (map.set! planner 'widen_added (map.get planner-meta "widen_added" 0))
      (map.set! planner 'confidence (map.get planner-meta "confidence" 0.0))
      (map.set! planner 'top_k (topk->u (map.get planner-meta "top_k" nil)))
      planner))

  (define (compute-front-signal proximity)
    (max2 (+ (nth proximity 0) (nth proximity 1))
          (+ (nth proximity 6) (nth proximity 7))))

  (define (compute-left-signal proximity)
    (+ (+ (nth proximity 5) (nth proximity 6)) (nth proximity 7)))

  (define (compute-right-signal proximity)
    (+ (+ (nth proximity 0) (nth proximity 1)) (nth proximity 2)))

  (define (compute-stop-action)
    (make-wheel-command 0.0 0.0))

  (define (compute-reverse-left-action)
    (make-wheel-command -1.4 2.6))

  (define (compute-reverse-right-action)
    (make-wheel-command 2.6 -1.4))

  (define (compute-arc-left-action)
    (make-wheel-command 3.8 1.3))

  (define (compute-arc-right-action)
    (make-wheel-command 1.3 3.8))

  (define (compute-direct-goal-action goal-dist goal-bearing)
    (begin
      (define base (clamp (+ 2.0 (* 1.8 (clamp goal-dist 0.0 1.2))) 1.4 4.8))
      (define steer (clamp (* 2.8 goal-bearing) -2.0 2.0))
      (make-wheel-command (+ base steer) (- base steer))))

  (define (make-planner-state goal-dist goal-bearing front-risk)
    (list goal-dist goal-bearing front-risk))

  (define (on-tick obs)
    (begin
      (define proximity (map.get obs 'proximity (list 0 0 0 0 0 0 0 0)))
      (define goal-dist (map.get obs 'goal_dist 1.0))
      (define goal-bearing (map.get obs 'goal_bearing 0.0))
      (define min-obstacle (map.get obs 'min_obstacle 1.0))
      (define obstacle-front (map.get obs 'obstacle_front (- 1.0 min-obstacle)))

      (define front-signal (compute-front-signal proximity))
      (define left-signal (compute-left-signal proximity))
      (define right-signal (compute-right-signal proximity))

      (define goal-reached (<= goal-dist goal-tolerance-m))
      (define goal-available (< goal-dist 2.5))
      (define front-blocked (or (> obstacle-front 0.45) (< min-obstacle 0.56) (> front-signal 0.60)))
      (define left-blocked (and (not front-blocked) (> left-signal 0.72)))
      (define right-blocked (and (not front-blocked) (> right-signal 0.72)))
      (define obstacle-bias-right (> right-signal left-signal))

      (define tick-inputs
        (list
          (list 'goal_reached goal-reached)
          (list 'goal_available goal-available)
          (list 'front_blocked front-blocked)
          (list 'left_blocked left-blocked)
          (list 'right_blocked right-blocked)
          (list 'obstacle_bias_right obstacle-bias-right)
          (list 'act_stop (compute-stop-action))
          (list 'act_reverse_left (compute-reverse-left-action))
          (list 'act_reverse_right (compute-reverse-right-action))
          (list 'act_arc_left (compute-arc-left-action))
          (list 'act_arc_right (compute-arc-right-action))
          (list 'act_goal_direct (compute-direct-goal-action goal-dist goal-bearing))
          (list 'planner_state (make-planner-state goal-dist goal-bearing obstacle-front))))

      (bt.tick inst tick-inputs)

      (define branch-id
        (if goal-reached
            0
            (bt.blackboard.get inst 'active_branch 6)))
      (define action-vec
        (bt.blackboard.get inst 'action_cmd (compute-direct-goal-action goal-dist goal-bearing)))

      (define planner-meta-raw (bt.blackboard.get inst 'planner_meta "{}"))
      (define planner-meta (json.decode planner-meta-raw))

      (define out (map.make))
      (map.set! out 'schema_version "educational_goal_bt.v1")
      (map.set! out 'action (make-action (nth action-vec 0) (nth action-vec 1)))
      (map.set! out 'bt (make-bt-map branch-id))
      (map.set! out 'planner (if (= branch-id 5) (planner-from-meta planner-meta) (planner-unused)))
      (map.set! out 'goal_dist goal-dist)
      (map.set! out 'goal_bearing goal-bearing)
      (map.set! out 'front_blocked front-blocked)
      (map.set! out 'left_blocked left-blocked)
      (map.set! out 'right_blocked right-blocked)
      (map.set! out 'obstacle_bias_right obstacle-bias-right)
      (map.set! out 'done goal-reached)
      out))

  (env.attach "webots")

  (define env-cfg (map.make))
  (map.set! env-cfg 'demo "goal")
  (map.set! env-cfg 'obs_schema "epuck.goal.obs.v1")
  (map.set! env-cfg 'tick_hz 20)
  (map.set! env-cfg 'steps_per_tick 1)
  (map.set! env-cfg 'realtime #t)
  (env.configure env-cfg)

  (define inst (bt.new-instance educational-goal-tree))
  (bt.export-dot educational-goal-tree "out/educational_tree.dot")

  (define safe-action (make-action 0.0 0.0))
  (define run-cfg (map.make))
  (map.set! run-cfg 'tick_hz 20)
  (map.set! run-cfg 'max_ticks 3200)
  (map.set! run-cfg 'safe_action safe-action)
  (map.set! run-cfg 'realtime #t)
  (map.set! run-cfg 'log_path "logs/goal_educational.jsonl")
  (map.set! run-cfg 'schema_version "educational_goal_bt.v1")

  (env.run-loop run-cfg on-tick))

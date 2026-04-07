(begin
  ; ROS2 wrapper for the shared v0.5 flagship.
  ; This keeps the ROS transport surface on the released Odometry -> Twist path
  ; and derives the shared contract from odometry plus fixed scenario geometry.

  (load "examples/flagship_wheeled/lisp/contract_helpers.lisp")
  (load "examples/flagship_wheeled/lisp/bt_goal_flagship.lisp")

  (define pi 3.141592653589793)
  (define goal-tolerance-m 0.25)
  (define collision-threshold 0.75)
  (define obstacle-range-m 0.50)
  (define obstacle-cone-rad 1.0471975512)
  (define goal-xy (list 1.0 0.0))
  (define obstacle-circles
    (list
      (list 0.45 0.18 0.08)
      (list 0.72 -0.14 0.10)))

  (define (nth xs idx)
    (if (= idx 0)
        (car xs)
        (nth (cdr xs) (- idx 1))))

  (define (wrap-angle angle)
    (if (> angle pi)
        (wrap-angle (- angle (* 2.0 pi)))
        (if (< angle (- pi))
            (wrap-angle (+ angle (* 2.0 pi)))
            angle)))

  (define (make-ros2-action linear-x angular-z t-ms)
    (begin
      (define action (map.make))
      (define u (map.make))
      (map.set! action 'action_schema "ros2.action.v1")
      (map.set! action 't_ms t-ms)
      (map.set! u 'linear_x (clamp linear-x -1.0 1.0))
      (map.set! u 'linear_y 0.0)
      (map.set! u 'angular_z (clamp angular-z -1.0 1.0))
      (map.set! action 'u u)
      action))

  (define (make-shared-action-map cmd)
    (begin
      (define action (map.make))
      (map.set! action 'action_schema "flagship.cmd.v1")
      (map.set! action 'u cmd)
      action))

  (define (branch-path branch-id)
    (if (= branch-id 0)
        (list "root" "goal_reached")
        (if (= branch-id 1)
            (list "root" "avoid")
            (if (= branch-id 2)
                (list "root" "plan_goal")
                (list "root" "direct_goal")))))

  (define (branch-label branch-id)
    (if (= branch-id 0)
        "goal_reached"
        (if (= branch-id 1)
            "avoid"
            (if (= branch-id 2)
                "planner"
                "direct_goal"))))

  (define (make-bt-map branch-id)
    (begin
      (define bt (map.make))
      (define status-by-node (map.make))
      (map.set! bt 'active_path (branch-path branch-id))
      (map.set! status-by-node "root" (if (= branch-id 0) "success" "running"))
      (map.set! status-by-node "branch" (branch-label branch-id))
      (map.set! bt 'status_by_node status-by-node)
      bt))

  (define (planner-unused)
    (begin
      (define planner (map.make))
      (map.set! planner 'used #f)
      (map.set! planner 'confidence 0.0)
      planner))

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

  (define (circle-summary pose-x pose-y yaw circle)
    (begin
      (define dx (- (nth circle 0) pose-x))
      (define dy (- (nth circle 1) pose-y))
      (define radius (nth circle 2))
      (define centre-dist (sqrt (+ (* dx dx) (* dy dy))))
      (define edge-dist (if (< centre-dist radius) 0.0 (- centre-dist radius)))
      (define rel-bearing (wrap-angle (- (atan2 dy dx) yaw)))
      (define risk
        (if (or (>= (abs rel-bearing) obstacle-cone-rad) (>= edge-dist obstacle-range-m))
            0.0
            (clamp
              (* (- 1.0 (/ edge-dist obstacle-range-m))
                 (- 1.0 (/ (abs rel-bearing) obstacle-cone-rad)))
              0.0
              1.0)))
      (list risk rel-bearing edge-dist)))

  (define (best-obstacle-info pose-x pose-y yaw circles)
    (if (null? circles)
        (list 0.0 0.0 obstacle-range-m)
        (begin
          (define current (circle-summary pose-x pose-y yaw (car circles)))
          (define tail-best (best-obstacle-info pose-x pose-y yaw (cdr circles)))
          (if (> (nth current 0) (nth tail-best 0))
              current
              tail-best))))

  (define (compute-avoid-command obstacle-info)
    (begin
      (define obstacle-risk (nth obstacle-info 0))
      (define bearing (nth obstacle-info 1))
      (define angular-mag (clamp (+ 0.55 (* 0.40 obstacle-risk)) 0.0 1.0))
      (define linear-x (if (> obstacle-risk 0.92) -0.10 0.08))
      (if (>= bearing 0.0)
          (make-shared-command linear-x (- angular-mag))
          (make-shared-command linear-x angular-mag))))

  (define (on-tick obs)
    (begin
      (define state-vec (map.get obs 'state_vec (list 0.0 0.0 0.0 0.0 0.0 0.0)))
      (define t-ms (map.get obs 't_ms 0))
      (define pose-x (nth state-vec 0))
      (define pose-y (nth state-vec 1))
      (define yaw (nth state-vec 2))
      (define vx (nth state-vec 3))
      (define vy (nth state-vec 4))
      (define speed (sqrt (+ (* vx vx) (* vy vy))))

      (define goal-dx (- (nth goal-xy 0) pose-x))
      (define goal-dy (- (nth goal-xy 1) pose-y))
      (define goal-dist (sqrt (+ (* goal-dx goal-dx) (* goal-dy goal-dy))))
      (define goal-bearing (wrap-angle (- (atan2 goal-dy goal-dx) yaw)))

      (define obstacle-info (best-obstacle-info pose-x pose-y yaw obstacle-circles))
      (define obstacle-front (nth obstacle-info 0))

      (define goal-reached (goal-reached-q goal-dist goal-tolerance-m))
      (define collision-imminent (collision-imminent-q obstacle-front collision-threshold))
      (define avoid-cmd (compute-avoid-command obstacle-info))
      (define direct-cmd (direct-goal-command goal-bearing))

      (define tick-inputs
        (list
          (list 'goal_reached goal-reached)
          (list 'collision_imminent collision-imminent)
          (list 'goal_dist goal-dist)
          (list 'goal_bearing goal-bearing)
          (list 'pose_x pose-x)
          (list 'pose_y pose-y)
          (list 'yaw yaw)
          (list 'speed speed)
          (list 'obstacle_front obstacle-front)
          (list 'planner_state (make-planner-state goal-dist goal-bearing obstacle-front speed))
          (list 'act_avoid avoid-cmd)
          (list 'act_goal_direct direct-cmd)))

      (bt.tick inst tick-inputs)

      (define branch-id
        (if goal-reached
            0
            (bt.blackboard.get inst 'active_branch 3)))
      (define shared-cmd
        (if goal-reached
            (make-shared-command 0.0 0.0)
            (bt.blackboard.get inst 'action_cmd direct-cmd)))

      (define planner-meta-raw (bt.blackboard.get inst 'planner_meta "{}"))
      (define planner-meta (json.decode planner-meta-raw))

      (define out (map.make))
      (map.set! out 'schema_version "ros2_flagship_goal.v1")
      (map.set! out 'action (make-ros2-action (nth shared-cmd 0) (nth shared-cmd 1) t-ms))
      (map.set! out 'shared_action (make-shared-action-map shared-cmd))
      (map.set! out 'bt (make-bt-map branch-id))
      (map.set! out 'planner (if (= branch-id 2) (planner-from-meta planner-meta) (planner-unused)))
      (map.set! out 'goal_dist goal-dist)
      (map.set! out 'goal_bearing goal-bearing)
      (map.set! out 'obstacle_front obstacle-front)
      (map.set! out 'done goal-reached)
      out))

  (env.attach "ros2")

  (define backend-cfg (map.make))
  (map.set! backend-cfg 'backend_version "ros2.transport.v1")
  (map.set! backend-cfg 'control_hz 20)
  (map.set! backend-cfg 'topic_ns "/robot")
  (map.set! backend-cfg 'obs_source "odom")
  (map.set! backend-cfg 'action_sink "cmd_vel")
  (map.set! backend-cfg 'reset_mode "unsupported")
  (env.configure backend-cfg)

  (define inst (bt.new-instance wheeled-goal-flagship))

  (define safe-action (make-ros2-action 0.0 0.0 0))

  (define run-cfg (map.make))
  (map.set! run-cfg 'tick_hz 20)
  (map.set! run-cfg 'max_ticks 240)
  (map.set! run-cfg 'step_max 240)
  (map.set! run-cfg 'realtime #t)
  (map.set! run-cfg 'stop_on_success #t)
  (map.set! run-cfg 'safe_action safe-action)
  (map.set! run-cfg 'log_path "build/linux-ros2/ros2-flagship-goal.jsonl")
  (map.set! run-cfg 'event_log_path "build/linux-ros2/ros2-flagship-goal/events.jsonl")
  (map.set! run-cfg 'schema_version "ros2_flagship_goal.v1")

  (env.run-loop run-cfg on-tick))

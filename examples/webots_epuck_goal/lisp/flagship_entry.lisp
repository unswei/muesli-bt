; Webots wrapper for the shared v0.5 flagship.
; Ownership:
; - load the shared flagship BT
; - derive shared blackboard keys from Webots observations
; - convert shared command intent into wheel commands

(load "../flagship_wheeled/lisp/contract_helpers.lisp")
(load "../flagship_wheeled/lisp/bt_goal_flagship.lisp")

(define goal-tolerance-m 0.25)
(define collision-threshold 0.75)
(define max-wheel-speed 6.28)

(define speed-state (map.make))
(map.set! speed-state 'prev_x 0.0)
(map.set! speed-state 'prev_y 0.0)
(map.set! speed-state 'prev_t_ms -1)

(define (nth xs idx)
  (if (= idx 0)
      (car xs)
      (nth (cdr xs) (- idx 1))))

(define (make-epuck-action left right)
  (begin
    (define action (map.make))
    (map.set! action 'action_schema "epuck.action.v1")
    (map.set! action 'u (list left right))
    action))

(define (make-shared-action-map cmd)
  (begin
    (define action (map.make))
    (map.set! action 'action_schema "flagship.cmd.v1")
    (map.set! action 'u cmd)
    action))

(define (shared-command->wheel-vec cmd)
  (begin
    (define linear-x (clamp (nth cmd 0) -1.0 1.0))
    (define angular-z (clamp (nth cmd 1) -1.0 1.0))
    (define base (* linear-x 4.6))
    (define turn (* angular-z 2.2))
    (list (clamp (- base turn) (- max-wheel-speed) max-wheel-speed)
          (clamp (+ base turn) (- max-wheel-speed) max-wheel-speed))))

(define (estimate-speed obs)
  (begin
    (define xy (map.get obs 'robot_xy nil))
    (define t-ms (map.get obs 't_ms 0))
    (if (null? xy)
        0.0
        (begin
          (define x (nth xy 0))
          (define y (nth xy 1))
          (define prev-t (map.get speed-state 'prev_t_ms -1))
          (define prev-x (map.get speed-state 'prev_x x))
          (define prev-y (map.get speed-state 'prev_y y))
          (define dt-ms (- t-ms prev-t))
          (define speed
            (if (or (< prev-t 0) (<= dt-ms 0))
                0.0
                (/ (sqrt (+ (* (- x prev-x) (- x prev-x))
                            (* (- y prev-y) (- y prev-y))))
                   (/ dt-ms 1000.0))))
          (map.set! speed-state 'prev_x x)
          (map.set! speed-state 'prev_y y)
          (map.set! speed-state 'prev_t_ms t-ms)
          speed))))

(define (compute-avoid-command proximity obstacle-front)
  (begin
    (define right (+ (+ (nth proximity 0) (nth proximity 1)) (nth proximity 2)))
    (define left (+ (+ (nth proximity 5) (nth proximity 6)) (nth proximity 7)))
    (define angular-mag (clamp (+ 0.55 (* 0.45 (clamp obstacle-front 0.0 1.0))) 0.0 1.0))
    (define linear-x (if (> obstacle-front 0.92) -0.15 0.10))
    (if (> right left)
        (make-shared-command linear-x angular-mag)
        (make-shared-command linear-x (- angular-mag)))))

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

(define (on-tick obs)
  (begin
    (define proximity (map.get obs 'proximity (list 0 0 0 0 0 0 0 0)))
    (define robot-xy (map.get obs 'robot_xy (list 0.0 0.0)))
    (define goal-dist (map.get obs 'goal_dist 1.0))
    (define goal-bearing (map.get obs 'goal_bearing 0.0))
    (define obstacle-front (map.get obs 'obstacle_front (- 1.0 (map.get obs 'min_obstacle 1.0))))
    (define pose-x (nth robot-xy 0))
    (define pose-y (nth robot-xy 1))
    (define yaw (map.get obs 'robot_yaw 0.0))
    (define speed (estimate-speed obs))

    (define goal-reached (goal-reached-q goal-dist goal-tolerance-m))
    (define collision-imminent (collision-imminent-q obstacle-front collision-threshold))
    (define avoid-cmd (compute-avoid-command proximity obstacle-front))
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
    (define wheel-cmd (shared-command->wheel-vec shared-cmd))

    (define planner-meta-raw (bt.blackboard.get inst 'planner_meta "{}"))
    (define planner-meta (json.decode planner-meta-raw))

    (define out (map.make))
    (map.set! out 'schema_version "flagship_goal.v1")
    (map.set! out 'action (make-epuck-action (nth wheel-cmd 0) (nth wheel-cmd 1)))
    (map.set! out 'shared_action (make-shared-action-map shared-cmd))
    (map.set! out 'bt (make-bt-map branch-id))
    (map.set! out 'planner (if (= branch-id 2) (planner-from-meta planner-meta) (planner-unused)))
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

(define inst (bt.new-instance wheeled-goal-flagship))
(bt.export-dot wheeled-goal-flagship "out/flagship_tree.dot")

(define safe-wheel (shared-command->wheel-vec (make-shared-command 0.0 0.0)))
(define safe-action (make-epuck-action (nth safe-wheel 0) (nth safe-wheel 1)))

(define run-cfg (map.make))
(map.set! run-cfg 'tick_hz 20)
(map.set! run-cfg 'max_ticks 640)
(map.set! run-cfg 'stop_on_success #f)
(map.set! run-cfg 'safe_action safe-action)
(map.set! run-cfg 'realtime #t)
(map.set! run-cfg 'log_path "logs/flagship_goal.jsonl")
(map.set! run-cfg 'schema_version "flagship_goal.v1")

(env.run-loop run-cfg on-tick)

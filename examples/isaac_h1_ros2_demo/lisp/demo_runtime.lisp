(load "bt_h1_demo.lisp")

(define pi 3.141592653589793)
(define tau 6.283185307179586)

(define (list-ref xs idx)
  (if (= idx 0)
      (car xs)
      (list-ref (cdr xs) (- idx 1))))

(define (list-len xs)
  (if (null? xs)
      0
      (+ 1 (list-len (cdr xs)))))

(define (bool-not x)
  (if x #f #t))

(define (make-waypoint name x y heading)
  (begin
    (define wp (map.make))
    (map.set! wp 'name name)
    (map.set! wp 'x x)
    (map.set! wp 'y y)
    (map.set! wp 'heading heading)
    wp))

(define (make-default-waypoints)
  (list
    (make-waypoint "forward-pad" 0.80 0.00 0.0)
    (make-waypoint "left-corner" 0.80 0.50 (/ pi 2.0))
    (make-waypoint "return-lane" 0.30 0.50 pi)))

(define (make-default-h1-demo-config)
  (begin
    (define cfg (map.make))
    (map.set! cfg 'topic_ns "/h1_01")
    (map.set! cfg 'control_hz 200)
    (map.set! cfg 'tick_hz 20)
    (map.set! cfg 'realtime #f)
    (map.set! cfg 'max_ticks 240)
    (map.set! cfg 'step_max 240)
    (map.set! cfg 'obs_source "odom")
    (map.set! cfg 'action_sink "cmd_vel")
    (map.set! cfg 'backend_version "ros2.transport.v1")
    (map.set! cfg 'obs_schema "ros2.obs.v1")
    (map.set! cfg 'state_schema "ros2.state.v1")
    (map.set! cfg 'action_schema "ros2.action.v1")
    (map.set! cfg 'reset_mode "unsupported")
    (map.set! cfg 'log_path "logs/isaac_h1_demo.run-loop.jsonl")
    (map.set! cfg 'event_log_path "logs/isaac_h1_demo.mbt.evt.v1.jsonl")
    (map.set! cfg 'event_log_ring_size 512)
    (map.set! cfg 'schema_version "isaac_h1_ros2_demo.v1")
    (map.set! cfg 'stand_ticks 12)
    (map.set! cfg 'obs_timeout_ms 600)
    (map.set! cfg 'turn_tol 0.18)
    (map.set! cfg 'goal_tol 0.12)
    (map.set! cfg 'turn_gain 1.4)
    (map.set! cfg 'turn_rate_max 0.60)
    (map.set! cfg 'walk_speed 0.40)
    (map.set! cfg 'walk_turn_gain 0.8)
    (map.set! cfg 'walk_turn_rate_max 0.25)
    (map.set! cfg 'stop_on_success #t)
    (map.set! cfg 'waypoints (make-default-waypoints))
    cfg))

(define (make-runtime-state cfg)
  (begin
    (define runtime (map.make))
    (map.set! runtime 'tick_index 0)
    (map.set! runtime 'waypoint_index 0)
    (map.set! runtime 'mission_complete (= (list-len (map.get cfg 'waypoints nil)) 0))
    (map.set! runtime 'last_fresh_t_ms 0)
    (map.set! runtime 'last_branch_id 0)
    (map.set! runtime 'last_branch_name "boot")
    runtime))

(define (make-command linear-x linear-y angular-z)
  (list linear-x linear-y angular-z))

(define (make-action-map action-schema t-ms cmd)
  (begin
    (define action (map.make))
    (define u (map.make))
    (map.set! action 'action_schema action-schema)
    (map.set! action 't_ms t-ms)
    (map.set! u 'linear_x (list-ref cmd 0))
    (map.set! u 'linear_y (list-ref cmd 1))
    (map.set! u 'angular_z (list-ref cmd 2))
    (map.set! action 'u u)
    action))

(define (wrap-angle angle)
  (if (> angle pi)
      (wrap-angle (- angle tau))
      (if (< angle (- 0.0 pi))
          (wrap-angle (+ angle tau))
          angle)))

(define (branch-name branch-id)
  (if (= branch-id 1)
      "timeout_stop"
      (if (= branch-id 2)
          "initialise_stand"
          (if (= branch-id 3)
              "goal_stop"
              (if (= branch-id 4)
                  "turn_to_heading"
                  "walk_segment")))))

(define (branch-path branch-id)
  (if (= branch-id 1)
      (list "root" "timeout_stop")
      (if (= branch-id 2)
          (list "root" "initialise_stand")
          (if (= branch-id 3)
              (list "root" "goal_stop")
              (if (= branch-id 4)
                  (list "root" "turn_to_heading")
                  (list "root" "walk_segment"))))))

(define (bt-status-name branch-id)
  (if (= branch-id 3)
      "success"
      "running"))

(define (make-bt-map branch-id)
  (begin
    (define bt (map.make))
    (define status-by-node (map.make))
    (map.set! bt 'active_path (branch-path branch-id))
    (map.set! status-by-node "root" (bt-status-name branch-id))
    (map.set! status-by-node "branch" (branch-name branch-id))
    (map.set! bt 'status_by_node status-by-node)
    bt))

(define (waypoint-count cfg)
  (list-len (map.get cfg 'waypoints nil)))

(define (active-waypoint cfg runtime)
  (let ((idx (map.get runtime 'waypoint_index 0))
        (waypoints (map.get cfg 'waypoints nil)))
    (if (>= idx (list-len waypoints))
        nil
        (list-ref waypoints idx))))

(define (waypoint-distance2 wp x y)
  (let ((dx (- (map.get wp 'x 0.0) x))
        (dy (- (map.get wp 'y 0.0) y)))
    (+ (* dx dx) (* dy dy))))

(define (advance-waypoint-index! runtime cfg x y)
  (begin
    (define idx (map.get runtime 'waypoint_index 0))
    (define total (waypoint-count cfg))
    (define goal-tol (map.get cfg 'goal_tol 0.12))
    (define goal-tol2 (* goal-tol goal-tol))
    (if (>= idx total)
        (map.set! runtime 'mission_complete #t)
        (begin
          (define wp (active-waypoint cfg runtime))
          (if (<= (waypoint-distance2 wp x y) goal-tol2)
              (begin
                (map.set! runtime 'waypoint_index (+ idx 1))
                (advance-waypoint-index! runtime cfg x y))
              (map.set! runtime 'mission_complete #f))))))

(define (compute-turn-command cfg heading-error)
  (make-command
    0.0
    0.0
    (clamp
      (* (map.get cfg 'turn_gain 1.4) heading-error)
      (- 0.0 (map.get cfg 'turn_rate_max 0.60))
      (map.get cfg 'turn_rate_max 0.60))))

(define (compute-walk-command cfg heading-error)
  (make-command
    (map.get cfg 'walk_speed 0.40)
    0.0
    (clamp
      (* (map.get cfg 'walk_turn_gain 0.8) heading-error)
      (- 0.0 (map.get cfg 'walk_turn_rate_max 0.25))
      (map.get cfg 'walk_turn_rate_max 0.25))))

(define (waypoint-summary wp)
  (if (null? wp)
      nil
      (begin
        (define summary (map.make))
        (map.set! summary 'name (map.get wp 'name ""))
        (map.set! summary 'x (map.get wp 'x 0.0))
        (map.set! summary 'y (map.get wp 'y 0.0))
        (map.set! summary 'heading (map.get wp 'heading 0.0))
        summary)))

(define (make-demo-map runtime cfg wp heading-error stale-ms obs-ready)
  (begin
    (define demo (map.make))
    (map.set! demo 'obs_ready obs-ready)
    (map.set! demo 'branch (map.get runtime 'last_branch_name "boot"))
    (map.set! demo 'waypoint_index (map.get runtime 'waypoint_index 0))
    (map.set! demo 'waypoint_total (waypoint-count cfg))
    (map.set! demo 'target (waypoint-summary wp))
    (map.set! demo 'pose_x (map.get runtime 'x 0.0))
    (map.set! demo 'pose_y (map.get runtime 'y 0.0))
    (map.set! demo 'yaw (map.get runtime 'yaw 0.0))
    (map.set! demo 'heading_error heading-error)
    (map.set! demo 'stale_ms stale-ms)
    (if (null? wp)
        (map.set! demo 'distance2 0.0)
        (map.set! demo 'distance2
                  (waypoint-distance2 wp
                                      (map.get runtime 'x 0.0)
                                      (map.get runtime 'y 0.0))))
    demo))

(define (run-h1-demo demo-cfg)
  (begin
    (define cfg demo-cfg)

    (env.attach "ros2")

    (define backend-cfg (map.make))
    (map.set! backend-cfg 'control_hz (map.get cfg 'control_hz 200))
    (map.set! backend-cfg 'topic_ns (map.get cfg 'topic_ns "/h1_01"))
    (map.set! backend-cfg 'obs_source (map.get cfg 'obs_source "odom"))
    (map.set! backend-cfg 'action_sink (map.get cfg 'action_sink "cmd_vel"))
    (map.set! backend-cfg 'realtime (map.get cfg 'realtime #f))
    (map.set! backend-cfg 'backend_version (map.get cfg 'backend_version "ros2.transport.v1"))
    (map.set! backend-cfg 'obs_schema (map.get cfg 'obs_schema "ros2.obs.v1"))
    (map.set! backend-cfg 'state_schema (map.get cfg 'state_schema "ros2.state.v1"))
    (map.set! backend-cfg 'action_schema (map.get cfg 'action_schema "ros2.action.v1"))
    (map.set! backend-cfg 'reset_mode (map.get cfg 'reset_mode "unsupported"))
    (env.configure backend-cfg)

    (events.enable #t)
    (events.set-path (map.get cfg 'event_log_path "logs/isaac_h1_demo.mbt.evt.v1.jsonl"))
    (events.set-ring-size (map.get cfg 'event_log_ring_size 512))

    (define inst (bt.new-instance h1-demo-tree))
    (define runtime (make-runtime-state cfg))
    (define action-schema (map.get cfg 'action_schema "ros2.action.v1"))
    (define stop-command (make-command 0.0 0.0 0.0))
    (define safe-action (make-action-map action-schema 0 stop-command))

    (define run-cfg (map.make))
    (map.set! run-cfg 'tick_hz (map.get cfg 'tick_hz 20))
    (map.set! run-cfg 'realtime (map.get cfg 'realtime #f))
    (map.set! run-cfg 'max_ticks (map.get cfg 'max_ticks 240))
    (map.set! run-cfg 'step_max (map.get cfg 'step_max (map.get cfg 'max_ticks 240)))
    (map.set! run-cfg 'safe_action safe-action)
    (map.set! run-cfg 'log_path (map.get cfg 'log_path "logs/isaac_h1_demo.run-loop.jsonl"))
    (map.set! run-cfg 'schema_version (map.get cfg 'schema_version "isaac_h1_ros2_demo.v1"))
    (map.set! run-cfg 'stop_on_success (map.get cfg 'stop_on_success #t))

    (define on-tick
      (lambda (obs)
        (begin
          (map.set! runtime 'tick_index (+ (map.get runtime 'tick_index 0) 1))

          (define state (map.get obs 'state (map.make)))
          (define pose (map.get state 'pose (map.make)))
          (define state-vec (map.get state 'state_vec (list 0.0 0.0 0.0 0.0 0.0 0.0)))
          (define flags (map.get obs 'flags (map.make)))
          (define has-sample (map.get flags 'has_sample #f))
          (define fresh-obs (map.get flags 'fresh_obs #f))
          (define t-ms (map.get obs 't_ms 0))
          (define now-ms (time.now-ms))
          (define yaw (list-ref state-vec 2))

          (if fresh-obs
              (map.set! runtime 'last_fresh_t_ms now-ms)
              nil)

          (map.set! runtime 'x (map.get pose 'x 0.0))
          (map.set! runtime 'y (map.get pose 'y 0.0))
          (map.set! runtime 'yaw yaw)

          (advance-waypoint-index! runtime cfg
                                   (map.get runtime 'x 0.0)
                                   (map.get runtime 'y 0.0))

          (define mission-complete (map.get runtime 'mission_complete #f))
          (define target (active-waypoint cfg runtime))
          (define target-heading (if (null? target) yaw (map.get target 'heading yaw)))
          (define heading-error (wrap-angle (- target-heading yaw)))
          (define stale-ms
            (if (= (map.get runtime 'last_fresh_t_ms 0) 0)
                now-ms
                (- now-ms (map.get runtime 'last_fresh_t_ms 0))))
          (define initialising
            (or (< (map.get runtime 'tick_index 0) (map.get cfg 'stand_ticks 12))
                (bool-not has-sample)))
          (define timeout-active
            (and has-sample
                 (> stale-ms (map.get cfg 'obs_timeout_ms 600))))
          (define need-turn
            (and (bool-not initialising)
                 (bool-not timeout-active)
                 (bool-not mission-complete)
                 (> (abs heading-error) (map.get cfg 'turn_tol 0.18))))

          (define act-stop stop-command)
          (define act-stand stop-command)
          (define act-turn (compute-turn-command cfg heading-error))
          (define act-walk
            (if mission-complete
                stop-command
                (compute-walk-command cfg heading-error)))

          (define tick-inputs
            (list
              (list 'timeout_active timeout-active)
              (list 'initialising initialising)
              (list 'mission_complete mission-complete)
              (list 'need_turn need-turn)
              (list 'act_stop act-stop)
              (list 'act_stand act-stand)
              (list 'act_turn act-turn)
              (list 'act_walk act-walk)))

          (bt.tick inst tick-inputs)

          (define branch-id
            (bt.blackboard.get inst 'active_branch
                               (if mission-complete 3 5)))
          (map.set! runtime 'last_branch_id branch-id)
          (map.set! runtime 'last_branch_name (branch-name branch-id))

          (define action-vec (bt.blackboard.get inst 'action_vec stop-command))

          (define out (map.make))
          (map.set! out 'schema_version (map.get cfg 'schema_version "isaac_h1_ros2_demo.v1"))
          (map.set! out 'action (make-action-map action-schema t-ms action-vec))
          (map.set! out 'bt (make-bt-map branch-id))
          (map.set! out 'demo
                    (make-demo-map runtime
                                   cfg
                                   target
                                   heading-error
                                   stale-ms
                                   (and has-sample (bool-not timeout-active))))
          (map.set! out 'done mission-complete)
          out)))

    (define result (env.run-loop run-cfg on-tick))

    (define out (map.make))
    (map.set! out 'env_info (env.info))
    (map.set! out 'result result)
    (map.set! out 'runtime runtime)
    out))

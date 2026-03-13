(begin
  (env.attach "ros2")

  (define backend-cfg (map.make))
  (map.set! backend-cfg 'control_hz 20)
  (map.set! backend-cfg 'topic_ns "/robot")
  (map.set! backend-cfg 'obs_source "odom")
  (map.set! backend-cfg 'action_sink "cmd_vel")
  (map.set! backend-cfg 'reset_mode "unsupported")
  (env.configure backend-cfg)

  (define safe (map.make))
  (define safe-u (map.make))
  (map.set! safe 'action_schema "ros2.action.v1")
  (map.set! safe 't_ms 0)
  (map.set! safe-u 'linear_x 0.0)
  (map.set! safe-u 'linear_y 0.0)
  (map.set! safe-u 'angular_z 0.0)
  (map.set! safe 'u safe-u)

  (define run-cfg (map.make))
  (map.set! run-cfg 'tick_hz 20)
  (map.set! run-cfg 'max_ticks 10)
  (map.set! run-cfg 'step_max 10)
  (map.set! run-cfg 'safe_action safe)
  (map.set! run-cfg 'log_path "build/linux-ros2/ros2-live-run.jsonl")

  (define result
    (env.run-loop
      run-cfg
      (lambda (obs)
        (let ((action (map.make))
              (u (map.make)))
          (map.set! action 'action_schema "ros2.action.v1")
          (map.set! action 't_ms (map.get obs 't_ms 0))
          (map.set! u 'linear_x 0.0)
          (map.set! u 'linear_y 0.0)
          (map.set! u 'angular_z 0.2)
          (map.set! action 'u u)
          action))))

  (list (env.info) result))

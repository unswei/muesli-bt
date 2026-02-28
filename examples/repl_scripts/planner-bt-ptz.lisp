(planner.set-base-seed 777)
(planner.set-log-enabled #t)

(defbt ptz-track-loop
  (sel
    (cond ptz-target-centered ptz-state 0.05)
    (seq
      (plan-action
        :name "ptz-planner"
        :budget_ms 12
        :iters_max 1200
        :model_service "ptz-track"
        :state_key ptz-state
        :action_key ptz-action
        :meta_key ptz-meta
        :gamma 0.96
        :max_depth 26
        :c_ucb 1.25
        :pw_k 2.2
        :pw_alpha 0.55)
      (act apply-planned-ptz ptz-state ptz-action ptz-state)
      (running))))

(define inst (bt.new-instance ptz-track-loop))

;; State layout: (pan tilt ball_x ball_y ball_vx ball_vy)
(bt.tick inst '((ptz-state (0.0 0.0 0.70 -0.45 0.00 0.00))))

(bt.tick inst)
(bt.tick inst)
(bt.tick inst)
(bt.tick inst)
(bt.tick inst)
(bt.tick inst)
(bt.tick inst)
(bt.tick inst)
(bt.tick inst)
(bt.tick inst)

(bt.blackboard.dump inst)
(planner.logs.dump 16)

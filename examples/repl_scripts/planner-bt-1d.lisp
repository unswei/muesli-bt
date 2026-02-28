(planner.set-base-seed 4242)
(planner.set-log-enabled #t)

(defbt one-d-control
  (sel
    (cond goal-reached-1d state 1.0 0.05)
    (seq
      (plan-action
        :name "one-d-planner"
        :planner "mcts"
        :budget_ms 8
        :work_max 800
        :model_service "toy-1d"
        :state_key state
        :action_key action
        :meta_key plan-meta
        :seed_key seed
        :gamma 0.95
        :max_depth 24
        :c_ucb 1.2
        :pw_k 2.0
        :pw_alpha 0.5)
      (act apply-planned-1d state action state)
      (running))))

(define inst (bt.new-instance one-d-control))

;; Seed and initial state.
(bt.tick inst '((state 0.0) (seed 42)))

;; Continue planning and applying action on each tick.
(bt.tick inst)
(bt.tick inst)
(bt.tick inst)
(bt.tick inst)
(bt.tick inst)
(bt.tick inst)
(bt.tick inst)

;; Inspect current planner output and state.
(bt.blackboard.dump inst)
(planner.logs.dump 12)

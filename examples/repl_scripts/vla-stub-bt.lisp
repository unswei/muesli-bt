(defbt vla-loop
  (sel
    (seq
      (vla-wait :name "vla-loop" :job_key vla-job :action_key vla-action :meta_key vla-meta)
      (act apply-planned-1d state vla-action state)
      (cond goal-reached-1d state 1.0 0.05))
    (seq
      (act bb-put-float fallback-action 0.0)
      (vla-request :name "vla-loop"
                   :job_key vla-job
                   :instruction_key instruction
                   :state_key state
                   :deadline_ms 20
                   :dims 1
                   :bound_lo -1.0
                   :bound_hi 1.0)
      (running))))

(define inst (bt.new-instance vla-loop))

;; Seed initial observation and instruction once.
(print (bt.tick inst '((state 0.0) (instruction "move toward +1.0"))))

(define (tick-loop n)
  (if (= n 0)
      (begin
        (print "blackboard")
        (print (bt.blackboard.dump inst))
        (print "vla logs")
        (print (vla.logs.dump 20)))
      (begin
        (print (bt.tick inst))
        (tick-loop (- n 1)))))

(tick-loop 25)

(events.enable #t)
(events.set-path "/tmp/muesli-bt-minimal-real-bt.mbt.evt.v1.jsonl")
(events.set-ring-size 128)

(defbt safe-goal
  (sel
    (seq
      (cond bb-has obstacle-clear)
      (act bb-put-int command 1))
    (act bb-put-int command 0)))

(define inst (bt.new-instance safe-goal))

(bt.tick inst '((obstacle-clear #t)))
(bt.blackboard.get inst 'command -1)

(bt.reset inst)
(bt.tick inst)
(bt.blackboard.get inst 'command -1)

(events.dump 20)

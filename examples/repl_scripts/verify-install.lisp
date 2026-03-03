(events.enable #t)
(events.set-path "logs/verify-install.mbt.evt.v1.jsonl")
(events.set-ring-size 256)

(defbt verify-install-tree
  (seq
    (cond always-true)
    (act running-then-success 1)))

(define inst (bt.new-instance verify-install-tree))
(bt.tick inst)
(events.snapshot-bb)
(bt.tick inst)
(events.dump 40)

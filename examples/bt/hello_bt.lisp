(defbt hello-tree
  (seq
    (cond always-true)
    (act running-then-success 1)))

(define inst (bt.new-instance hello-tree))
(bt.tick inst)
(bt.tick inst)
(bt.trace.snapshot inst)

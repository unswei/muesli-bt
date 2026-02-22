(define tree
  (bt.compile
    '(seq
       (cond always-true)
       (act running-then-success 1))))

(define inst (bt.new-instance tree))
(bt.tick inst)
(bt.tick inst)
(bt.trace.dump inst)

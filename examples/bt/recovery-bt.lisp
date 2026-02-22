(define tree
  (bt.compile
    '(sel
       (seq
         (cond target-visible)
         (act approach-target)
         (act grasp))
       (act search-target))))

(define inst (bt.new-instance tree))
(bt.tick inst)
(bt.tick inst)
(bt.tick inst)
(bt.blackboard.dump inst)

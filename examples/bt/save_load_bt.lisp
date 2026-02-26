(define dsl-path "/tmp/muesli_bt_saved_tree.lisp")
(define bin-path "/tmp/muesli_bt_saved_tree.mbt")

(defbt patrol-tree
  (sel
    (seq
      (cond target-visible)
      (act approach-target)
      (act grasp))
    (act search-target)))

(bt.save-dsl patrol-tree dsl-path)
(define patrol-from-dsl (bt.load-dsl dsl-path))

(bt.save patrol-tree bin-path)
(define patrol-from-bin (bt.load bin-path))

(define dsl-inst (bt.new-instance patrol-from-dsl))
(define bin-inst (bt.new-instance patrol-from-bin))

(bt.tick dsl-inst)
(bt.tick dsl-inst)

(bt.tick bin-inst)
(bt.tick bin-inst)

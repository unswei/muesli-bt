(defbt minimal-robot
  (sel
    (seq
      (cond obstacle-clear)
      (act drive-forward))
    (act safe-stop)))

(defbt obstacle-tree
  (sel
    (seq
      (cond bb-truthy collision_imminent)
      (act select-action act_avoid 1)
      (running))
    (seq
      (cond bb-truthy wall_detected)
      (act select-action act_wall 2)
      (running))
    (seq
      (act select-action act_roam 3)
      (running))))

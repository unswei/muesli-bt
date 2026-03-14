(defbt h1-hero-tree
  (sel
    (seq
      (cond bb-truthy timeout_active)
      (act select-action act_stop 1)
      (running))
    (seq
      (cond bb-truthy initialising)
      (act select-action act_stand 2)
      (running))
    (seq
      (cond bb-truthy mission_complete)
      (act select-action act_stop 3)
      (succeed))
    (seq
      (cond bb-truthy need_turn)
      (act select-action act_turn 4)
      (running))
    (seq
      (act select-action act_walk 5)
      (running))))

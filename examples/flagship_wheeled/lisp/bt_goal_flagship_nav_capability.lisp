; Experimental navigation-capability variant for the shared wheeled flagship.
;
; Backend wrappers keep loading bt_goal_flagship.lisp. This variant proves that
; the goal-seeking lane can be delegated to cap.navigation.v1 without changing
; the canonical cross-transport flagship tree.

(defbt wheeled-goal-flagship-nav-capability
  (sel
    (seq
      (cond bb-truthy goal_reached)
      (succeed))
    (seq
      (cond bb-truthy collision_imminent)
      (act cap-navigation-cancel)
      (act select-action act_avoid 1 action_cmd)
      (running))
    (seq
      (act cap-navigation-tick)
      (succeed))))

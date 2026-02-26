# Example: Fallback/Recovery BT

```lisp
(defbt tree
  (sel
    (seq
      (cond target-visible)
      (act approach-target)
      (act grasp))
    (act search-target)))

(define inst (bt.new-instance tree))
(bt.tick inst)
(bt.tick inst)
(bt.tick inst)
(bt.blackboard.dump inst)
```

Interpretation:

- `sel` uses left branch as primary path
- if target is not visible, primary branch fails
- fallback `search-target` runs and updates blackboard
- later ticks can re-enter primary branch and complete

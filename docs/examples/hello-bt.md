# Example: Hello BT

```lisp
(defbt tree
  (seq
    (cond always-true)
    (act running-then-success 1)))

(define inst (bt.new-instance tree))
(bt.tick inst) ; running
(bt.tick inst) ; success
(bt.trace.snapshot inst)
```

What this shows:

- BT authoring sugar with `defbt`
- compile + instance creation
- `running` transition to `success`
- trace inspection after ticks

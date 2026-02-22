# Example: Hello BT

```lisp
(define tree
  (bt.compile
    '(seq
       (cond always-true)
       (act running-then-success 1))))

(define inst (bt.new-instance tree))
(bt.tick inst) ; running
(bt.tick inst) ; success
(bt.trace.dump inst)
```

What this shows:

- quoted BT DSL (domain-specific language)
- compile + instance creation
- `running` transition to `success`
- trace inspection after ticks

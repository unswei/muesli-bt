# BT node option schemas

These schemas describe the public option objects for Behaviour Tree (BT) DSL leaves that accept key/value options.

The Lisp DSL writes options as flat keyword pairs, for example:

```lisp
(plan-action :planner :mcts :budget_ms 20 :state_key state)
```

For validation and documentation, the same options are represented as a JSON object:

```json
{
  ":planner": ":mcts",
  ":budget_ms": 20,
  ":state_key": "state"
}
```

The `x-muesli-aliases` extension records accepted Lisp aliases. Validators should canonicalise aliases before checking duplicate options.

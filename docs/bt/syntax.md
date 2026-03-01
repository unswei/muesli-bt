# Behaviour Tree Syntax

BTs are authored in the BT language (DSL: a small purpose-built language for behaviour trees) and compiled into `bt_def` values.

Preferred authoring forms:

```lisp
(bt (seq (cond battery-ok) (act approach-target)))
```

```lisp
(defbt patrol
  (sel
    (seq
      (cond target-visible)
      (act approach-target)
      (act grasp))
    (act search-target)))
```

`bt.compile` remains the low-level primitive:

```lisp
(bt.compile '(seq (cond battery-ok) (act approach-target)))
```

## Syntax Summary

## Composites

### `seq`

```lisp
(seq child1 child2 ...)
```

### `sel`

```lisp
(sel child1 child2 ...)
```

## Leaves

### `cond`

```lisp
(cond name arg...)
```

`name` must match a registered condition callback.

### `act`

```lisp
(act name arg...)
```

`name` must match a registered action callback.

### `plan-action`

```lisp
(plan-action key value key value ...)
```

`plan-action` is a built-in planning leaf that runs bounded-time planning and writes action output to blackboard.
`plan-action` is planner-agnostic and dispatches to `planner.plan`.

Common keys:

- `:name`
- `:planner`
- `:budget_ms`
- `:work_max`
- `:model_service`
- `:state_key`
- `:action_key`
- `:meta_key`
- `:seed_key`

### `vla-request`

```lisp
(vla-request key value key value ...)
```

Submits a non-blocking VLA job and returns `running`.

### `vla-wait`

```lisp
(vla-wait key value key value ...)
```

Polls a VLA job and returns `running` until a terminal status is available.

### `vla-cancel`

```lisp
(vla-cancel key value key value ...)
```

Cancels an in-flight VLA job id.

## Decorators

### `invert`

```lisp
(invert child)
```

### `repeat`

```lisp
(repeat n child)
```

`n` must be a non-negative integer.

### `retry`

```lisp
(retry n child)
```

`n` must be a non-negative integer.

## Utility Nodes

```lisp
(succeed)
(fail)
(running)
```

## BT Save/Load APIs

### Portable DSL path (recommended)

- `(bt.to-dsl bt-def)` -> canonical DSL form (data, not string)
- `(bt.save-dsl bt-def "tree.lisp")`
- `(bt.load-dsl "tree.lisp")`

### Compiled binary path (fast load)

- `(bt.save bt-def "tree.mbt")`
- `(bt.load "tree.mbt")`

Binary format notes:

- magic header: `MBT1`
- explicit format version
- little-endian marker
- unsupported/unknown versions are rejected

Use DSL save/load when long-term portability is the priority.

## See Also

- [PlanAction Node Reference](plan-action-node.md)
- [Planner Configuration Reference](planner-configuration.md)
- [VLA BT Nodes](vla-nodes.md)
- [VLA Integration In BTs](vla-integration.md)

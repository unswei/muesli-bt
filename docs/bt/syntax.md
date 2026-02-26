# Behaviour Tree Syntax

BTs are authored in the BT DSL and compiled into `bt_def` values.

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

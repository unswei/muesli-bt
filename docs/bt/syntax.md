# Behaviour Tree DSL Syntax

In v1, BTs are authored as quoted Lisp data and compiled with `bt.compile`.

```lisp
(bt.compile '(seq (cond battery-ok) (act approach-target)))
```

## Why Quoting Is Required

Without quoting, Lisp would try to evaluate forms as normal function calls.

BT forms are data consumed by the BT compiler, so quoting is required in v1.

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

`name` must be a symbol or string that matches a registered condition callback.

### `act`

```lisp
(act name arg...)
```

`name` must be a symbol or string that matches a registered action callback.

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

## Complete Example

```lisp
(define tree
  (bt.compile
    '(sel
       (seq
         (cond target-visible)
         (act approach-target)
         (act grasp))
       (act search-target))))
```

## Fallback/Recovery Example

```lisp
(define recover-tree
  (bt.compile
    '(sel
       (seq
         (cond battery-ok)
         (cond target-visible)
         (act approach-target)
         (act grasp))
       (act search-target))))
```

The second branch acts as fallback behaviour if the primary branch cannot proceed.

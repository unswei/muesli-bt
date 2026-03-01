# `defbt`

**Signature:** `(defbt name dsl-form) -> bt_def`

## What It Does

Compiles a BT language form (DSL: a small purpose-built language for behaviour trees) and binds it to a symbol.

## Arguments And Return

- Arguments: symbol name and one DSL form
- Return: `bt_def`

## Errors And Edge Cases

- First argument must be a symbol.

## Examples

### Minimal

```lisp
(defbt tree (succeed))
```

### Realistic

```lisp
(begin
  (defbt patrol
    (reactive-sel
      (reactive-seq
        (cond target-visible)
        (async-seq
          (act approach-target)
          (act grasp)))
      (mem-seq
        (act search-target)
        (running))))
  (define i (bt.new-instance patrol))
  (bt.tick i))
```

## Notes

- Equivalent to `(define name (bt ...))`.

## See Also

- [Reference Index](../index.md)
- [Language Semantics](../../semantics.md)

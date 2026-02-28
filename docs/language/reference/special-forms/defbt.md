# `defbt`

**Signature:** `(defbt name dsl-form) -> bt_def`

## What It Does

Compiles a BT DSL form and binds it to a symbol.

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
(begin (defbt patrol (sel (cond target-visible) (act search-target))) (define i (bt.new-instance patrol)) (bt.tick i))
```

## Notes

- Equivalent to `(define name (bt ...))`.

## See Also

- [Reference Index](../index.md)
- [Language Semantics](../../semantics.md)

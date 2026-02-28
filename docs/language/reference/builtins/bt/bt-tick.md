# `bt.tick`

**Signature:** `(bt.tick inst [input-pairs]) -> symbol`

## What It Does

Ticks one instance and returns `success`, `failure`, or `running`.

## Arguments And Return

- Arguments: bt_instance, optional list of `(key value)` pairs
- Return: status symbol

## Errors And Edge Cases

- Arity must be 1 or 2; input and handle validation errors.

## Examples

### Minimal

```lisp
(begin (define d (bt (succeed))) (define i (bt.new-instance d)) (bt.tick i))
```

### Realistic

```lisp
(begin (define d (bt (cond bb-has foo))) (define i (bt.new-instance d)) (bt.tick i '((foo 1))))
```

## Notes

- Optional input list updates blackboard before tick.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)

# `bt.reset`

**Signature:** `(bt.reset inst) -> nil`

## What It Does

Resets per-instance memory and blackboard.

## Arguments And Return

- Arguments: bt_instance
- Return: nil

## Errors And Edge Cases

- Handle/type validation errors.

## Examples

### Minimal

```lisp
(begin (define d (bt (succeed))) (define i (bt.new-instance d)) (bt.reset i))
```

### Realistic

```lisp
(begin (define d (bt (cond bb-has foo))) (define i (bt.new-instance d)) (bt.tick i '((foo 1))) (bt.reset i))
```

## Notes

- Does not implicitly clear logs.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)

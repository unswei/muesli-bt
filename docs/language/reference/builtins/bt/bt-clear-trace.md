# `bt.clear-trace`

**Signature:** `(bt.clear-trace inst) -> nil`

## What It Does

Clears trace buffer for an instance.

## Arguments And Return

- Arguments: bt_instance
- Return: nil

## Errors And Edge Cases

- Type/handle validation errors.

## Examples

### Minimal

```lisp
(begin (define d (bt (succeed))) (define i (bt.new-instance d)) (bt.clear-trace i))
```

### Realistic

```lisp
(begin (defbt t (act running-then-success 1)) (define i (bt.new-instance t)) (bt.tick i) (bt.clear-trace i))
```

## Notes

- Affects trace only.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)

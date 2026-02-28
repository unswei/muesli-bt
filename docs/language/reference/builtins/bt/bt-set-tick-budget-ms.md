# `bt.set-tick-budget-ms`

**Signature:** `(bt.set-tick-budget-ms inst ms) -> nil`

## What It Does

Sets per-instance tick-budget threshold (milliseconds).

## Arguments And Return

- Arguments: bt_instance, integer milliseconds
- Return: nil

## Errors And Edge Cases

- Type/handle validation errors.

## Examples

### Minimal

```lisp
(begin (define d (bt (succeed))) (define i (bt.new-instance d)) (bt.set-tick-budget-ms i 20))
```

### Realistic

```lisp
(begin (defbt t (act async-sleep-ms 5)) (define i (bt.new-instance t)) (bt.set-tick-budget-ms i 1))
```

## Notes

- Used for overrun warning/profiling thresholds.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)

# `bt.set-trace-enabled`

**Signature:** `(bt.set-trace-enabled inst bool) -> nil`

## What It Does

Enables/disables trace recording for an instance.

## Arguments And Return

- Arguments: bt_instance, boolean
- Return: nil

## Errors And Edge Cases

- Type/handle validation errors.

## Examples

### Minimal

```lisp
(begin (define d (bt (succeed))) (define i (bt.new-instance d)) (bt.set-trace-enabled i #t))
```

### Realistic

```lisp
(begin (define d (bt (succeed))) (define i (bt.new-instance d)) (bt.set-trace-enabled i #f))
```

## Notes

- Does not clear existing trace entries.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)

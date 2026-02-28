# `bt.new-instance`

**Signature:** `(bt.new-instance bt-def) -> bt_instance`

## What It Does

Creates runtime instance from definition.

## Arguments And Return

- Arguments: bt_def
- Return: bt_instance

## Errors And Edge Cases

- Handle/type validation errors.

## Examples

### Minimal

```lisp
(begin (define d (bt (succeed))) (bt.new-instance d))
```

### Realistic

```lisp
(begin (defbt t (act running-then-success 1)) (bt.new-instance t))
```

## Notes

- Instances hold mutable runtime state.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)

# `bt.clear-logs`

**Signature:** `(bt.clear-logs) -> nil`

## What It Does

Clears global runtime-host logs.

## Arguments And Return

- Arguments: none
- Return: nil

## Errors And Edge Cases

- Arity validation errors.

## Examples

### Minimal

```lisp
(bt.clear-logs)
```

### Realistic

```lisp
(begin (bt.clear-logs) (bt.logs.dump))
```

## Notes

- Shared log sink across instances.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)

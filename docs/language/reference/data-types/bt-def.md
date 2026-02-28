# `bt_def`

**Signature:** `compiled BT definition handle`

## What It Does

Immutable compiled BT graph handle.

## Arguments And Return

- Return: `bt_def`

## Errors And Edge Cases

- BT APIs validate handle type.

## Examples

### Minimal

```lisp
(bt (succeed))
```

### Realistic

```lisp
(begin (define d (bt (act always-success))) d)
```

## Notes

- Reusable across many instances.

## See Also

- [Reference Index](../index.md)
- [Language Semantics](../../semantics.md)

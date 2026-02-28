# `bt.log.snapshot`

**Signature:** `(bt.log.snapshot) -> string`

## What It Does

Alias of `bt.logs.snapshot`.

## Arguments And Return

- Arguments: none
- Return: string

## Errors And Edge Cases

- Arity validation errors.

## Examples

### Minimal

```lisp
(bt.log.snapshot)
```

### Realistic

```lisp
(begin (bt.clear-logs) (bt.log.snapshot))
```

## Notes

- Legacy alias.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)

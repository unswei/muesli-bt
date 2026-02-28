# `bt.log.dump`

**Signature:** `(bt.log.dump) -> string`

## What It Does

Alias of `bt.logs.dump`.

## Arguments And Return

- Arguments: none
- Return: string

## Errors And Edge Cases

- Arity validation errors.

## Examples

### Minimal

```lisp
(bt.log.dump)
```

### Realistic

```lisp
(begin (bt.clear-logs) (bt.log.dump))
```

## Notes

- Legacy alias.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)

# `bt.logs.dump`

**Signature:** `(bt.logs.dump) -> string`

## What It Does

Returns runtime-[host](../../../../terminology.md#host) log sink text.

## Arguments And Return

- Arguments: none
- Return: string

## Errors And Edge Cases

- Arity validation errors.

## Examples

### Minimal

```lisp
(bt.logs.dump)
```

### Realistic

```lisp
(begin (bt.clear-logs) (bt.logs.dump))
```

## Notes

- Global log view.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)

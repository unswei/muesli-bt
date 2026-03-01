# `bt.logs.snapshot`

**Signature:** `(bt.logs.snapshot) -> string`

## What It Does

Returns runtime-[host](../../../../terminology.md#host) logs (snapshot alias).

## Arguments And Return

- Arguments: none
- Return: string

## Errors And Edge Cases

- Arity validation errors.

## Examples

### Minimal

```lisp
(bt.logs.snapshot)
```

### Realistic

```lisp
(begin (bt.clear-logs) (bt.logs.snapshot))
```

## Notes

- Alias of dump path.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)

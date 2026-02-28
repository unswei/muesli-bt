# `time.now-ms`

**Signature:** `(time.now-ms) -> int`

## What It Does

Returns monotonic milliseconds.

## Arguments And Return

- Arguments: none
- Return: integer milliseconds

## Errors And Edge Cases

- Arity validation errors.

## Examples

### Minimal

```lisp
(time.now-ms)
```

### Realistic

```lisp
(begin (define t0 (time.now-ms)) (time.now-ms))
```

## Notes

- Uses steady monotonic clock.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)

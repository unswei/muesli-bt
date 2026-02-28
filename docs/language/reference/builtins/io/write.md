# `write`

**Signature:** `(write x) -> x`

## What It Does

Prints readable representation for parse-back when supported.

## Arguments And Return

- Arguments: one value
- Return: original value

## Errors And Edge Cases

- Unreadable runtime values raise errors.

## Examples

### Minimal

```lisp
(write (list 1 2 3))
```

### Realistic

```lisp
(begin (define payload "ok") (write payload))
```

## Notes

- Serialization-oriented output.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)

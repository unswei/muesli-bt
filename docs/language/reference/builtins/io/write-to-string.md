# `write-to-string`

**Signature:** `(write-to-string x) -> string`

## What It Does

Returns readable representation as a string.

## Arguments And Return

- Arguments: one value
- Return: string

## Errors And Edge Cases

- Same readability constraints as `write`.

## Examples

### Minimal

```lisp
(write-to-string (list 1 2))
```

### Realistic

```lisp
(begin (define s (write-to-string "hi")) s)
```

## Notes

- Useful with `save` and logs.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)

# `string`

**Signature:** ``"text"``

## What It Does

Text value with common escapes.

## Arguments And Return

- Return: string value

## Errors And Edge Cases

- Invalid escapes raise parse errors.

## Examples

### Minimal

```lisp
"hello"
```

### Realistic

```lisp
(write-to-string "ok")
```

## Notes

- Readable via `write` and `save`.

## See Also

- [Reference Index](../index.md)
- [Language Semantics](../../semantics.md)

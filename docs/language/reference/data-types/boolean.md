# `boolean`

**Signature:** ``#t` / `#f``

## What It Does

Boolean truth values.

## Arguments And Return

- Return: boolean value

## Errors And Edge Cases

- Self-evaluating.

## Examples

### Minimal

```lisp
#t
```

### Realistic

```lisp
(if #f 1 2)
```

## Notes

- Only `nil` and `#f` are falsey.

## See Also

- [Reference Index](../index.md)
- [Language Semantics](../../semantics.md)

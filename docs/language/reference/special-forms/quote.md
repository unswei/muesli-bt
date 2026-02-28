# `quote`

**Signature:** `(quote x) -> any`

## What It Does

Returns `x` without evaluating it.

## Arguments And Return

- Arguments: one expression
- Return: unevaluated data

## Errors And Edge Cases

- Arity error unless exactly 1 argument.

## Examples

### Minimal

```lisp
(quote x)
```

### Realistic

```lisp
(begin (define data '(1 2 3)) (car data))
```

## Notes

- Reader sugar: `'x`.

## See Also

- [Reference Index](../index.md)
- [Language Semantics](../../semantics.md)

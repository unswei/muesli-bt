# `unquote`

**Signature:** `(unquote expr) -> any (in quasiquote)`

## What It Does

Evaluates one expression during quasiquote expansion.

## Arguments And Return

- Arguments: one expression
- Return: value inserted into quasiquote

## Errors And Edge Cases

- Using outside quasiquote raises an error.

## Examples

### Minimal

```lisp
`(a ,(+ 1 2))
```

### Realistic

```lisp
(begin (define x 3) `(value ,x))
```

## Notes

- Reader sugar: `,expr`.

## See Also

- [Reference Index](../index.md)
- [Language Semantics](../../semantics.md)

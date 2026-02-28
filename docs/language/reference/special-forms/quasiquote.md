# `quasiquote`

**Signature:** `(quasiquote expr) -> data`

## What It Does

Builds template data with selective evaluation.

## Arguments And Return

- Arguments: one expression
- Return: expanded data

## Errors And Edge Cases

- Arity error unless exactly one argument.

## Examples

### Minimal

```lisp
`(a ,(+ 1 2) b)
```

### Realistic

```lisp
(begin (define name "target-visible") `(seq (cond ,name) (act grasp)))
```

## Notes

- Reader sugar: `` `expr ``.

## See Also

- [Reference Index](../index.md)
- [Language Semantics](../../semantics.md)

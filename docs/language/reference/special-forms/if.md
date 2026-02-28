# `if`

**Signature:** `(if test then [else]) -> any`

## What It Does

Conditionally evaluates one of two branches.

## Arguments And Return

- Arguments: 2 or 3 expressions
- Return: selected branch result

## Errors And Edge Cases

- Arity error unless 2 or 3 arguments.

## Examples

### Minimal

```lisp
(if #t 1 2)
```

### Realistic

```lisp
(begin (define x 10) (if (> x 5) x 0))
```

## Notes

- Only the selected branch is evaluated.

## See Also

- [Reference Index](../index.md)
- [Language Semantics](../../semantics.md)

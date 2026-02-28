# `cond`

**Signature:** `(cond (test expr...) ... (else expr...)) -> any`

## What It Does

Evaluates clauses top-to-bottom and returns first match.

## Arguments And Return

- Arguments: one or more clauses
- Return: first matching clause result or `nil`

## Errors And Edge Cases

- `else` must be last clause.

## Examples

### Minimal

```lisp
(cond ((< 1 0) 'no) (else 'yes))
```

### Realistic

```lisp
(begin (define x -1) (cond ((< x 0) "neg") ((= x 0) "zero") (else "pos")))
```

## Notes

- Each clause must be a proper list.

## See Also

- [Reference Index](../index.md)
- [Language Semantics](../../semantics.md)

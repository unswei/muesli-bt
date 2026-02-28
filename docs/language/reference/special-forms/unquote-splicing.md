# `unquote-splicing`

**Signature:** `(unquote-splicing expr) -> splice (in quasiquote list context)`

## What It Does

Splices list elements into a quasiquoted list.

## Arguments And Return

- Arguments: one expression returning a list
- Return: spliced elements

## Errors And Edge Cases

- Using outside quasiquote list context raises an error.

## Examples

### Minimal

```lisp
`(a ,@(list 2 3) b)
```

### Realistic

```lisp
(begin (define xs (list "x" "y")) `(prefix ,@xs suffix))
```

## Notes

- Reader sugar: `,@expr`.

## See Also

- [Reference Index](../index.md)
- [Language Semantics](../../semantics.md)

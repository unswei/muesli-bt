# `print`

**Signature:** `(print x...) -> nil`

## What It Does

Prints display-oriented representation to stdout.

## Arguments And Return

- Arguments: zero or more values
- Return: nil

## Errors And Edge Cases

- No special-case value restrictions.

## Examples

### Minimal

```lisp
(print "hello")
```

### Realistic

```lisp
(begin (define x 3) (print "x=" x) x)
```

## Notes

- Display-oriented output.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)

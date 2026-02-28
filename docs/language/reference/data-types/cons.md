# `cons`

**Signature:** `pair cell`

## What It Does

Two-slot pair used for list construction.

## Arguments And Return

- Return: cons pair

## Errors And Edge Cases

- `car`/`cdr` require cons input.

## Examples

### Minimal

```lisp
(cons 1 2)
```

### Realistic

```lisp
(list 1 2 3)
```

## Notes

- Proper lists are cons chains ending in `nil`.

## See Also

- [Reference Index](../index.md)
- [Language Semantics](../../semantics.md)

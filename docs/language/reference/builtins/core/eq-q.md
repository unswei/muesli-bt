# `eq?`

**Signature:** `(eq? a b) -> bool`

## What It Does

Checks runtime equality semantics.

## Arguments And Return

- Arguments: two values
- Return: boolean

## Errors And Edge Cases

- Arity error unless two arguments.

## Examples

### Minimal

```lisp
(eq? 4 4)
```

### Realistic

```lisp
(eq? 'a 'a)
```

## Notes

- Numbers compare by value.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)

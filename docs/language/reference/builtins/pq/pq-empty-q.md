# `pq.empty?`

**Signature:** `(pq.empty? q) -> bool`

## What It Does

Checks whether a queue has no entries.

## Arguments And Return

- Arguments: `pq`
- Return: boolean

## Errors And Edge Cases

- Type/arity validation errors.

## Examples

### Minimal

```lisp
(begin (define q (pq.make)) (pq.empty? q))
```

### Realistic

```lisp
(begin (define q (pq.make)) (pq.push! q 1 'x) (pq.pop! q) (pq.empty? q))
```

## Notes

- O(1).

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)

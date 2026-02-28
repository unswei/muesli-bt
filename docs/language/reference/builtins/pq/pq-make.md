# `pq.make`

**Signature:** `(pq.make [capacity]) -> pq`

## What It Does

Creates an empty min-priority queue.

## Arguments And Return

- Arguments: optional non-negative integer capacity
- Return: `pq` handle

## Errors And Edge Cases

- Type/arity validation errors.
- Negative capacity is rejected.

## Examples

### Minimal

```lisp
(pq.make)
```

### Realistic

```lisp
(begin (define q (pq.make 32)) (pq.empty? q))
```

## Notes

- Capacity is a reserve hint only.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)

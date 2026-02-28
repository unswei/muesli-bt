# `pq.len`

**Signature:** `(pq.len q) -> int`

## What It Does

Returns the number of entries in a priority queue.

## Arguments And Return

- Arguments: `pq`
- Return: integer length

## Errors And Edge Cases

- Type/arity validation errors.

## Examples

### Minimal

```lisp
(begin (define q (pq.make)) (pq.len q))
```

### Realistic

```lisp
(begin (define q (pq.make)) (pq.push! q 2 'a) (pq.push! q 1 'b) (pq.len q))
```

## Notes

- O(1).

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)

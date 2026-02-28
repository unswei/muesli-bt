# `pq.push!`

**Signature:** `(pq.push! q priority value) -> int`

## What It Does

Inserts a `(priority, value)` entry.

## Arguments And Return

- Arguments: `pq`, numeric priority, payload value
- Return: queue size after insertion

## Errors And Edge Cases

- Priority must be integer or float.
- Non-finite priorities (`inf`, `nan`) are rejected.
- Type/arity validation errors.

## Examples

### Minimal

```lisp
(begin (define q (pq.make)) (pq.push! q 1 'x))
```

### Realistic

```lisp
(begin
  (define q (pq.make))
  (pq.push! q 3 'slow)
  (pq.push! q 1 'fast)
  (pq.peek q))
```

## Notes

- Equal priorities preserve FIFO insertion order.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)

# `pq`

**Signature:** `mutable min-priority queue handle`

## What It Does

Stores `(priority, value)` entries in ascending priority order.

## Arguments And Return

- Return: `pq` handle

## Errors And Edge Cases

- Use `pq.make` to construct before use.
- Priorities must be numeric and finite.
- `write`/`write-to-string` cannot serialise `pq` as readable data.

## Examples

### Minimal

```lisp
(pq.make)
```

### Realistic

```lisp
(begin
  (define q (pq.make))
  (pq.push! q 1.2 'task)
  (pq.pop! q))
```

## Notes

- Stable FIFO tie-break is applied for equal priorities.
- Payload values are GC-traced while stored in the queue.

## See Also

- [Reference Index](../index.md)
- [Language Semantics](../../semantics.md)

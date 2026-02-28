# `pq.peek`

**Signature:** `(pq.peek q) -> (priority value)`

## What It Does

Returns the current minimum-priority entry without removing it.

## Arguments And Return

- Arguments: `pq`
- Return: 2-item list `(priority value)`

## Errors And Edge Cases

- Errors if queue is empty.
- Type/arity validation errors.

## Examples

### Minimal

```lisp
(begin (define q (pq.make)) (pq.push! q 1 'x) (pq.peek q))
```

### Realistic

```lisp
(begin
  (define q (pq.make))
  (pq.push! q 2 'task-a)
  (pq.push! q 1 'task-b)
  (list (pq.peek q) (pq.len q)))
```

## Notes

- Non-mutating operation.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)

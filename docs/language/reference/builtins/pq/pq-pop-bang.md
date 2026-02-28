# `pq.pop!`

**Signature:** `(pq.pop! q) -> (priority value)`

## What It Does

Removes and returns the current minimum-priority entry.

## Arguments And Return

- Arguments: `pq`
- Return: 2-item list `(priority value)`

## Errors And Edge Cases

- Errors if queue is empty.
- Type/arity validation errors.

## Examples

### Minimal

```lisp
(begin (define q (pq.make)) (pq.push! q 1 'x) (pq.pop! q))
```

### Realistic

```lisp
(begin
  (define q (pq.make))
  (pq.push! q 2 'slow)
  (pq.push! q 1 'fast)
  (list (pq.pop! q) (pq.pop! q)))
```

## Notes

- Mutates queue contents.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)

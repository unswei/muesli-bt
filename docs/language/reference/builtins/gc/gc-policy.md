# `gc.policy`

**Signature:** `(gc.policy) -> keyword`

## What It Does

Returns the current Lisp heap GC policy.

Policies:

- `:default`: automatic collection behaves as the normal runtime policy
- `:between-ticks`: automatic collection requested during a BT tick is deferred until the tick exits
- `:manual`: automatic `maybe_collect` checks do not collect; explicit `gc-stats` can still force collection
- `:fail-on-tick-gc`: collection during a BT tick raises an error

## Arguments And Return

- Arguments: none
- Return: policy keyword

## Errors And Edge Cases

- Arity validation errors.

## Examples

### Minimal

```lisp
(gc.policy)
```

### Realistic

```lisp
(begin
  (gc.set-policy! "between-ticks")
  (gc.policy))
```

## Notes

- Policy changes affect the process-wide Lisp heap.
- GC lifecycle events are emitted as `gc_begin` and `gc_end` when the default runtime host event stream is active.

## See Also

- [`gc.set-policy!`](gc-set-policy.md)
- [`gc-stats`](gc-stats.md)
- [tick audit record](../../../../observability/tick-audit.md)

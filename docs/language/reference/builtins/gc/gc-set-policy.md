# `gc.set-policy!`

**Signature:** `(gc.set-policy! policy) -> keyword`

## What It Does

Sets the process-wide Lisp heap GC policy and returns the selected policy.

Accepted policies:

- `:default`
- `:between-ticks`
- `:manual`
- `:fail-on-tick-gc`

Strings without the leading colon are also accepted.

## Arguments And Return

- `policy`: string or symbol
- Return: selected policy keyword

## Errors And Edge Cases

- Unknown policy raises a runtime error.
- `:fail-on-tick-gc` raises an error if collection is requested or forced during a BT tick.
- `:manual` disables automatic collection from `maybe_collect`, but explicit `gc-stats` still forces collection.

## Examples

### Minimal

```lisp
(gc.set-policy! "between-ticks")
```

### Realistic

```lisp
(begin
  (gc.set-policy! "manual")
  (gc.policy))
```

## Notes

- Reset the policy to `:default` after experiments unless the host owns GC policy explicitly.
- Use `:between-ticks` or `:fail-on-tick-gc` for v0.7 audit experiments that need to prove GC does not run inside the tick critical section.

## See Also

- [`gc.policy`](gc-policy.md)
- [`gc-stats`](gc-stats.md)
- [tick audit record](../../../../observability/tick-audit.md)

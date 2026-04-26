# `gc-stats`

**Signature:** `(gc-stats) -> nil`

## What It Does

Forces GC and prints allocation counters.

## Arguments And Return

- Arguments: none
- Return: nil

## Errors And Edge Cases

- Arity validation errors.

## Examples

### Minimal

```lisp
(gc-stats)
```

### Realistic

```lisp
(begin (list 1 2 3) (gc-stats))
```

## Notes

- Useful for retention checks.
- Emits `gc_begin` and `gc_end` lifecycle events through the default runtime host event stream.
- Under `:fail-on-tick-gc`, forcing collection during a BT tick raises an error.

## See Also

- [`gc.policy`](gc-policy.md)
- [`gc.set-policy!`](gc-set-policy.md)
- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)

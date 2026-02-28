# `planner.get-base-seed`

**Signature:** `(planner.get-base-seed) -> int`

## What It Does

Returns current planner base seed.

## Arguments And Return

- Arguments: none
- Return: non-negative integer seed

## Errors And Edge Cases

- type/arity validation errors

## Examples

### Minimal

```lisp
(planner.get-base-seed)
```

### Realistic

```lisp
(begin
  (planner.set-base-seed 777)
  (planner.get-base-seed))
```

## Notes

- Use with `planner.set-base-seed` to control deterministic replay.

## See Also

- [Reference Index](../../index.md)
- [Planner Configuration](../../../../bt/planner-configuration.md)
